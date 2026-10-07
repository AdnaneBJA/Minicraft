#include "client_socket.h"

#include <emscripten/websocket.h>

#include <utility>

namespace {

// The browser's own WebSocket. Its callbacks run on the page's main thread (the game's), between frames, so
// they can append to the event list directly.
class WebClientSocket final : public ClientSocket {
public:
    ~WebClientSocket() override {
        if (socket_ <= 0) return;
        // Unhook first: the browser may still report the close, and this object is about to go away.
        emscripten_websocket_set_onopen_callback(socket_, nullptr, nullptr);
        emscripten_websocket_set_onmessage_callback(socket_, nullptr, nullptr);
        emscripten_websocket_set_onclose_callback(socket_, nullptr, nullptr);
        emscripten_websocket_set_onerror_callback(socket_, nullptr, nullptr);
        emscripten_websocket_close(socket_, 1000, "");
        emscripten_websocket_delete(socket_);
    }

    void open(const std::string& url) override {
        EmscriptenWebSocketCreateAttributes attributes;
        emscripten_websocket_init_create_attributes(&attributes);
        attributes.url = url.c_str();
        attributes.protocols = nullptr;
        attributes.createOnMainThread = true;
        socket_ = emscripten_websocket_new(&attributes);
        if (socket_ <= 0) {  // a malformed URL, say
            events_.push_back({SocketEvent::Kind::Closed});
            return;
        }
        emscripten_websocket_set_onopen_callback(socket_, this, [](int, const EmscriptenWebSocketOpenEvent*, void* self) {
            static_cast<WebClientSocket*>(self)->events_.push_back({SocketEvent::Kind::Opened});
            return EM_TRUE;
        });
        emscripten_websocket_set_onmessage_callback(
            socket_, this, [](int, const EmscriptenWebSocketMessageEvent* event, void* self) {
                if (!event->isText) {
                    static_cast<WebClientSocket*>(self)->events_.push_back(
                        {SocketEvent::Kind::Message, {event->data, event->data + event->numBytes}});
                }
                return EM_TRUE;
            });
        emscripten_websocket_set_onclose_callback(socket_, this, [](int, const EmscriptenWebSocketCloseEvent*, void* self) {
            static_cast<WebClientSocket*>(self)->events_.push_back({SocketEvent::Kind::Closed});
            return EM_TRUE;
        });
        // An error is always followed by a close, which reports it.
        emscripten_websocket_set_onerror_callback(socket_, this, [](int, const EmscriptenWebSocketErrorEvent*, void*) {
            return EM_TRUE;
        });
    }

    void send(std::span<const std::uint8_t> bytes) override {
        if (socket_ > 0) {
            emscripten_websocket_send_binary(socket_, const_cast<std::uint8_t*>(bytes.data()),
                                             static_cast<std::uint32_t>(bytes.size()));
        }
    }

    std::vector<SocketEvent> poll() override { return std::exchange(events_, {}); }

private:
    EMSCRIPTEN_WEBSOCKET_T socket_ = 0;
    std::vector<SocketEvent> events_;
};

}  // namespace

std::unique_ptr<ClientSocket> makeClientSocket() { return std::make_unique<WebClientSocket>(); }
