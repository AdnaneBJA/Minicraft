#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

// The client's WebSocket connection to the server, the same on every platform: in the browser it's the browser's
// own WebSocket (client_socket_web.cpp), elsewhere IXWebSocket (client_socket_native.cpp). Each protocol message
// travels as one binary WebSocket message, reliable and in order.

// Something that happened on the connection.
struct SocketEvent {
    enum class Kind { Opened, Message, Closed };
    Kind kind = Kind::Opened;
    std::vector<std::uint8_t> bytes;  // Message
};

class ClientSocket {
public:
    virtual ~ClientSocket() = default;  // closes the connection
    // Starts connecting to a "ws://" or "wss://" URL. Opened or Closed follows.
    virtual void open(const std::string& url) = 0;
    // Sends one message. Only after Opened.
    virtual void send(std::span<const std::uint8_t> bytes) = 0;
    // What happened since the last call, in order. Call from the game's thread (once a frame).
    virtual std::vector<SocketEvent> poll() = 0;
};

std::unique_ptr<ClientSocket> makeClientSocket();
