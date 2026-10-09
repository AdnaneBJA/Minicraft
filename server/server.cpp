#include "server.h"

#include "protocol.h"

#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>
#include <ixwebsocket/IXWebSocketServer.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <random>
#include <utility>

namespace {

using Clock = std::chrono::steady_clock;
constexpr auto kTickLength = std::chrono::microseconds(1'000'000 / 60);
constexpr int kListenBacklog = 16;
// Every connection is pinged this often, and closed if the previous ping got no answer: a player whose laptop
// went to sleep leaves the game after 5-10 seconds instead of whenever TCP gives up (many minutes).
constexpr int kPingIntervalSeconds = 5;

}  // namespace

namespace {

std::int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

}  // namespace

Server::Server(int resetAfterTicks, std::optional<StatsConfig> stats) : resetAfterTicks_(resetAfterTicks) {
    if (stats) {
        reporter_ = std::make_unique<StatsReporter>(stats->url, stats->token);
        reportPrefix_ = "server" + std::to_string(nowMs());
        std::printf("Reporting stats to %s\n", stats->url.c_str());
    }
}

void Server::report(const std::string& type, const std::string& player, int count) {
    if (!reporter_) return;
    const std::string seed = world_ ? std::to_string(world_->seed()) : "0";
    reporter_->add({{.id = seed + "-" + reportPrefix_ + "-" + std::to_string(reportCounter_++),
                     .type = type,
                     .at = nowMs(),
                     .player = player,
                     .count = count}});
}

Server::~Server() {
    stop();
    if (socketServer_) socketServer_->stop();  // closes every connection and joins their threads
}

bool Server::start(std::uint16_t port) {
    ix::initNetSystem();
    socketServer_ = std::make_unique<ix::WebSocketServer>(
        port, "0.0.0.0", kListenBacklog, static_cast<std::size_t>(protocol::kMaxPlayers + protocol::kMaxObservers),
        ix::WebSocketServer::kDefaultHandShakeTimeoutSecs, ix::SocketServer::kDefaultAddressFamily,
        kPingIntervalSeconds);
    // A new connection gets an id; from then on its thread only queues what it hears.
    socketServer_->setOnConnectionCallback(
        [this](std::weak_ptr<ix::WebSocket> weakSocket, std::shared_ptr<ix::ConnectionState>) {
            const std::shared_ptr<ix::WebSocket> socket = weakSocket.lock();
            if (!socket) return;
            const int id = nextClientId_++;
            socket->setOnMessageCallback([this, id, weakSocket](const ix::WebSocketMessagePtr& message) {
                switch (message->type) {
                    case ix::WebSocketMessageType::Open:
                        push({.kind = NetEvent::Kind::Connected, .clientId = id, .socket = weakSocket});
                        break;
                    case ix::WebSocketMessageType::Message:
                        push({.kind = NetEvent::Kind::Message, .clientId = id, .bytes = message->str});
                        break;
                    case ix::WebSocketMessageType::Close:
                        push({.kind = NetEvent::Kind::Disconnected, .clientId = id});
                        break;
                    default: break;  // ping, pong, errors (a close follows)
                }
            });
        });
    if (const auto [ok, error] = socketServer_->listen(); !ok) {
        std::printf("Could not listen on port %u: %s\n", port, error.c_str());
        socketServer_.reset();
        return false;
    }
    socketServer_->start();
    running_ = true;
    std::printf("minicraft-server listening on port %u\n", port);
    return true;
}

void Server::stop() {
    // Only an atomic store, so a signal handler may call it too. run() notices at its next wake-up, within a tick.
    running_ = false;
}

void Server::push(NetEvent event) {
    {
        const std::lock_guard lock(eventsMutex_);
        events_.push_back(std::move(event));
    }
    eventsReady_.notify_one();
}

void Server::run() {
    // A fixed 60 Hz clock for the lobbies; in between, handle what the connections queued.
    auto nextTick = Clock::now();
    while (running_) {
        const auto now = Clock::now();
        if (now >= nextTick) {
            tickWorld();
            answerPings();
            nextTick += kTickLength;
            if (now - nextTick > std::chrono::seconds(1)) nextTick = now;  // fell far behind: don't try to catch up
            continue;
        }
        std::deque<NetEvent> events;
        {
            std::unique_lock lock(eventsMutex_);
            eventsReady_.wait_until(lock, nextTick, [this] { return !events_.empty(); });
            events.swap(events_);
        }
        for (NetEvent& event : events) handle(event);
    }
}

