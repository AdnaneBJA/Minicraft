#include "zombie.h"

#include "camera.h"
#include "collision.h"
#include "effects.h"
#include "player.h"
#include "tile_map.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kTick = 1.0f / 60.0f;
constexpr float kSize = 16.0f;
constexpr int kDetectDistance = 100;    // EnemyMob detectDist for zombies: chases the player within 100 px
constexpr int kRandomWalkTicks = 60;    // EnemyMob default rwTime
constexpr int kRandomWalkChance = 200;  // 1 in 200 ticks starts a random walk
constexpr int kHurtTicks = 10;          // Mob.doHurt: hurtTime = 10
constexpr int kTicksPerStep = 3;        // 1 px every 3 ticks = 20 px/s (Minicraft's walkTime 2 gives 30 px/s)
constexpr int kKnockback = 6;
constexpr int kContactDamage = 1;  // EnemyMob.touchedBy: lvl * 1 on normal difficulty
constexpr int kSpawnIntervalTicks = 60;
constexpr int kSpawnMinTiles = 10;  // off screen, like Minicraft's 60 px minimum but further
constexpr int kSpawnMaxTiles = 20;
constexpr float kDespawnDistance = 48.0f * static_cast<float>(TileMap::kTileSize);
constexpr float kDaytimeDespawnDistance = 12.0f * static_cast<float>(TileMap::kTileSize);  // out of view
constexpr SDL_Color kPlayerDamageColor{255, 0, 204, 255};  // Minicraft: Color.get(-1, 504)

// Zombies can't swim, so water blocks them as well as solid tiles.
bool blocksZombie(const TileMap& map, int tx, int ty) {
    return map.isSolidAt(tx, ty) || map.tileAt(tx, ty) == Tile::Water;
}

int sign(int value) { return (value > 0) - (value < 0); }

}  // namespace

Zombie::Zombie(float x, float y) : x_(x), y_(y) { randomizeWalk(); }

void Zombie::randomizeWalk() {
    randomWalkTime_ = kRandomWalkTicks;
    moveX_ = static_cast<int>(SDL_rand(3)) - 1;
    moveY_ = static_cast<int>(SDL_rand(3)) - 1;
}

template <typename Blocked>
bool Zombie::move(float dx, float dy, const TileMap& map, Player& player, Effects& effects, Blocked blocked,
                  bool changeDirection) {
    if (dx == 0.0f && dy == 0.0f) return true;
    if (changeDirection) {
        // Direction.getDirection: the dominant axis wins; ties go vertical.
        if (std::abs(dx) > std::abs(dy)) direction_ = dx < 0 ? Direction::Left : Direction::Right;
        else direction_ = dy < 0 ? Direction::Up : Direction::Down;
        ++walkDistance_;
    }
    const auto solid = [&](int tx, int ty) { return blocksZombie(map, tx, ty); };
    const SDL_FRect playerBox = player.hitbox();
    bool moved = false;
    // Like Entity.move: each axis separately, stopped by tiles, other zombies and the player.
    const auto tryAxis = [&](float delta, bool horizontal) {
        if (delta == 0.0f) return;
        SDL_FRect box = hitbox();
        const float allowed = horizontal ? collision::allowedMoveX(box, delta, solid)
                                         : collision::allowedMoveY(box, delta, solid);
        if (allowed == 0.0f) return;
        (horizontal ? box.x : box.y) += allowed;
        if (SDL_HasRectIntersectionFloat(&box, &playerBox)) {
            // Bumping into the player is the zombie's punch (EnemyMob.touchedBy), knocking them away.
            const SDL_FPoint me = center();
            const SDL_FPoint them = player.center();
            const int dirX = std::abs(them.x - me.x) > std::abs(them.y - me.y) ? (them.x < me.x ? -1 : 1) : 0;
            const int dirY = dirX == 0 ? (them.y < me.y ? -1 : 1) : 0;
            if (player.takeHit(kContactDamage, dirX, dirY)) {
                effects.addDamageNumber(kContactDamage, them.x, them.y, kPlayerDamageColor);
            }
            return;
        }
        if (blocked(box, this)) return;
        (horizontal ? x_ : y_) += allowed;
        moved = true;
    };
    tryAxis(dx, true);
    tryAxis(dy, false);
    return moved;
}

