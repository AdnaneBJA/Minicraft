#include "client_socket.h"

#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>

#include <mutex>
#include <utility>

namespace {

constexpr int kHandshakeTimeoutSeconds = 5;

// IXWebSocket runs the connection on its own thread: what it reports waits in a queue until poll().
class NativeClientSocket final : public ClientSocket {
public:
    NativeClientSocket() {
        static const bool netReady = ix::initNetSystem();  // Windows sockets; once per process
        (void)netReady;
    }
    ~NativeClientSocket() override { socket_.stop(); }

    void open(const std::string& url) override {
        socket_.setUrl(url);
        socket_.disableAutomaticReconnection();
        socket_.setHandshakeTimeout(kHandshakeTimeoutSeconds);
        socket_.setOnMessageCallback([this](const ix::WebSocketMessagePtr& message) {
            switch (message->type) {
                case ix::WebSocketMessageType::Open: push({SocketEvent::Kind::Opened}); break;
                case ix::WebSocketMessageType::Message:
                    push({SocketEvent::Kind::Message, {message->str.begin(), message->str.end()}});
                    break;
                case ix::WebSocketMessageType::Close:
                case ix::WebSocketMessageType::Error: push({SocketEvent::Kind::Closed}); break;
                default: break;  // ping, pong, fragments
            }
        });
        socket_.start();
    }

    void send(std::span<const std::uint8_t> bytes) override {
        socket_.sendBinary(std::string(bytes.begin(), bytes.end()));
    }

    std::vector<SocketEvent> poll() override {
        const std::lock_guard lock(mutex_);
        return std::exchange(events_, {});
    }

private:
    void push(SocketEvent event) {
        const std::lock_guard lock(mutex_);
        events_.push_back(std::move(event));
    }

    std::mutex mutex_;
    std::vector<SocketEvent> events_;
    ix::WebSocket socket_;  // last: stopped (its thread joined) before the queue goes away
};

}  // namespace

std::unique_ptr<ClientSocket> makeClientSocket() { return std::make_unique<NativeClientSocket>(); }
