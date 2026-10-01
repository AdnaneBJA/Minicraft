#include "mob.h"

#include "camera.h"
#include "collision.h"
#include "dropped_items.h"
#include "effects.h"
#include "player.h"
#include "tile_map.h"

#include <cmath>
#include <initializer_list>

namespace {

constexpr float kSize = 16.0f;
constexpr int kHurtTicks = 10;  // Mob.doHurt: hurtTime = 10
constexpr int kKnockback = 6;
constexpr SDL_Color kPlayerDamageColor{255, 0, 204, 255};  // Minicraft: Color.get(-1, 504)

// Zombies.
constexpr int kZombieHealth = 10;
constexpr int kZombieTicksPerStep = 3;  // 20 px/s (Minicraft's walkTime 2 gives 30 px/s)
constexpr int kZombieDetectDistance = 100;
constexpr int kContactDamage = 1;  // EnemyMob.touchedBy: lvl * 1 on normal difficulty

// Animals (PassiveMob: 5 + healthFactor * difficulty, rwTime 45, rwChance 40, MobAi walkTime 2 = 30 px/s).
constexpr int kAnimalTicksPerStep = 2;

int sign(int value) { return (value > 0) - (value < 0); }

// No mob here can swim, so water and holes block them as well as solid tiles.
bool blocksMob(const TileMap& map, int tx, int ty) { return !map.inBounds(tx, ty) || blocksMobs(map.tileAt(tx, ty)); }

// Mob.dropItem(min, max, items...): one random count, that many of each item.
void dropEach(DroppedItems& drops, SDL_FPoint at, int min, int max, std::initializer_list<ItemType> items) {
    const int count = min + static_cast<int>(SDL_rand(max - min + 1));
    for (const ItemType item : items) drops.spawn(item, count, at.x, at.y);
}

int animalHealth(MobKind kind) { return kind == MobKind::Cow ? 10 : 8; }  // cow healthFactor 5, others 3

}  // namespace

const char* mobName(MobKind kind) {
    switch (kind) {
        case MobKind::Zombie: return "Zombie";
        case MobKind::Cow: return "Cow";
        case MobKind::Pig: return "Pig";
        case MobKind::Sheep: return "Sheep";
    }
    return "?";
}

// ---------------------------------------------------------------------------------------------------------------
// Mob

Mob::Mob(MobKind kind, float x, float y, int maxHealth, int ticksPerStep, int randomWalkTicks, int randomWalkChance)
    : randomWalkTicks_(randomWalkTicks),
      randomWalkChance_(randomWalkChance),
      kind_(kind),
      x_(x),
      y_(y),
      health_(maxHealth),
      ticksPerStep_(ticksPerStep) {}

void Mob::touchPlayer(Player&, Effects&) {}

bool Mob::move(float dx, float dy, const World& world, bool changeDirection) {
    if (dx == 0.0f && dy == 0.0f) return true;
    if (changeDirection) {
        // Direction.getDirection: the dominant axis wins; ties go vertical.
        if (std::abs(dx) > std::abs(dy)) direction_ = dx < 0 ? Direction::Left : Direction::Right;
        else direction_ = dy < 0 ? Direction::Up : Direction::Down;
        ++walkDistance_;
    }
    const auto solid = [&](int tx, int ty) { return blocksMob(world.map, tx, ty); };
    const SDL_FRect playerBox = world.player.hitbox();
    bool moved = false;
    // Like Entity.move: each axis separately, stopped by tiles, other mobs, furniture and the player.
    const auto tryAxis = [&](float delta, bool horizontal) {
        if (delta == 0.0f) return;
        SDL_FRect box = hitbox();
        const float allowed = horizontal ? collision::allowedMoveX(box, delta, solid)
                                         : collision::allowedMoveY(box, delta, solid);
        if (allowed == 0.0f) return;
        (horizontal ? box.x : box.y) += allowed;
        if (SDL_HasRectIntersectionFloat(&box, &playerBox)) {
            touchPlayer(world.player, world.effects);
            return;
        }
        if (world.blocked(box, this)) return;
        (horizontal ? x_ : y_) += allowed;
        moved = true;
    };
    tryAxis(dx, true);
    tryAxis(dy, false);
    return moved;
}

void Mob::tick(const World& world) {
    ++ticks_;
    if (hurtTime_ > 0) --hurtTime_;

    // Knockback (Mob.tick): half the remaining steps each tick, shrinking by one.
    if (knockbackX_ != 0 || knockbackY_ != 0) {
        move(static_cast<float>(knockbackX_ / 2), static_cast<float>(knockbackY_ / 2), world, false);
        knockbackX_ -= sign(knockbackX_);
        knockbackY_ -= sign(knockbackY_);
    }

    // MobAi.tick: walk 1 px every ticksPerStep ticks; no walking while hurt. Stop if blocked.
    if (ticks_ % ticksPerStep_ == 0 && hurtTime_ == 0 && (moveX_ != 0 || moveY_ != 0)) {
        if (!move(static_cast<float>(moveX_), static_cast<float>(moveY_), world, true)) {
            moveX_ = 0;
            moveY_ = 0;
        }
    }
    if (SDL_rand(randomWalkChance_) == 0) randomizeWalk();
    if (randomWalkTime_ > 0) --randomWalkTime_;

    think(world);
}

