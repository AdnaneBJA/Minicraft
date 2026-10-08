#include "dropped_items.h"

#include "player.h"
#include "random.h"
#include "tile_map.h"

#include <cmath>

namespace {

constexpr int kPickupDelay = 30;  // ticks before an item can be picked up, so it visibly pops out first
constexpr float kRadius = 2.0f;   // items are 4x4 for touching (Minicraft: xr = yr = 2)

int tileOf(float worldValue) {
    return static_cast<int>(std::floor(worldValue / static_cast<float>(TileMap::kTileSize)));
}

}  // namespace

void DroppedItems::spawn(ItemType type, int count, float centerX, float centerY, Random& rng, int durability) {
    if (durability < 0) durability = maxDurability(type);
    const int tx = tileOf(centerX);
    const int ty = tileOf(centerY);
    for (int i = 0; i < count; ++i) {
        // Random offset of up to 5 px, re-rolled until it stays in the same tile.
        float x = 0.0f;
        float y = 0.0f;
        do {
            x = centerX + static_cast<float>(rng.nextInt(11)) - 5.0f;
            y = centerY + static_cast<float>(rng.nextInt(11)) - 5.0f;
        } while (tileOf(x) != tx || tileOf(y) != ty);
        const int lifetime = 600 + rng.nextInt(70);  // ~10 s, like Minicraft
        items_.push_back({type, durability, Bounce::toss(x, y, 1.0f, rng), 0, lifetime});
    }
}

int DroppedItems::tick(const TileMap& map, std::span<Player* const> players, Events& events) {
    int pickedUp = 0;
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
        const Rect box{item.motion.x - kRadius, item.motion.y - kRadius, kRadius * 2.0f, kRadius * 2.0f};
        for (Player* player : players) {
            Inventory& inventory = player->inventory();
            if (intersects(box, player->hitbox()) && inventory.canAdd(item.type)) {
                inventory.add(Inventory::Stack{item.type, 1, item.durability});
                events.push({.kind = GameEvent::Kind::ItemCollected, .value = static_cast<int>(item.type), .count = 1,
                             .player = player->id()});
                ++pickedUp;
                return true;
            }
        }
        return false;
    });
    return pickedUp;
}
