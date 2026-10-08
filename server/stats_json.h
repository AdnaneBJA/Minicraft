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

// A batch as JSON: {"online":N,"events":[...]}.
std::string toJson(int online, const std::vector<StatEvent>& events);
