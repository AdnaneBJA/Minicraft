#include "dropped_items.h"

#include "camera.h"
#include "tile_map.h"

#include <cmath>

namespace {

constexpr int kPickupDelay = 30;  // ticks before an item can be picked up, so it visibly pops out first
constexpr int kBlinkTicks = 120;  // the last 2 seconds of its life, the item blinks
constexpr float kRadius = 2.0f;   // items are 4x4 for touching (Minicraft: xr = yr = 2)

int tileOf(float worldValue) {
    return static_cast<int>(std::floor(worldValue / static_cast<float>(TileMap::kTileSize)));
}

}  // namespace

void DroppedItems::spawn(ItemType type, int count, float centerX, float centerY) {
    const int tx = tileOf(centerX);
    const int ty = tileOf(centerY);
    for (int i = 0; i < count; ++i) {
        // Random offset of up to 5 px, re-rolled until it stays in the same tile.
        float x = 0.0f;
        float y = 0.0f;
        do {
            x = centerX + static_cast<float>(SDL_rand(11)) - 5.0f;
            y = centerY + static_cast<float>(SDL_rand(11)) - 5.0f;
        } while (tileOf(x) != tx || tileOf(y) != ty);
        const int lifetime = 600 + static_cast<int>(SDL_rand(70));  // ~10 s, like Minicraft
        items_.push_back({type, Bounce::toss(x, y, 1.0f), 0, lifetime});
    }
}

void DroppedItems::update(float dt, const TileMap& map, const SDL_FRect& pickupBox, Inventory& inventory) {
    tickAccumulator_ += dt;
    while (tickAccumulator_ >= Bounce::kTick) {
        tickAccumulator_ -= Bounce::kTick;
        tick(map, pickupBox, inventory);
    }
}

void DroppedItems::tick(const TileMap& map, const SDL_FRect& pickupBox, Inventory& inventory) {
    for (Item& item : items_) {
        ++item.age;
        // Move, but don't slide into solid tiles (the player couldn't reach the item there).
        const float oldX = item.motion.x;
        const float oldY = item.motion.y;
        item.motion.tick();
        if (map.isSolidAt(tileOf(item.motion.x), tileOf(oldY))) item.motion.x = oldX;
        if (map.isSolidAt(tileOf(item.motion.x), tileOf(item.motion.y))) item.motion.y = oldY;
    }

    std::erase_if(items_, [&](const Item& item) {
        if (item.age >= item.lifetime) return true;
        if (item.age <= kPickupDelay) return false;
        const SDL_FRect box{item.motion.x - kRadius, item.motion.y - kRadius, kRadius * 2.0f, kRadius * 2.0f};
        if (SDL_HasRectIntersectionFloat(&box, &pickupBox)) {
            inventory.add(item.type);
            return true;
        }
        return false;
    });
}

void DroppedItems::draw(SDL_Renderer* renderer, const Camera& camera, const ItemIcons& icons) const {
    for (const Item& item : items_) {
        // Blink before despawning: hidden every other 6-tick slice.
        if (item.age >= item.lifetime - kBlinkTicks && (item.age / 6) % 2 == 0) continue;
        // Whole world pixels, like Minicraft's int positions.
        const float x = std::floor(item.motion.x) - 4.0f - camera.x();
        const float y = std::floor(item.motion.y) - 4.0f - camera.y();
        icons.drawShadow(renderer, item.type, x, y);
        icons.draw(renderer, item.type, x, y - std::floor(item.motion.z));
    }
}
