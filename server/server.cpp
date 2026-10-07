#include "server.h"

#include "protocol.h"

#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>
#include <ixwebsocket/IXWebSocketServer.h>

#include <chrono>
#include <cstdio>
#include <random>

namespace {

using Clock = std::chrono::steady_clock;
constexpr auto kTickLength = std::chrono::microseconds(1'000'000 / 60);
constexpr int kListenBacklog = 16;

}  // namespace

Server::Server() = default;

Server::~Server() {
    stop();
    if (socketServer_) socketServer_->stop();  // closes every connection and joins their threads
}

bool Server::start(std::uint16_t port) {
    ix::initNetSystem();
    socketServer_ = std::make_unique<ix::WebSocketServer>(port, "0.0.0.0", kListenBacklog,
                                                          static_cast<std::size_t>(protocol::kMaxPlayers));
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
            tickLobbies();
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
    // Until a player has said who they are, Hello is the only thing they can do.
    if (client.name.empty() && *type != MessageType::Hello) return;
    switch (*type) {
        case MessageType::Hello:
            if (const auto hello = protocol::decode<protocol::Hello>(bytes); hello && client.name.empty()) {
                if (!protocol::isValidName(hello->name)) {
                    sendTo(client, protocol::ErrorMessage{"Invalid name"});
                    return;
                }
                client.name = hello->name;
                std::printf("Player %d is %s\n", client.id, client.name.c_str());
                sendLobbyList(&client);
            }
            break;
        case MessageType::CreateLobby:
            if (protocol::decode<protocol::CreateLobby>(bytes)) createLobby(client);
            break;
        case MessageType::JoinLobby:
            if (const auto join = protocol::decode<protocol::JoinLobby>(bytes)) joinLobby(client, join->lobbyId);
            break;
        case MessageType::LeaveLobby:
            if (protocol::decode<protocol::LeaveLobby>(bytes)) leaveLobby(client);
            break;
        case MessageType::Input:
            if (const auto input = protocol::decode<protocol::InputMessage>(bytes)) {
                if (Lobby* lobby = lobbyOf(client)) lobby->setInput(client.id, input->input);
            }
            break;
        case MessageType::Command:
            if (const auto command = protocol::decode<protocol::CommandMessage>(bytes)) {
                if (Lobby* lobby = lobbyOf(client)) lobby->addCommand(client.id, command->command);
            }
            break;
        case MessageType::Chat:
            if (const auto chatMessage = protocol::decode<protocol::ChatMessage>(bytes)) chat(client, chatMessage->text);
            break;
        case MessageType::StateHash:
            if (const auto report = protocol::decode<protocol::StateHashMessage>(bytes)) {
                Lobby* lobby = lobbyOf(client);
                if (lobby && lobby->reportHash(client.id, report->tick, report->hash)) {
                    std::printf("Lobby %d went out of sync at tick %d\n", lobby->id(), report->tick);
                    tellLobby(*lobby, "", "Desync detected at tick " + std::to_string(report->tick) + "!");
                }
            }
            break;
        default: break;  // server -> client messages: ignore
    }
}

void Server::disconnect(Client& client) {
    std::printf("Player %d (%s) disconnected\n", client.id, client.name.c_str());
    leaveLobby(client);
    clients_.erase(client.id);
}

// ---------------------------------------------------------------------------------------------------------------
// Lobbies

void Server::createLobby(Client& client) {
    if (client.lobbyId != 0) return;
    std::random_device random;
    auto lobby = std::make_unique<Lobby>(nextLobbyId_++, client.name + "'s world", random());
    const int id = lobby->id();
    std::printf("%s created lobby %d (seed %u)\n", client.name.c_str(), id, lobby->seed());
    lobbies_[id] = std::move(lobby);
    joinLobby(client, id);
}

void Server::joinLobby(Client& client, int lobbyId) {
    if (client.lobbyId != 0) return;  // already playing
    const auto it = lobbies_.find(lobbyId);
    if (it == lobbies_.end()) {
        sendTo(client, protocol::ErrorMessage{"That lobby is gone"});
        return;
    }
    Lobby& lobby = *it->second;
    // The newcomer gets the world's seed and every tick so far; their own Join comes with the next tick.
    sendTo(client, protocol::Joined{lobby.id(), lobby.name(), lobby.seed(), lobby.history()});
    lobby.addMember(client.id, client.name);
    client.lobbyId = lobby.id();
    tellLobby(lobby, "", client.name + " joined the game");
    sendLobbyList();
}

void Server::leaveLobby(Client& client) {
    Lobby* lobby = lobbyOf(client);
    if (!lobby) return;
    lobby->removeMember(client.id);
    client.lobbyId = 0;
    if (lobby->empty()) {
        std::printf("Lobby %d closed\n", lobby->id());
        lobbies_.erase(lobby->id());
    } else {
        tellLobby(*lobby, "", client.name + " left the game");
    }
    sendLobbyList();
}

void Server::chat(Client& client, const std::string& text) {
    const Lobby* lobby = lobbyOf(client);
    const std::string clean = protocol::cleanChat(text);
    if (!lobby || clean.empty()) return;
    std::printf("[%s] %s: %s\n", lobby->name().c_str(), client.name.c_str(), clean.c_str());
    tellLobby(*lobby, client.name, clean);
}

void Server::tellLobby(const Lobby& lobby, const std::string& from, const std::string& text) {
    for (const auto& [id, client] : clients_) {
        if (client.lobbyId == lobby.id()) sendTo(client, protocol::ChatLine{from, text});
    }
}

void Server::sendLobbyList(Client* only) {
    protocol::LobbyList list;
    for (const auto& [id, lobby] : lobbies_) list.lobbies.push_back({id, lobby->name(), lobby->size()});
    for (const auto& [id, client] : clients_) {
        const bool browsing = !client.name.empty() && client.lobbyId == 0;
        if (browsing && (!only || only == &client)) sendTo(client, list);
    }
}

void Server::tickLobbies() {
    for (auto& [id, lobby] : lobbies_) {
        const protocol::TickMessage message{lobby->nextTick()};
        for (const int memberId : lobby->memberIds()) {
            if (const Client* client = clientOf(memberId)) sendTo(*client, message);
        }
    }
}

Server::Client* Server::clientOf(int clientId) {
    const auto it = clients_.find(clientId);
    return it == clients_.end() ? nullptr : &it->second;
}

Lobby* Server::lobbyOf(const Client& client) {
    const auto it = lobbies_.find(client.lobbyId);
    return it == lobbies_.end() ? nullptr : it->second.get();
}