void Server::handle(NetEvent& event) {
    switch (event.kind) {
        case NetEvent::Kind::Connected: {
            const Client& client = clients_[event.clientId] = {.socket = event.socket, .id = event.clientId};
            std::printf("Player %d connected\n", client.id);
            sendTo(client, protocol::Welcome{client.id});
            break;
        }
        case NetEvent::Kind::Message:
            if (Client* client = clientOf(event.clientId)) {
                const auto* data = reinterpret_cast<const std::uint8_t*>(event.bytes.data());
                handleMessage(*client, {data, event.bytes.size()});
            }
            break;
        case NetEvent::Kind::Disconnected:
            if (Client* client = clientOf(event.clientId)) disconnect(*client);
            break;
    }
}

template <typename Message>
void Server::sendTo(const Client& client, const Message& message) {
    // The connection may have closed already (its Disconnected is still in the queue): then there's no one to tell.
    if (const std::shared_ptr<ix::WebSocket> socket = client.socket.lock()) {
        const std::vector<std::uint8_t> bytes = protocol::encode(message);
        socket->sendBinary(std::string(bytes.begin(), bytes.end()));
    }
}

void Server::handleMessage(Client& client, std::span<const std::uint8_t> bytes) {
    using protocol::MessageType;
    const auto type = protocol::typeOf(bytes);
    if (!type) return;
    // An observer only ever pings; everything else it sends is ignored.
    if (client.observing) {
        if (*type == MessageType::ProbePing) {
            if (const auto ping = protocol::decode<protocol::ProbePing>(bytes)) {
                client.pendingPing = ping->id;  // a newer ping replaces one not answered yet
            }
        }
        return;
    }
    // Until a player is in the world, Hello (or Observe, for a probe) is the only thing they can do.
    if (client.name.empty() && *type != MessageType::Hello && *type != MessageType::Observe) return;
    switch (*type) {
        case MessageType::Hello:
            if (const auto hello = protocol::decode<protocol::Hello>(bytes); hello && client.name.empty()) {
                // A refused name leaves the connection open: the player can try another one.
                if (!protocol::isValidName(hello->name)) {
                    sendTo(client, protocol::ErrorMessage{"Invalid name"});
                    return;
                }
                if (nameInUse(hello->name)) {
                    sendTo(client, protocol::ErrorMessage{"Name already in use"});
                    return;
                }
                if (playersOnline() >= protocol::kMaxPlayers) {
                    sendTo(client, protocol::ErrorMessage{"Server is full"});
                    return;
                }
                client.name = hello->name;
                std::printf("Player %d is %s\n", client.id, client.name.c_str());
                enterWorld(client);
            }
            break;
        case MessageType::Observe:
            if (protocol::decode<protocol::Observe>(bytes) && client.name.empty()) observe(client);
            break;
        case MessageType::Input:
            if (const auto input = protocol::decode<protocol::InputMessage>(bytes); input && world_) {
                world_->setInput(client.id, input->input);
            }
            break;
        case MessageType::Command:
            if (const auto command = protocol::decode<protocol::CommandMessage>(bytes); command && world_) {
                world_->addCommand(client.id, command->command);
            }
            break;
        case MessageType::Chat:
            if (const auto chatMessage = protocol::decode<protocol::ChatMessage>(bytes)) chat(client, chatMessage->text);
            break;
        case MessageType::StateHash:
            if (const auto report = protocol::decode<protocol::StateHashMessage>(bytes)) {
                if (world_ && world_->reportHash(client.id, report->tick, report->hash)) {
                    std::printf("The world went out of sync at tick %d\n", report->tick);
                    tellEveryone("", "Desync detected at tick " + std::to_string(report->tick) + "!");
                }
            }
            break;
        default: break;  // server -> client messages: ignore
    }
}

void Server::disconnect(Client& client) {
    if (client.observing) {
        std::printf("Observer %d disconnected\n", client.id);
    } else {
        std::printf("Player %d (%s) disconnected\n", client.id, client.name.c_str());
    }
    const int id = client.id;
    const std::string name = client.name;
    const auto played = std::chrono::steady_clock::now() - client.joinedAt;
    clients_.erase(id);
    if (name.empty() || !world_) return;  // never got into the world
    report("PlayerLeft", name, static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(played).count()));
    world_->removeMember(id);
    if (world_->empty()) {
        std::printf("Everyone left: the world ends\n");
        world_.reset();
        observer_.reset();
        resetWarned_ = false;
    } else {
        tellEveryone("", name + " left the game");
    }
}

