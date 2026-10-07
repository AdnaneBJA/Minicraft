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

// minicraft-server: accepts players over WebSockets, lets them create and join lobbies, and runs every lobby's
// 60 Hz tick: gathering what its players did and sending it to all of them. Chat goes through here too.
//
// IXWebSocket serves every connection on its own thread. Those threads only queue what happened (NetEvent); run()
// handles the queue on its own thread, so everything else here is single-threaded.
class Server {
public:
    Server();
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
        std::string name;  // empty until their Hello arrives
        int lobbyId = 0;   // 0 = not in a lobby (looking at the lobby list)
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

    void createLobby(Client& client);
    void joinLobby(Client& client, int lobbyId);
    void leaveLobby(Client& client);
    void chat(Client& client, const std::string& text);
    // Everyone in a lobby gets a line of chat (from "" = a note from the server).
    void tellLobby(const Lobby& lobby, const std::string& from, const std::string& text);
    // Everyone not in a lobby gets the current lobby list.
    void sendLobbyList(Client* only = nullptr);
    // Every lobby moves on by one tick, and every member hears about it.
    void tickLobbies();

    Client* clientOf(int clientId);
    Lobby* lobbyOf(const Client& client);

    std::unique_ptr<ix::WebSocketServer> socketServer_;
    std::mutex eventsMutex_;
    std::condition_variable eventsReady_;
    std::deque<NetEvent> events_;
    std::atomic<bool> running_{false};
    std::atomic<int> nextClientId_{1};

    std::map<int, Client> clients_;  // by client id
    std::map<int, std::unique_ptr<Lobby>> lobbies_;
    int nextLobbyId_ = 1;
};
