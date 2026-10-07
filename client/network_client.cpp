#include "network_client.h"

#include <string_view>

namespace {

// No answer this long after connecting: give up, as if the connection had dropped.
constexpr auto kConnectTimeout = std::chrono::seconds(5);
// In a world, nothing from the server for this long (no ticks): the connection died without closing.
constexpr auto kSilenceTimeout = std::chrono::seconds(10);

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
    inWorld_ = false;
    playerId_ = 0;
    ticks_.clear();
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
            case SocketEvent::Kind::Message:
                lastHeard_ = std::chrono::steady_clock::now();
                handleMessage(event.bytes);
                break;
            case SocketEvent::Kind::Closed:
                disconnect();
                connectionLost_ = true;
                return;
        }
    }
    const auto now = std::chrono::steady_clock::now();
    const bool connectTimedOut = state_ == State::Connecting && now - connectStarted_ > kConnectTimeout;
    const bool serverWentSilent = inWorld_ && now - lastHeard_ > kSilenceTimeout;
    if (connectTimedOut || serverWentSilent) {
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
        case MessageType::Joined:
            if (auto joined = protocol::decode<protocol::Joined>(bytes)) {
                ticks_.clear();
                joined_ = std::move(*joined);
                inWorld_ = true;
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

void NetworkClient::sendInput(const PlayerInput& input) { send(protocol::InputMessage{input}); }
void NetworkClient::sendCommand(const PlayerCommand& command) { send(protocol::CommandMessage{command}); }
void NetworkClient::sendChat(const std::string& text) { send(protocol::ChatMessage{text}); }
void NetworkClient::sendStateHash(int tick, std::uint64_t hash) { send(protocol::StateHashMessage{tick, hash}); }
