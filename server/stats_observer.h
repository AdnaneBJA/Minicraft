#pragma once

#include "simulation.h"
#include "stats_json.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

// The server's own copy of the world, for the stats. It runs every tick the players get, so it sees everything
// they do, exactly once and from the server (a browser can't make it up). Each tick it hands back what happened,
// with player ids turned into names.
class StatsObserver {
public:
    explicit StatsObserver(std::uint32_t seed);

    // A player entered the world under `name`. Names stay known after they leave: their last events still need one.
    void nameJoined(int playerId, const std::string& name) { names_[playerId] = name; }
    std::string nameOf(int playerId) const;

    // Runs one tick; returns the stats it produced. `nowMs`: the time to stamp them with.
    std::vector<StatEvent> apply(const TickInput& tick, std::int64_t nowMs);

    Simulation& simulation() { return sim_; }  // for tests

private:
    StatEvent describe(const GameEvent& event) const;

    std::uint32_t seed_;
    Simulation sim_;
    std::map<int, std::string> names_;
};
