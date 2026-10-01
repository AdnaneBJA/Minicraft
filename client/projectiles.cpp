#include "projectiles.h"

#include "audio.h"
#include "camera.h"
#include "collision.h"
#include "effects.h"
#include "mobs.h"
#include "player.h"
#include "tile_map.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kCell = 8.0f;
constexpr float kSparkColumn = 4.0f;  // the spark sits after the four arrow frames

// Arrows fly over water, lava and holes but stop at anything solid (Arrow.tick's mayPass / connectsToFluid).
bool stopsArrows(const TileMap& map, float x, float y) {
    const int tx = collision::tileIndex(x);
    const int ty = collision::tileIndex(y);
    if (!map.inBounds(tx, ty)) return true;
    const Tile tile = map.tileAt(tx, ty);
    return tile != Tile::InfiniteFall && isSolid(tile, map.dataAt(tx, ty));
}

int arrowFrame(SDL_Point direction) {
    if (direction.x < 0) return 1;
    if (direction.y < 0) return 2;
    if (direction.y > 0) return 3;
    return 0;
}

}  // namespace

void Projectiles::shootArrow(float x, float y, SDL_Point direction, int damage, bool fromPlayer) {
    // Arrow: faster from better bows.
    const int speed = damage > 3 ? 8 : 7;
    arrows_.push_back({x, y, direction, damage, speed, fromPlayer});
}

void Projectiles::addSpark(float x, float y, float vx, float vy) {
    sparks_.push_back({x, y, vx, vy, 0, 360 + static_cast<int>(SDL_rand(30))});
}

void Projectiles::tick(const TileMap& map, Player& player, Mobs& mobs, Effects& effects, Audio& audio) {
    const SDL_FRect playerBox = player.hitbox();
    std::erase_if(arrows_, [&](Arrow& arrow) {
        arrow.x += static_cast<float>(arrow.direction.x * arrow.speed);
        arrow.y += static_cast<float>(arrow.direction.y * arrow.speed);
        if (stopsArrows(map, arrow.x, arrow.y)) return true;
        // Arrow.tick: 3 extra damage against mobs, and a 2 in 11 chance of one more.
        const int bonus = SDL_rand(11) < 9 ? 0 : 1;
        const SDL_FRect tip{arrow.x - 1.0f, arrow.y - 1.0f, 2.0f, 2.0f};
        if (arrow.fromPlayer) {
            return mobs.hit(tip, arrow.damage + 3 + bonus, arrow.direction, effects, audio);
        }
        if (SDL_HasRectIntersectionFloat(&tip, &playerBox)) {
            player.takeHit(std::max(1, arrow.damage + bonus), arrow.direction.x, arrow.direction.y, effects, audio);
            return true;
        }
        return false;
    });
    std::erase_if(sparks_, [&](Spark& spark) {
        if (++spark.age >= spark.lifetime) return true;
        spark.x += spark.vx;
        spark.y += spark.vy;
        // Spark.tick: touching the player hurts them for 1.
        if (spark.x >= playerBox.x && spark.x < playerBox.x + playerBox.w && spark.y >= playerBox.y &&
            spark.y < playerBox.y + playerBox.h) {
            player.takeHit(1, 0, 0, effects, audio);
        }
        return false;
    });
}

void Projectiles::draw(SDL_Renderer* renderer, const Camera& camera, SDL_Texture* sheet) const {
    for (const Arrow& arrow : arrows_) {
        const SDL_FRect source{static_cast<float>(arrowFrame(arrow.direction)) * kCell, 0.0f, kCell, kCell};
        const SDL_FRect destination{std::floor(arrow.x) - 4.0f - camera.x(), std::floor(arrow.y) - 4.0f - camera.y(),
                                    kCell, kCell};
        SDL_RenderTexture(renderer, sheet, &source, &destination);
    }
    const SDL_FRect sparkSource{kSparkColumn * kCell, 0.0f, kCell, kCell};
    for (const Spark& spark : sparks_) {
        // Spark.render: blinks during its last 2 seconds; a shadow on the ground under the spark.
        if (spark.age >= spark.lifetime - 120 && (spark.age / 6) % 2 == 0) continue;
        const float x = std::floor(spark.x) - 4.0f - camera.x();
        const float y = std::floor(spark.y) - 4.0f - camera.y();
        const auto flip = static_cast<SDL_FlipMode>(SDL_rand(4));  // random mirroring makes it crackle
        const SDL_FRect shadow{x, y + 2.0f, kCell, kCell};
        SDL_SetTextureColorMod(sheet, 0, 0, 0);
        SDL_RenderTextureRotated(renderer, sheet, &sparkSource, &shadow, 0.0, nullptr, flip);
        SDL_SetTextureColorMod(sheet, 255, 255, 255);
        const SDL_FRect body{x, y - 2.0f, kCell, kCell};
        SDL_RenderTextureRotated(renderer, sheet, &sparkSource, &body, 0.0, nullptr, flip);
    }
}

void Projectiles::clear() {
    arrows_.clear();
    sparks_.clear();
}
