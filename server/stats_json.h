#pragma once

#include <cstdint>
#include <string>
#include <vector>

// One thing that happened, as the stats service receives it: already named (players, items, mobs), with an id that
// makes it count once however many times it's sent.
struct StatEvent {
    std::string id;          // "<seed>-<tick>-<i>" for the world's events, "<seed>-server-<n>" for the server's
    std::string type;        // TileBroken, ItemCollected, ItemCrafted, MobKilled, PlayerKilled, LevelReached,
                             // BossDefeated, PlayerJoined, PlayerLeft, WorldStarted, ChatSent
    std::int64_t at = 0;     // when, in Unix milliseconds
    std::string player;      // who did it (may be empty)
    std::string subject;     // the item, tile or mob, or the killer's name in PvP
    std::string killerKind;  // PlayerKilled: "player", "mob" or "environment"
    int count = 0;           // how many, the mob's level, seconds alive or played, or the level reached
    int icon = -1;           // items: their place in items.png
};

// How the game server is doing, sent with every batch (the stats service turns it into metrics).
struct ServerHealth {
    int connections = 0;            // open WebSocket connections: players, observers, and ones still saying Hello
    int observers = 0;              // monitoring probes watching
    std::int64_t ticks = 0;         // ticks sent since the server started
    int historyTicks = 0;           // the current world's age in ticks (0: no world)
    std::int64_t historyBytes = 0;  // what a player joining now downloads (Joined)
    std::int64_t rssBytes = 0;      // the server's resident memory
    double cpuSeconds = 0;          // the server's CPU time so far
};

// A batch as JSON: {"online":N,"events":[...],"health":{...}}. `at` (Unix ms) is when the batch was cut, `backlog`
// how many batches were waiting to go out.
std::string toJson(int online, const std::vector<StatEvent>& events, std::int64_t at, const ServerHealth& health,
                   std::size_t backlog);
