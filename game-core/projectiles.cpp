#include "projectiles.h"

#include "collision.h"
#include "events.h"
#include "mobs.h"
#include "player.h"
#include "random.h"
#include "tile_map.h"

#include <algorithm>
#include <cmath>

namespace {

// Arrows fly over water, lava and holes but stop at anything solid (Arrow.tick's mayPass / connectsToFluid).
bool stopsArrows(const TileMap& map, float x, float y) {
    const int tx = collision::tileIndex(x);
    const int ty = collision::tileIndex(y);
    if (!map.inBounds(tx, ty)) return true;
    const Tile tile = map.tileAt(tx, ty);
    return tile != Tile::InfiniteFall && isSolid(tile, map.dataAt(tx, ty));
}

}  // namespace

void Projectiles::shootArrow(float x, float y, Point direction, int damage, int shooterId) {
    // Arrow: faster from better bows.
    const int speed = damage > 3 ? 8 : 7;
    arrows_.push_back({x, y, direction, damage, speed, shooterId});
}

void Projectiles::addSpark(float x, float y, float vx, float vy, Random& rng) {
    sparks_.push_back({x, y, vx, vy, 0, 360 + rng.nextInt(30)});
}

void Projectiles::tick(const TileMap& map, std::span<Player* const> players, Mobs& mobs, Events& events,
                       Random& rng) {
    std::erase_if(arrows_, [&](Arrow& arrow) {
        arrow.x += static_cast<float>(arrow.direction.x * arrow.speed);
        arrow.y += static_cast<float>(arrow.direction.y * arrow.speed);
        if (stopsArrows(map, arrow.x, arrow.y)) return true;
        // Arrow.tick: 3 extra damage against mobs, and a 2 in 11 chance of one more.
        const int bonus = rng.nextInt(11) < 9 ? 0 : 1;
        const Rect tip{arrow.x - 1.0f, arrow.y - 1.0f, 2.0f, 2.0f};
        const bool fromPlayer = arrow.shooterId >= 0;
        if (fromPlayer && mobs.hit(tip, arrow.damage + 3 + bonus, arrow.direction, events)) return true;
        // Players: hit by skeletons' arrows, and by each other's (PvP), but never by their own.
        for (Player* player : players) {
            if (player->id() == arrow.shooterId || !intersects(tip, player->hitbox())) continue;
            player->takeHit(std::max(1, arrow.damage + bonus), arrow.direction.x, arrow.direction.y, events);
            return true;
        }
        return false;
    });
    std::erase_if(sparks_, [&](Spark& spark) {
        if (++spark.age >= spark.lifetime) return true;
        spark.x += spark.vx;
        spark.y += spark.vy;
        // Spark.tick: touching a player hurts them for 1.
        for (Player* player : players) {
            const Rect box = player->hitbox();
            if (spark.x >= box.x && spark.x < box.x + box.w && spark.y >= box.y && spark.y < box.y + box.h) {
                player->takeHit(1, 0, 0, events);
            }
        }
        return false;
    });
}

void Projectiles::clear() {
    arrows_.clear();
    sparks_.clear();
}
