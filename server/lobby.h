#pragma once

#include "tick_input.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

// One world on the server, and the players in it. The server doesn't simulate the world: it only decides, 60 times
// a second, what every player did during that tick (nextTick), and keeps every tick so far so that a player who
// joins later can replay them.
class Lobby {
public:
    Lobby(int id, std::string name, std::uint32_t seed) : id_(id), name_(std::move(name)), seed_(seed) {}

    int id() const { return id_; }
    const std::string& name() const { return name_; }
    std::uint32_t seed() const { return seed_; }
    const std::vector<TickInput>& history() const { return history_; }
    std::vector<int> memberIds() const;
    bool empty() const { return members_.empty(); }
    int size() const { return static_cast<int>(members_.size()); }

    // A player enters: their Join command goes out with the next tick.
    void addMember(int playerId, const std::string& name);
    // A player leaves: their Leave command goes out with the next tick.
    void removeMember(int playerId);
    bool hasMember(int playerId) const { return members_.contains(playerId); }

    // The keys a player holds now. A press (attackPressed) is remembered until the next tick, so a quick tap isn't
    // lost between two ticks.
    void setInput(int playerId, const PlayerInput& input);
    // Something a player did once; it goes out with the next tick.
    void addCommand(int playerId, const PlayerCommand& command);

    // Builds the next tick from what everyone did since the last one, and adds it to the history.
    const TickInput& nextTick();

    // A client's world fingerprint at a tick. Returns true the first time two players' hashes disagree for the same
    // tick: their simulations went out of sync.
    bool reportHash(int playerId, int tick, std::uint64_t hash);

private:
    struct Member {
        PlayerInput input;                    // the keys held right now
        bool pressed = false;                 // Space went down since the last tick
        std::vector<PlayerCommand> commands;  // done since the last tick
    };

    int id_;
    std::string name_;
    std::uint32_t seed_;
    std::map<int, Member> members_;  // by player id, so every tick lists the players in the same order
    std::vector<int> leaving_;       // players who left since the last tick
    std::vector<TickInput> history_;
    std::map<int, std::map<int, std::uint64_t>> hashes_;  // tick -> player -> hash
    bool desyncReported_ = false;
};
