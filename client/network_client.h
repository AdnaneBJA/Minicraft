#pragma once

#include "protocol.h"
#include "tick_input.h"

#include <enet/enet.h>

#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

// The game's connection to a minicraft-server. poll() (once a frame) sends and receives; what arrived waits in the
// queues below until the game takes it.
class NetworkClient {
public:
    enum class State { Offline, Connecting, Online };

    // Starts connecting to "host" or "host:port". False if the address can't be resolved.
    bool connect(const std::string& address, const std::string& playerName);
    void disconnect();
    // Handles everything the network delivered since the last call. Call once a frame.
    void poll();

    State state() const { return state_; }
    // The id the server gave this player (Welcome); 0 until then.
    int playerId() const { return playerId_; }
    // True once after the connection failed or dropped.
    bool takeConnectionLost() { return std::exchange(connectionLost_, false); }

    // --- What arrived.
    // The latest lobby list, when a new one came in.
    std::optional<std::vector<protocol::LobbyInfo>> takeLobbies() { return std::exchange(lobbies_, std::nullopt); }
    // Entering a lobby: its seed and the history to replay.
    std::optional<protocol::Joined> takeJoined() { return std::exchange(joined_, std::nullopt); }
    // The lobby's ticks, in order, waiting to be simulated.
    std::deque<TickInput>& ticks() { return ticks_; }
    std::size_t ticksWaiting() const { return ticks_.size(); }
    std::vector<protocol::ChatLine> takeChat() { return std::exchange(chat_, {}); }
    std::optional<std::string> takeError() { return std::exchange(error_, std::nullopt); }

    // --- What to send.
    void createLobby();
    void joinLobby(int lobbyId);
    void leaveLobby();
    void sendInput(const PlayerInput& input);
    void sendCommand(const PlayerCommand& command);
    void sendChat(const std::string& text);
    void sendStateHash(int tick, std::uint64_t hash);

private:
    void handleMessage(std::span<const std::uint8_t> bytes);
    template <typename Message>
    void send(const Message& message);

    std::unique_ptr<ENetHost, void (*)(ENetHost*)> host_{nullptr, enet_host_destroy};
    ENetPeer* server_ = nullptr;
    State state_ = State::Offline;
    std::string playerName_;
    int playerId_ = 0;
    bool connectionLost_ = false;
    std::optional<std::vector<protocol::LobbyInfo>> lobbies_;
    std::optional<protocol::Joined> joined_;
    std::deque<TickInput> ticks_;
    std::vector<protocol::ChatLine> chat_;
    std::optional<std::string> error_;
};
