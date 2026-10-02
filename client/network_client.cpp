#include "network_client.h"

#include "enet_util.h"

#include <cstdlib>

namespace {

constexpr enet_uint32 kConnectTimeoutMs = 5000;

}  // namespace

bool NetworkClient::connect(const std::string& address, const std::string& playerName) {
    disconnect();
    // "host" or "host:port".
    std::string hostName = address;
    int port = protocol::kDefaultPort;
    if (const auto colon = address.rfind(':'); colon != std::string::npos) {
        hostName = address.substr(0, colon);
        port = std::atoi(address.c_str() + colon + 1);
    }
    ENetAddress serverAddress{};
    if (hostName.empty() || port <= 0 || port > 65535 || enet_address_set_host(&serverAddress, hostName.c_str()) != 0) {
        return false;
    }
    serverAddress.port = static_cast<enet_uint16>(port);

    // A client host: no incoming connections, one outgoing peer with one channel.
    host_.reset(enet_host_create(nullptr, 1, 1, 0, 0));
    if (!host_) return false;
    server_ = enet_host_connect(host_.get(), &serverAddress, 1, 0);
    if (!server_) return false;
    enet_peer_timeout(server_, 0, kConnectTimeoutMs, kConnectTimeoutMs);
    playerName_ = playerName;
    state_ = State::Connecting;
    return true;
}

void NetworkClient::disconnect() {
    if (server_) {
        enet_peer_disconnect_now(server_, 0);
        server_ = nullptr;
    }
    host_.reset();
    state_ = State::Offline;
    playerId_ = 0;
    ticks_.clear();
    lobbies_.reset();
    joined_.reset();
}

void NetworkClient::poll() {
    if (!host_) return;
    ENetEvent event;
    while (host_ && enet_host_service(host_.get(), &event, 0) > 0) {
        switch (event.type) {
            case ENET_EVENT_TYPE_CONNECT:
                state_ = State::Online;
                send(protocol::Hello{playerName_});  // the first thing the server wants to hear
                break;
            case ENET_EVENT_TYPE_RECEIVE:
                handleMessage({event.packet->data, event.packet->dataLength});
                enet_packet_destroy(event.packet);
                break;
            case ENET_EVENT_TYPE_DISCONNECT:
                server_ = nullptr;  // ENet already let go of it
                disconnect();
                connectionLost_ = true;
                return;
            case ENET_EVENT_TYPE_NONE: break;
        }
    }
    if (host_) enet_host_flush(host_.get());
}

void NetworkClient::handleMessage(std::span<const std::uint8_t> bytes) {
    using protocol::MessageType;
    const auto type = protocol::typeOf(bytes);
    if (!type) return;
    switch (*type) {
        case MessageType::Welcome:
            if (const auto welcome = protocol::decode<protocol::Welcome>(bytes)) playerId_ = welcome->playerId;
            break;
        case MessageType::LobbyList:
            if (auto list = protocol::decode<protocol::LobbyList>(bytes)) lobbies_ = std::move(list->lobbies);
            break;
        case MessageType::Joined:
            if (auto joined = protocol::decode<protocol::Joined>(bytes)) {
                ticks_.clear();
                joined_ = std::move(*joined);
            }
            break;
        case MessageType::Tick:
            if (auto tick = protocol::decode<protocol::TickMessage>(bytes)) ticks_.push_back(std::move(tick->input));
            break;
        case MessageType::ChatLine:
            if (auto line = protocol::decode<protocol::ChatLine>(bytes)) chat_.push_back(std::move(*line));
            break;
        case MessageType::Error:
            if (auto error = protocol::decode<protocol::ErrorMessage>(bytes)) error_ = std::move(error->text);
            break;
        default: break;  // client -> server messages: ignore
    }
}

template <typename Message>
void NetworkClient::send(const Message& message) {
    if (server_ && state_ == State::Online) sendMessage(server_, message);
}

void NetworkClient::createLobby() { send(protocol::CreateLobby{}); }
void NetworkClient::joinLobby(int lobbyId) { send(protocol::JoinLobby{lobbyId}); }
void NetworkClient::leaveLobby() {
    send(protocol::LeaveLobby{});
    ticks_.clear();
}
void NetworkClient::sendInput(const PlayerInput& input) { send(protocol::InputMessage{input}); }
void NetworkClient::sendCommand(const PlayerCommand& command) { send(protocol::CommandMessage{command}); }
void NetworkClient::sendChat(const std::string& text) { send(protocol::ChatMessage{text}); }
void NetworkClient::sendStateHash(int tick, std::uint64_t hash) { send(protocol::StateHashMessage{tick, hash}); }