void Mob::hurt(int damage, int directionX, int directionY, Effects& effects) {
    if (hurtTime_ > 0) return;
    health_ -= damage;
    hurtTime_ = kHurtTicks;
    knockbackX_ = directionX * kKnockback;
    knockbackY_ = directionY * kKnockback;
    const SDL_FPoint c = center();
    effects.addDamageNumber(damage, c.x, c.y);  // red, like MobAi.doHurt
}

void Mob::draw(SDL_Renderer* renderer, const Camera& camera, SDL_Texture* sprite, SDL_Texture* flash) const {
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

// ---------------------------------------------------------------------------------------------------------------
// Zombie

Zombie::Zombie(float x, float y)
    : Mob(MobKind::Zombie, x, y, kZombieHealth, kZombieTicksPerStep, /*randomWalkTicks=*/60,
          /*randomWalkChance=*/200) {
    randomizeWalk();
}

void Zombie::randomizeWalk() {
    randomWalkTime_ = randomWalkTicks_;
    moveX_ = static_cast<int>(SDL_rand(3)) - 1;
    moveY_ = static_cast<int>(SDL_rand(3)) - 1;
}

void Zombie::think(const World& world) {
    // EnemyMob.tick: unless on a random walk, head for the player when within detectDist, else maybe wander.
    if (randomWalkTime_ > 0) return;
    const SDL_FPoint me = center();
    const SDL_FPoint them = world.player.center();
    const int xd = static_cast<int>(them.x) - static_cast<int>(me.x);
    const int yd = static_cast<int>(them.y) - static_cast<int>(me.y);
    if (xd * xd + yd * yd < kZombieDetectDistance * kZombieDetectDistance) {
        // A 1 px dead zone so the zombie doesn't jitter once lined up (Minicraft's sig0).
        moveX_ = xd < 1 ? -1 : (xd > 1 ? 1 : 0);
        moveY_ = yd < 1 ? -1 : (yd > 1 ? 1 : 0);
    } else if (SDL_rand(randomWalkChance_) == 0) {
        randomizeWalk();
    }
}

void Zombie::touchPlayer(Player& player, Effects& effects) {
    // Bumping into the player is the zombie's punch (EnemyMob.touchedBy), knocking them away.
    const SDL_FPoint me = center();
    const SDL_FPoint them = player.center();
    const int dirX = std::abs(them.x - me.x) > std::abs(them.y - me.y) ? (them.x < me.x ? -1 : 1) : 0;
    const int dirY = dirX == 0 ? (them.y < me.y ? -1 : 1) : 0;
    if (player.takeHit(kContactDamage, dirX, dirY)) {
        effects.addDamageNumber(kContactDamage, them.x, them.y, kPlayerDamageColor);
    }
}

void Zombie::dropLoot(DroppedItems& drops) const {
    // Zombie.die() on normal difficulty: 1-3 cloth, a 1 in 60 chance of iron and a 4% chance of a potato.
    // (Minicraft also has a 1 in 40 chance of coloured clothes, which are armour and don't exist here yet.)
    const SDL_FPoint at = center();
    drops.spawn(ItemType::Cloth, 1 + static_cast<int>(SDL_rand(3)), at.x, at.y);
    if (SDL_rand(60) == 2) drops.spawn(ItemType::Iron, 1, at.x, at.y);
    if (SDL_rand(100) < 4) drops.spawn(ItemType::Potato, 1, at.x, at.y);
}

// ---------------------------------------------------------------------------------------------------------------
// Animal

Animal::Animal(MobKind kind, float x, float y)
    : Mob(kind, x, y, animalHealth(kind), kAnimalTicksPerStep, /*randomWalkTicks=*/45, /*randomWalkChance=*/40) {}

void Animal::randomizeWalk() {
    // PassiveMob.randomizeWalkDir: each axis is -1, 0 or 1, but zeroed half the time, so animals often stand still.
    randomWalkTime_ = randomWalkTicks_;
    moveX_ = (static_cast<int>(SDL_rand(3)) - 1) * static_cast<int>(SDL_rand(2));
    moveY_ = (static_cast<int>(SDL_rand(3)) - 1) * static_cast<int>(SDL_rand(2));
}

void Animal::think(const World&) {
    // Nothing beyond MobAi's random walks: animals just wander.
}

void Animal::dropLoot(DroppedItems& drops) const {
    // die() on normal difficulty: 1-2 of each listed item.
    const SDL_FPoint at = center();
    switch (kind()) {
        case MobKind::Cow: dropEach(drops, at, 1, 2, {ItemType::Leather, ItemType::RawBeef}); break;
        case MobKind::Pig: dropEach(drops, at, 1, 2, {ItemType::RawPork}); break;
        case MobKind::Sheep:
            dropEach(drops, at, 1, 2, {ItemType::WhiteWool});
            dropEach(drops, at, 1, 2, {ItemType::RawBeef});
            break;
        case MobKind::Zombie: break;
    }
}
