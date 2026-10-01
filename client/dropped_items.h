#pragma once

#include "bounce.h"
#include "items.h"

#include <SDL3/SDL.h>

#include <vector>

class Camera;
class TileMap;

// Items lying on the ground (Minicraft's ItemEntity): tossed out when a tile breaks, picked up on touch.
class DroppedItems {
public:
    // Drops `count` items around a world point, each landing somewhere inside the same tile (like Level.dropItem).
    // `durability`: for a tool, its uses left (-1 = a new tool at full durability).
    void spawn(ItemType type, int count, float centerX, float centerY, int durability = -1);

    // Advances physics in 60 Hz ticks. Items touching `pickupBox` (the player's hitbox) go into `inventory`.
    void update(float dt, const TileMap& map, const SDL_FRect& pickupBox, Inventory& inventory);
    void draw(SDL_Renderer* renderer, const Camera& camera, const ItemIcons& icons) const;

    void clear() { items_.clear(); }
    std::size_t size() const { return items_.size(); }

private:
    struct Item {
        ItemType type;
        int durability;
        Bounce motion;
        int age;       // ticks since dropped
        int lifetime;  // ticks before it despawns
    };

    void tick(const TileMap& map, const SDL_FRect& pickupBox, Inventory& inventory);

    std::vector<Item> items_;
    float tickAccumulator_ = 0.0f;
};
