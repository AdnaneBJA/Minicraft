#pragma once

#include "lobby.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <string>

namespace ix {
class WebSocket;
class WebSocketServer;
}  // namespace ix

// minicraft-server: accepts players over WebSockets and runs the one world they all play in. Its 60 Hz tick gathers
// what every player did and sends it to all of them. Chat goes through here too.
//
// The world starts when the first player arrives and ends when the last one leaves. A world that has run for
// resetAfterTicks is replaced by a fresh one for everyone, so joining (which replays the whole history) stays quick.
//
// IXWebSocket serves every connection on its own thread. Those threads only queue what happened (NetEvent); run()
// handles the queue on its own thread, so everything else here is single-threaded.
class Server {
public:
    static constexpr int kDefaultResetAfterTicks = 60 * 60 * 60 * 3;  // 3 hours

    explicit Server(int resetAfterTicks = kDefaultResetAfterTicks);
    ~Server();
    // Listens on `port` (all network interfaces). False if the port can't be opened.
    bool start(std::uint16_t port);
    // Runs until stop() is called.
    void run();
    // Makes run() return (within a tick). Safe from any thread and from a signal handler. The connections close
    // when the Server is destroyed.
    void stop();

private:
    // One connected player.
    struct Client {
        std::weak_ptr<ix::WebSocket> socket;  // gone once the connection has closed
        int id = 0;
        std::string name;  // empty until their Hello is accepted; from then on they're in the world
    };

    // What a connection's thread saw, waiting for run().
    struct NetEvent {
        enum class Kind { Connected, Message, Disconnected };
        Kind kind = Kind::Connected;
        int clientId = 0;
        std::weak_ptr<ix::WebSocket> socket;  // Connected
        std::string bytes;                    // Message
    };

    void push(NetEvent event);
    void handle(NetEvent& event);
    void handleMessage(Client& client, std::span<const std::uint8_t> bytes);
    template <typename Message>
    void sendTo(const Client& client, const Message& message);
    void disconnect(Client& client);

    // A player whose name was accepted enters the world (starting it if nobody is playing).
    void enterWorld(Client& client);
    void chat(Client& client, const std::string& text);
    // Everyone in the world gets a line of chat (from "" = a note from the server).
    void tellEveryone(const std::string& from, const std::string& text);
    // The world moves on by one tick, and every player hears about it.
    void tickWorld();
    // A fresh world, with everyone in it.
    void resetWorld();
    static std::unique_ptr<Lobby> newWorld();

    Client* clientOf(int clientId);
    bool nameInUse(const std::string& name) const;

    std::unique_ptr<ix::WebSocketServer> socketServer_;
    std::mutex eventsMutex_;
    std::condition_variable eventsReady_;
    std::deque<NetEvent> events_;
    std::atomic<bool> running_{false};
    std::atomic<int> nextClientId_{1};

    std::map<int, Client> clients_;  // by client id
    std::unique_ptr<Lobby> world_;   // null while nobody is playing
    int resetAfterTicks_;
    bool resetWarned_ = false;
};