// ---------------------------------------------------------------------------------------------------------------
// The world

void Server::startWorld() {
    std::random_device random;
    world_ = std::make_unique<Lobby>(random());
    resetWarned_ = false;
    std::printf("A new world begins (seed %u)\n", world_->seed());
    if (reporter_) {
        observer_ = std::make_unique<StatsObserver>(world_->seed());
        report("WorldStarted", "", 1);
    }
    sendWorldToObservers();
}

void Server::enterWorld(Client& client) {
    if (!world_) startWorld();
    // The newcomer gets the world's seed and every tick so far; their own Join comes with the next tick.
    sendTo(client, protocol::Joined{world_->seed(), world_->history()});
    world_->addMember(client.id, client.name);
    client.joinedAt = std::chrono::steady_clock::now();
    if (observer_) observer_->nameJoined(client.id, client.name);
    report("PlayerJoined", client.name, 1);
    tellEveryone("", client.name + " joined the game");
}

void Server::resetWorld() {
    startWorld();
    for (const auto& [id, client] : clients_) {
        if (client.name.empty()) continue;
        sendTo(client, protocol::Joined{world_->seed(), world_->history()});
        world_->addMember(client.id, client.name);
        if (observer_) observer_->nameJoined(client.id, client.name);
    }
}

void Server::chat(Client& client, const std::string& text) {
    const std::string clean = protocol::cleanChat(text);
    if (!world_ || clean.empty()) return;
    std::printf("%s: %s\n", client.name.c_str(), clean.c_str());
    report("ChatSent", client.name, 1);  // that someone chatted, never what they said
    tellEveryone(client.name, clean);
}

void Server::tellEveryone(const std::string& from, const std::string& text) {
    for (const auto& [id, client] : clients_) {
        if (!client.name.empty()) sendTo(client, protocol::ChatLine{from, text});
    }
}

void Server::tickWorld() {
    if (!world_) return;
    const protocol::TickMessage message{world_->nextTick()};
    for (const int memberId : world_->memberIds()) {
        if (const Client* client = clientOf(memberId)) sendTo(*client, message);
    }
    for (const auto& [id, client] : clients_) {
        if (client.observing) sendTo(client, message);
    }
    if (observer_) {
        // The same tick the players got, on the server's copy of the world: what they did, for the stats.
        const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
        reporter_->add(observer_->apply(message.input, now));
        reporter_->setOnline(playersOnline());
    }
    const int age = static_cast<int>(world_->history().size());
    constexpr int kWarningTicks = 60 * 60;  // a minute before the reset
    if (!resetWarned_ && age >= resetAfterTicks_ - kWarningTicks) {
        resetWarned_ = true;
        tellEveryone("", "The world resets in 1 minute!");
    }
    if (age >= resetAfterTicks_) resetWorld();
}

Server::Client* Server::clientOf(int clientId) {
    const auto it = clients_.find(clientId);
    return it == clients_.end() ? nullptr : &it->second;
}

int Server::playersOnline() const {
    return static_cast<int>(std::count_if(clients_.begin(), clients_.end(),
                                          [](const auto& entry) { return !entry.second.name.empty(); }));
}

void Server::observe(Client& client) {
    if (observersOnline() >= protocol::kMaxObservers) {
        sendTo(client, protocol::ErrorMessage{"Too many observers"});
        if (const std::shared_ptr<ix::WebSocket> socket = client.socket.lock()) socket->close();
        return;
    }
    client.observing = true;
    std::printf("Connection %d is an observer\n", client.id);
    if (world_) {
        sendTo(client, protocol::Joined{world_->seed(), world_->history()});
    } else {
        sendTo(client, protocol::Joined{});  // no world: seed 0, nothing to replay; the next one comes when it starts
    }
}

void Server::sendWorldToObservers() {
    for (const auto& [id, client] : clients_) {
        if (client.observing) sendTo(client, protocol::Joined{world_->seed(), world_->history()});
    }
}

void Server::answerPings() {
    for (auto& [id, client] : clients_) {
        if (client.pendingPing) sendTo(client, protocol::ProbePong{*std::exchange(client.pendingPing, std::nullopt)});
    }
}

int Server::observersOnline() const {
    return static_cast<int>(std::count_if(clients_.begin(), clients_.end(),
                                          [](const auto& entry) { return entry.second.observing; }));
}

bool Server::nameInUse(const std::string& name) const {
    return std::any_of(clients_.begin(), clients_.end(), [&](const auto& entry) { return entry.second.name == name; });
}