template <typename Blocked>
void Zombie::tick(const TileMap& map, Player& player, Effects& effects, Blocked blocked) {
    ++ticks_;
    if (hurtTime_ > 0) --hurtTime_;

    // Knockback (Mob.tick): half the remaining steps each tick, shrinking by one.
    if (knockbackX_ != 0 || knockbackY_ != 0) {
        move(static_cast<float>(knockbackX_ / 2), static_cast<float>(knockbackY_ / 2), map, player, effects, blocked,
             false);
        knockbackX_ -= sign(knockbackX_);
        knockbackY_ -= sign(knockbackY_);
    }

    // MobAi.tick: walk 1 px every kTicksPerStep ticks; no walking while hurt. Stop if blocked.
    if (ticks_ % kTicksPerStep == 0 && hurtTime_ == 0 && (moveX_ != 0 || moveY_ != 0)) {
        if (!move(static_cast<float>(moveX_), static_cast<float>(moveY_), map, player, effects, blocked, true)) {
            moveX_ = 0;
            moveY_ = 0;
        }
    }
    if (SDL_rand(kRandomWalkChance) == 0) randomizeWalk();
    if (randomWalkTime_ > 0) --randomWalkTime_;

    // EnemyMob.tick: unless on a random walk, head for the player when within detectDist, else maybe wander.
    if (randomWalkTime_ <= 0) {
        const SDL_FPoint me = center();
        const SDL_FPoint them = player.center();
        const int xd = static_cast<int>(them.x) - static_cast<int>(me.x);
        const int yd = static_cast<int>(them.y) - static_cast<int>(me.y);
        if (xd * xd + yd * yd < kDetectDistance * kDetectDistance) {
            // A 1 px dead zone so the zombie doesn't jitter once lined up (Minicraft's sig0).
            moveX_ = xd < 1 ? -1 : (xd > 1 ? 1 : 0);
            moveY_ = yd < 1 ? -1 : (yd > 1 ? 1 : 0);
        } else if (SDL_rand(kRandomWalkChance) == 0) {
            randomizeWalk();
        }
    }
}

void Zombie::hurt(int damage, int directionX, int directionY, Effects& effects) {
    if (hurtTime_ > 0) return;
    health_ -= damage;
    hurtTime_ = kHurtTicks;
    knockbackX_ = directionX * kKnockback;
    knockbackY_ = directionY * kKnockback;
    const SDL_FPoint c = center();
    effects.addDamageNumber(damage, c.x, c.y);  // red, like MobAi.doHurt
}

