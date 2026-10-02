#pragma once

#include "lobby.h"

#include <enet/enet.h>

#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>

// minicraft-server: accepts players over ENet, lets them create and join lobbies, and runs every lobby's 60 Hz
// tick: gathering what its players did and sending it to all of them. Chat goes through here too.
class Server {
public:
    // Listens on `port` (all network interfaces). False if the port can't be opened.
    bool start(std::uint16_t port);
    // Runs until the process is stopped.
    void run();

private:
    // One connected player.
    struct Client {
        ENetPeer* peer = nullptr;
        int id = 0;
        std::string name;  // empty until their Hello arrives
        int lobbyId = 0;   // 0 = not in a lobby (looking at the lobby list)
    };

    void handle(const ENetEvent& event);
    void handleMessage(Client& client, std::span<const std::uint8_t> bytes);
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

    Client* clientOf(ENetPeer* peer);
    Lobby* lobbyOf(const Client& client);

    std::unique_ptr<ENetHost, void (*)(ENetHost*)> host_{nullptr, enet_host_destroy};
    std::map<ENetPeer*, Client> clients_;
    std::map<int, std::unique_ptr<Lobby>> lobbies_;
    int nextClientId_ = 1;
    int nextLobbyId_ = 1;
};
