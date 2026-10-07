#include "network_client.h"

#include <string_view>

namespace {

// No answer this long after connecting: give up, as if the connection had dropped.
constexpr auto kConnectTimeout = std::chrono::seconds(5);

}  // namespace

std::string NetworkClient::urlFor(const std::string& address) {
    if (std::string_view(address).starts_with("ws://") || std::string_view(address).starts_with("wss://")) {
        return address;
    }
    std::string host = address;
    std::string port = std::to_string(protocol::kDefaultPort);
    if (const auto colon = address.rfind(':'); colon != std::string::npos) {
        host = address.substr(0, colon);
        port = address.substr(colon + 1);
    }
    return "ws://" + host + ":" + port;
}

bool NetworkClient::connect(const std::string& address, const std::string& playerName) {
    disconnect();
    if (address.empty()) return false;
    socket_ = makeClientSocket();
    socket_->open(urlFor(address));
    connectStarted_ = std::chrono::steady_clock::now();
    playerName_ = playerName;
    state_ = State::Connecting;
    return true;
}

void NetworkClient::disconnect() {
    socket_.reset();  // closes the connection
    state_ = State::Offline;
    playerId_ = 0;
    ticks_.clear();
    lobbies_.reset();
    joined_.reset();
}

void NetworkClient::poll() {
    if (!socket_) return;
    for (const SocketEvent& event : socket_->poll()) {
        switch (event.kind) {
            case SocketEvent::Kind::Opened:
                state_ = State::Online;
                send(protocol::Hello{playerName_});  // the first thing the server wants to hear
                break;
            case SocketEvent::Kind::Message: handleMessage(event.bytes); break;
            case SocketEvent::Kind::Closed:
                disconnect();
                connectionLost_ = true;
                return;
        }
    }
    if (state_ == State::Connecting && std::chrono::steady_clock::now() - connectStarted_ > kConnectTimeout) {
        disconnect();
        connectionLost_ = true;
    }
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
    if (socket_ && state_ == State::Online) socket_->send(protocol::encode(message));
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
