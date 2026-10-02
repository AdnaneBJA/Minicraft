#include "server.h"

#include "enet_util.h"
#include "protocol.h"

#include <chrono>
#include <cstdio>
#include <random>

namespace {

using Clock = std::chrono::steady_clock;
constexpr auto kTickLength = std::chrono::microseconds(1'000'000 / 60);

}  // namespace

bool Server::start(std::uint16_t port) {
    ENetAddress address{};
    address.host = ENET_HOST_ANY;
    address.port = port;
    // Up to kMaxPlayers clients, one channel each, no bandwidth limits.
    host_.reset(enet_host_create(&address, protocol::kMaxPlayers, 1, 0, 0));
    if (!host_) {
        std::printf("Could not listen on port %u (is another server running?)\n", port);
        return false;
    }
    std::printf("minicraft-server listening on port %u\n", port);
    return true;
}

void Server::run() {
    // A fixed 60 Hz clock for the lobbies; in between, wait for network events.
    auto nextTick = Clock::now();
    while (true) {
        const auto now = Clock::now();
        if (now >= nextTick) {
            tickLobbies();
            nextTick += kTickLength;
            if (now - nextTick > std::chrono::seconds(1)) nextTick = now;  // fell far behind: don't try to catch up
            continue;
        }
        const auto wait = std::chrono::duration_cast<std::chrono::milliseconds>(nextTick - now);
        ENetEvent event;
        if (enet_host_service(host_.get(), &event, static_cast<enet_uint32>(wait.count())) > 0) handle(event);
    }
}

void Server::handle(const ENetEvent& event) {
    switch (event.type) {
        case ENET_EVENT_TYPE_CONNECT: {
            Client client{.peer = event.peer, .id = nextClientId_++};
            clients_[event.peer] = client;
            std::printf("Player %d connected\n", client.id);
            sendMessage(event.peer, protocol::Welcome{client.id});
            break;
        }
        case ENET_EVENT_TYPE_RECEIVE:
            if (Client* client = clientOf(event.peer)) {
                handleMessage(*client, {event.packet->data, event.packet->dataLength});
            }
            enet_packet_destroy(event.packet);
            break;
        case ENET_EVENT_TYPE_DISCONNECT:
            if (Client* client = clientOf(event.peer)) disconnect(*client);
            break;
        case ENET_EVENT_TYPE_NONE: break;
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
                    sendMessage(client.peer, protocol::ErrorMessage{"Invalid name"});
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
    clients_.erase(client.peer);
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
        sendMessage(client.peer, protocol::ErrorMessage{"That lobby is gone"});
        return;
    }
    Lobby& lobby = *it->second;
    // The newcomer gets the world's seed and every tick so far; their own Join comes with the next tick.
    sendMessage(client.peer, protocol::Joined{lobby.id(), lobby.name(), lobby.seed(), lobby.history()});
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
    for (auto& [peer, client] : clients_) {
        if (client.lobbyId == lobby.id()) sendMessage(peer, protocol::ChatLine{from, text});
    }
}

void Server::sendLobbyList(Client* only) {
    protocol::LobbyList list;
    for (const auto& [id, lobby] : lobbies_) list.lobbies.push_back({id, lobby->name(), lobby->size()});
    for (auto& [peer, client] : clients_) {
        const bool browsing = !client.name.empty() && client.lobbyId == 0;
        if (browsing && (!only || only == &client)) sendMessage(peer, list);
    }
}

void Server::tickLobbies() {
    for (auto& [id, lobby] : lobbies_) {
        const protocol::TickMessage message{lobby->nextTick()};
        for (const int memberId : lobby->memberIds()) {
            for (auto& [peer, client] : clients_) {
                if (client.id == memberId) sendMessage(peer, message);
            }
        }
    }
    enet_host_flush(host_.get());
}

Server::Client* Server::clientOf(ENetPeer* peer) {
    const auto it = clients_.find(peer);
    return it == clients_.end() ? nullptr : &it->second;
}

Lobby* Server::lobbyOf(const Client& client) {
    const auto it = lobbies_.find(client.lobbyId);
    return it == lobbies_.end() ? nullptr : it->second.get();
}
