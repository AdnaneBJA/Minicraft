#pragma once

#include "bounce.h"
#include "geometry.h"
#include "events.h"
#include "items.h"

#include <span>
#include <vector>

class Player;
class Random;
class TileMap;

// Items lying on the ground (Minicraft's ItemEntity): tossed out when a tile breaks, picked up on touch.
class DroppedItems {
public:
    static constexpr int kBlinkTicks = 120;  // the last 2 seconds of its life, an item blinks

    struct Item {
        ItemType type;
        int durability;
        Bounce motion;
        int age;       // ticks since dropped
        int lifetime;  // ticks before it despawns
    };

    // Drops `count` items around a world point, each landing somewhere inside the same tile (like Level.dropItem).
    // `durability`: for a tool, its uses left (-1 = a new tool at full durability).
    void spawn(ItemType type, int count, float centerX, float centerY, Random& rng, int durability = -1);

    // One 60 Hz tick of physics. An item touching a player's hitbox goes into their inventory (recorded as an
    // ItemCollected event). Returns how many were picked up.
    int tick(const TileMap& map, std::span<Player* const> players, Events& events);

    void clear() { items_.clear(); }
    std::size_t size() const { return items_.size(); }
    const std::vector<Item>& items() const { return items_; }

private:
    std::vector<Item> items_;
};