void Zombie::draw(SDL_Renderer* renderer, const Camera& camera, SDL_Texture* sprite, SDL_Texture* flash) const {
    // Same sheet layout and mirroring as the player: down, up, right 1, right 2.
    const int frame = (walkDistance_ >> 3) & 1;
    int column = 0;
    bool mirrored = false;
    switch (direction_) {
        case Direction::Down: column = 0; mirrored = frame == 1; break;
        case Direction::Up: column = 1; mirrored = frame == 1; break;
        case Direction::Right: column = 2 + frame; break;
        case Direction::Left: column = 2 + frame; mirrored = true; break;
    }
    const SDL_FRect source{static_cast<float>(column) * kSize, 0.0f, kSize, kSize};
    const SDL_FRect destination{std::floor(x_) - camera.x(), std::floor(y_) - camera.y(), kSize, kSize};
    // MobAi.render: white while hurt.
    SDL_RenderTextureRotated(renderer, hurtTime_ > 0 ? flash : sprite, &source, &destination, 0.0, nullptr,
                             mirrored ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
}

bool Zombies::load(SDL_Renderer* renderer, const std::string& spritePath) {
    sprite_ = loadTexture(renderer, spritePath);
    flash_ = loadTexture(renderer, spritePath, true);
    return sprite_ && flash_;
}

void Zombies::update(float dt, const TileMap& map, Player& player, Effects& effects, bool night) {
    tickAccumulator_ += dt;
    while (tickAccumulator_ >= kTick) {
        tickAccumulator_ -= kTick;
        tick(map, player, effects, night);
    }
}

void Zombies::tick(const TileMap& map, Player& player, Effects& effects, bool night) {
    const auto blocked = [&](const SDL_FRect& box, const Zombie* self) {
        for (const Zombie& other : zombies_) {
            if (&other == self) continue;
            const SDL_FRect otherBox = other.hitbox();
            if (SDL_HasRectIntersectionFloat(&box, &otherBox)) return true;
        }
        return false;
    };
    for (Zombie& zombie : zombies_) {
        zombie.tick(map, player, effects, blocked);
    }

    // Remove dead zombies and those that wandered (or were left) too far from the player.
    const SDL_FPoint p = player.center();
    std::erase_if(zombies_, [&](const Zombie& zombie) {
        const SDL_FPoint c = zombie.center();
        const float distance = std::hypot(c.x - p.x, c.y - p.y);
        return zombie.isDead() || distance > kDespawnDistance || (!night && distance > kDaytimeDespawnDistance);
    });

    if (spawningEnabled && night && ++spawnTimer_ >= kSpawnIntervalTicks) {
        spawnTimer_ = 0;
        if (static_cast<int>(zombies_.size()) < kMaxAlive) {
            spawnNear(map, p.x, p.y, kSpawnMinTiles, kSpawnMaxTiles);
        }
    }
}

bool Zombies::spawnNear(const TileMap& map, float x, float y, int minTiles, int maxTiles) {
    const int centerX = collision::tileIndex(x);
    const int centerY = collision::tileIndex(y);
    for (int attempt = 0; attempt < 20; ++attempt) {
        const int tx = centerX + static_cast<int>(SDL_rand(2 * maxTiles + 1)) - maxTiles;
        const int ty = centerY + static_cast<int>(SDL_rand(2 * maxTiles + 1)) - maxTiles;
        const int distance = std::max(std::abs(tx - centerX), std::abs(ty - centerY));
        if (distance < minTiles || !map.inBounds(tx, ty) || blocksZombie(map, tx, ty)) continue;
        Zombie zombie(static_cast<float>(tx * TileMap::kTileSize), static_cast<float>(ty * TileMap::kTileSize) - 3.0f);
        const SDL_FRect box = zombie.hitbox();
        const bool occupied = std::any_of(zombies_.begin(), zombies_.end(), [&](const Zombie& other) {
            const SDL_FRect otherBox = other.hitbox();
            return SDL_HasRectIntersectionFloat(&box, &otherBox);
        });
        if (occupied) continue;
        zombies_.push_back(zombie);
        return true;
    }
    return false;
}

bool Zombies::punch(const SDL_FRect& attackBox, int damage, SDL_Point direction, Effects& effects) {
    bool hit = false;
    for (Zombie& zombie : zombies_) {
        const SDL_FRect box = zombie.hitbox();
        if (SDL_HasRectIntersectionFloat(&box, &attackBox)) {
            zombie.hurt(damage, direction.x, direction.y, effects);
            hit = true;
        }
    }
    return hit;
}

void Zombies::draw(SDL_Renderer* renderer, const Camera& camera, float playerY, bool behind) const {
    for (const Zombie& zombie : zombies_) {
        if ((zombie.center().y < playerY) == behind) {
            zombie.draw(renderer, camera, sprite_.get(), flash_.get());
        }
    }
}
