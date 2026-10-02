#include "mob.h"

#include "collision.h"
#include "dropped_items.h"
#include "events.h"
#include "player.h"
#include "projectiles.h"
#include "random.h"
#include "tile_map.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace {

constexpr int kHurtTicks = 10;  // Mob.doHurt: hurtTime = 10
constexpr int kKnockback = 6;
constexpr int kDifficultyFactor = 2;  // EnemyMob health: 2^difficulty, normal difficulty

// Enemies (EnemyMob: rwTime 60, rwChance 200 unless noted). They walk 1 px every 3 ticks (20 px/s).
constexpr int kEnemyTicksPerStep = 3;

// Animals (PassiveMob: 5 + healthFactor * difficulty, rwTime 45, rwChance 40, MobAi walkTime 2 = 30 px/s).
constexpr int kAnimalTicksPerStep = 2;

int sign(int value) { return (value > 0) - (value < 0); }

// Mob.dropItem(min, max, items...): one random count, that many of each item.
void dropEach(DroppedItems& drops, Random& rng, Vec2 at, int min, int max, std::initializer_list<ItemType> items) {
    const int count = min + rng.nextInt(max - min + 1);
    for (const ItemType item : items) drops.spawn(item, count, at.x, at.y, rng);
}

int animalHealth(MobKind kind) { return kind == MobKind::Cow ? 10 : 8; }  // cow healthFactor 5, others 3

}  // namespace

const char* mobName(MobKind kind) {
    switch (kind) {
        case MobKind::Zombie: return "Zombie";
        case MobKind::Cow: return "Cow";
        case MobKind::Pig: return "Pig";
        case MobKind::Sheep: return "Sheep";
        case MobKind::Skeleton: return "Skeleton";
        case MobKind::Slime: return "Slime";
        case MobKind::Creeper: return "Creeper";
        case MobKind::Snake: return "Snake";
        case MobKind::AirWizard: return "Air Wizard";
    }
    return "?";
}

bool isEnemy(MobKind kind) { return kind != MobKind::Cow && kind != MobKind::Pig && kind != MobKind::Sheep; }

// ---------------------------------------------------------------------------------------------------------------
// Mob

Player* Mob::World::nearestPlayer(Vec2 from) const {
    Player* nearest = nullptr;
    float best = 0.0f;
    for (Player* player : players) {
        const Vec2 c = player->center();
        const float distance = (c.x - from.x) * (c.x - from.x) + (c.y - from.y) * (c.y - from.y);
        if (!nearest || distance < best) {
            nearest = player;
            best = distance;
        }
    }
    return nearest;
}

Mob::Mob(MobKind kind, float x, float y, int level, int maxHealth, int ticksPerStep, int randomWalkTicks,
         int randomWalkChance)
    : randomWalkTicks_(randomWalkTicks),
      randomWalkChance_(randomWalkChance),
      health_(maxHealth),
      kind_(kind),
      x_(x),
      y_(y),
      level_(level),
      maxHealth_(maxHealth),
      ticksPerStep_(ticksPerStep) {}

void Mob::touchPlayer(Player&, Events&) {}

Point Mob::facing() const {
    switch (direction_) {
        case Direction::Up: return {0, -1};
        case Direction::Down: return {0, 1};
        case Direction::Left: return {-1, 0};
        case Direction::Right: return {1, 0};
    }
    return {0, 1};
}

bool Mob::move(float dx, float dy, const World& world, bool changeDirection) {
    if (dx == 0.0f && dy == 0.0f) return true;
    if (changeDirection) {
        // Direction.getDirection: the dominant axis wins; ties go vertical.
        if (std::abs(dx) > std::abs(dy)) direction_ = dx < 0 ? Direction::Left : Direction::Right;
        else direction_ = dy < 0 ? Direction::Up : Direction::Down;
        ++walkDistance_;
    }
    const auto solid = [&](int tx, int ty) {
        if (floatsOverSky() && world.map.inBounds(tx, ty) && world.map.tileAt(tx, ty) == Tile::InfiniteFall) {
            return false;
        }
        return world.map.blocksMobsAt(tx, ty);
    };
    bool moved = false;
    // Like Entity.move: each axis separately, stopped by tiles, other mobs, furniture and players.
    const auto tryAxis = [&](float delta, bool horizontal) {
        if (delta == 0.0f) return;
        Rect box = hitbox();
        const float allowed = horizontal ? collision::allowedMoveX(box, delta, solid)
                                         : collision::allowedMoveY(box, delta, solid);
        if (allowed == 0.0f) return;
        (horizontal ? box.x : box.y) += allowed;
        for (Player* player : world.players) {
            if (intersects(box, player->hitbox())) {
                touchPlayer(*player, world.events);
                return;
            }
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

    // Mob.tick: lava burns anything standing in it (4 damage, with the usual hurt cooldown).
    const Vec2 c = center();
    const int tx = collision::tileIndex(c.x);
    const int ty = collision::tileIndex(c.y);
    if (world.map.inBounds(tx, ty) && world.map.tileAt(tx, ty) == Tile::Lava) hurt(4, 0, 0, world.events);

    // MobAi.tick: walk 1 px every ticksPerStep ticks; no walking while hurt. Stop if blocked.
    if (ticks_ % ticksPerStep_ == 0 && hurtTime_ == 0 && (moveX_ != 0 || moveY_ != 0)) {
        if (!move(static_cast<float>(moveX_), static_cast<float>(moveY_), world, true)) {
            moveX_ = 0;
            moveY_ = 0;
        }
    }
    if (world.rng.nextInt(randomWalkChance_) == 0) randomizeWalk(world.rng);
    if (randomWalkTime_ > 0) --randomWalkTime_;

    think(world);
}

void Mob::hurt(int damage, int directionX, int directionY, Events& events) {
    if (hurtTime_ > 0) return;
    health_ -= damage;
    hurtTime_ = kHurtTicks;
    knockbackX_ = directionX * kKnockback;
    knockbackY_ = directionY * kKnockback;
    const Vec2 c = center();
    events.number(damage, c.x, c.y);  // red, like MobAi.doHurt
    events.sound(Sound::MonsterHurt);
}

bool Mob::hitPlayer(Player& player, int damage, Events& events) const {
    const Vec2 me = center();
    const Vec2 them = player.center();
    const int dirX = std::abs(them.x - me.x) > std::abs(them.y - me.y) ? (them.x < me.x ? -1 : 1) : 0;
    const int dirY = dirX == 0 ? (them.y < me.y ? -1 : 1) : 0;
    return player.takeHit(damage, dirX, dirY, events);
}

void Mob::chasePlayer(const World& world, int distance) {
    // EnemyMob.tick: unless on a random walk, head for the nearest player when within detectDist, else maybe
    // wander.
    if (randomWalkTime_ > 0) return;
    const Vec2 me = center();
    const Player* target = world.nearestPlayer(me);
    if (!target) return;
    const Vec2 them = target->center();
    const int xd = static_cast<int>(them.x) - static_cast<int>(me.x);
    const int yd = static_cast<int>(them.y) - static_cast<int>(me.y);
    if (xd * xd + yd * yd < distance * distance) {
        // A 1 px dead zone so the mob doesn't jitter once lined up (Minicraft's sig0).
        moveX_ = xd < 1 ? -1 : (xd > 1 ? 1 : 0);
        moveY_ = yd < 1 ? -1 : (yd > 1 ? 1 : 0);
    } else if (world.rng.nextInt(randomWalkChance_) == 0) {
        randomizeWalk(world.rng);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Enemy

Enemy::Enemy(MobKind kind, float x, float y, int level, int baseHealth, int detectDistance, int ticksPerStep,
             int randomWalkTicks, int randomWalkChance, Random& rng, bool scaleHealth)
    : Mob(kind, x, y, level, scaleHealth ? baseHealth * level * level * kDifficultyFactor : baseHealth, ticksPerStep,
          randomWalkTicks, randomWalkChance),
      detectDistance_(detectDistance) {
    randomizeWalk(rng);
}

void Enemy::randomizeWalk(Random& rng) {
    randomWalkTime_ = randomWalkTicks_;
    moveX_ = rng.nextInt(3) - 1;
    moveY_ = rng.nextInt(3) - 1;
}

void Enemy::think(const World& world) { chasePlayer(world, detectDistance_); }

void Enemy::touchPlayer(Player& player, Events& events) {
    // EnemyMob.touchedBy: bumping into the player hits them for the mob's level.
    hitPlayer(player, level(), events);
}

void Enemy::dropLoot(DroppedItems& drops, Random& rng) const { dropItems(drops, rng); }

// ---------------------------------------------------------------------------------------------------------------
// Zombie

Zombie::Zombie(float x, float y, int level, Random& rng)
    : Enemy(MobKind::Zombie, x, y, level, /*baseHealth=*/5, /*detectDistance=*/100, kEnemyTicksPerStep, 60, 200, rng) {}

void Zombie::dropItems(DroppedItems& drops, Random& rng) const {
    // Zombie.die() on normal difficulty: 1-3 cloth, a 1 in 60 chance of iron and a 4% chance of a potato.
    const Vec2 at = center();
    drops.spawn(ItemType::Cloth, 1 + rng.nextInt(3), at.x, at.y, rng);
    if (rng.nextInt(60) == 2) drops.spawn(ItemType::Iron, 1, at.x, at.y, rng);
    if (rng.nextInt(100) < 4) drops.spawn(ItemType::Potato, 1, at.x, at.y, rng);
}

// ---------------------------------------------------------------------------------------------------------------
// Skeleton

Skeleton::Skeleton(float x, float y, int level, Random& rng)
    : Enemy(MobKind::Skeleton, x, y, level, /*baseHealth=*/6, /*detectDistance=*/100, kEnemyTicksPerStep, 45, 200, rng),
      arrowDelay_(500 / (level + 5)),
      arrowTimer_(arrowDelay_) {}

void Skeleton::think(const World& world) {
    Enemy::think(world);
    if (randomWalkTime_ != 0) return;
    // Skeleton.tick: count down while the player is around, and shoot when within 100 px.
    --arrowTimer_;
    const Vec2 me = center();
    const Player* target = world.nearestPlayer(me);
    if (!target) return;
    const Vec2 them = target->center();
    const float xd = them.x - me.x;
    const float yd = them.y - me.y;
    if (xd * xd + yd * yd < 100.0f * 100.0f && arrowTimer_ < 1) {
        world.projectiles.shootArrow(me.x, me.y, facing(), level(), -1);
        arrowTimer_ = arrowDelay_;
    }
}

void Skeleton::dropItems(DroppedItems& drops, Random& rng) const {
    // Skeleton.die() on normal difficulty: usually 1-2 bones and arrows.
    const Vec2 at = center();
    const int count = 1 + rng.nextInt(2);
    drops.spawn(ItemType::Bone, count, at.x, at.y, rng);
    drops.spawn(ItemType::Arrow, count, at.x, at.y, rng);
}

// ---------------------------------------------------------------------------------------------------------------
// Slime

Slime::Slime(float x, float y, int level, Random& rng)
    : Enemy(MobKind::Slime, x, y, level, /*baseHealth=*/1, /*detectDistance=*/50, 1, 60, 40, rng) {}

void Slime::randomizeWalk(Random& rng) {
    if (jumpTime_ > 0) return;  // can't change direction mid-jump
    Enemy::randomizeWalk(rng);
}

void Slime::think(const World& world) {
    Enemy::think(world);
    // Slime.tick: a jump lasts 10 ticks, then it rests at least 10 ticks before the next one.
    if (jumpTime_ <= -10 && (moveX_ != 0 || moveY_ != 0)) jumpTime_ = 10;
    --jumpTime_;
    if (jumpTime_ <= 0) {
        moveX_ = 0;
        moveY_ = 0;
    }
}

void Slime::dropItems(DroppedItems& drops, Random& rng) const {
    // Slime.die(): 1-3 slime on normal difficulty.
    const Vec2 at = center();
    drops.spawn(ItemType::Slime, 1 + rng.nextInt(3), at.x, at.y, rng);
}

// ---------------------------------------------------------------------------------------------------------------
// Creeper

Creeper::Creeper(float x, float y, int level, Random& rng)
    : Enemy(MobKind::Creeper, x, y, level, /*baseHealth=*/10, /*detectDistance=*/50, kEnemyTicksPerStep, 60, 200, rng) {}

void Creeper::think(const World& world) {
    constexpr float kTriggerRadius = 64.0f;
    if (fuseTime_ > 0) {
        --fuseTime_;  // the fuse burns: stand still
        moveX_ = 0;
        moveY_ = 0;
        return;
    }
    if (fuseLit_) {
        moveX_ = 0;
        moveY_ = 0;
        const Vec2 me = center();
        const Player* target = world.nearestPlayer(me);
        if (target && std::abs(target->center().x - me.x) < kTriggerRadius &&
            std::abs(target->center().y - me.y) < kTriggerRadius) {
            exploding_ = true;  // Mobs runs the blast and removes the creeper
        } else {
            fuseLit_ = false;  // the player got away: calm down
        }
        return;
    }
    Enemy::think(world);
}

void Creeper::touchPlayer(Player& player, Events& events) {
    if (fuseTime_ == 0 && !fuseLit_) {
        events.sound(Sound::Fuse);
        fuseTime_ = 60;
        fuseLit_ = true;
    }
    hitPlayer(player, 1, events);
}

void Creeper::dropItems(DroppedItems& drops, Random& rng) const {
    // Creeper.die(): 1-3 gunpowder, unless it blew itself up.
    if (exploding_) return;
    const Vec2 at = center();
    drops.spawn(ItemType::Gunpowder, 1 + rng.nextInt(3), at.x, at.y, rng);
}

// ---------------------------------------------------------------------------------------------------------------
// Snake

Snake::Snake(float x, float y, int level, Random& rng)
    : Enemy(MobKind::Snake, x, y, level, level > 1 ? 8 : 7, /*detectDistance=*/100, kEnemyTicksPerStep, 60, 200,
            rng) {}

void Snake::touchPlayer(Player& player, Events& events) {
    hitPlayer(player, level() + 1, events);  // Snake.touchedBy: lvl + difficulty
}

void Snake::dropItems(DroppedItems& drops, Random& rng) const {
    // Snake.die(): 0-1 scales (more generous here, so snake armour is reachable: 1-2).
    const Vec2 at = center();
    drops.spawn(ItemType::Scale, 1 + rng.nextInt(2), at.x, at.y, rng);
}

// ---------------------------------------------------------------------------------------------------------------
// Air Wizard

AirWizard::AirWizard(float x, float y, Random& rng)
    : Enemy(MobKind::AirWizard, x, y, 1, kMaxHealth, /*detectDistance=*/128, /*ticksPerStep=*/1, 10, 50, rng,
            /*scaleHealth=*/false) {}

void AirWizard::hurt(int damage, int directionX, int directionY, Events& events) {
    Enemy::hurt(damage, directionX, directionY, events);
    // AirWizard.doHurt: being hit makes it wind up a spiral.
    if (attackDelay_ == 0 && attackTime_ == 0) attackDelay_ = 120;
}

void AirWizard::think(const World& world) {
    if (attackDelay_ > 0) {
        // Winding up: spin on the spot, then pick the spiral by how hurt it is.
        moveX_ = 0;
        moveY_ = 0;
        int dir = (attackDelay_ - 45) / 4 % 4;
        dir = (dir * 2 % 4) + (dir / 2);
        if (attackDelay_ < 45) dir = 0;
        direction_ = static_cast<Direction>(dir);
        if (--attackDelay_ == 0) {
            if (health_ < kMaxHealth / 2) attackType_ = 1;
            if (health_ < kMaxHealth / 10) attackType_ = 2;
            attackTime_ = 120;
        }
        return;
    }
    if (attackTime_ > 0) {
        // A spiral of sparks: one per tick, the angle sweeping back and forth as the attack winds down.
        moveX_ = 0;
        moveY_ = 0;
        attackTime_ = static_cast<int>(static_cast<float>(attackTime_) * 0.92f);
        const double angle = attackTime_ * 0.25 * (attackTime_ % 2 * 2 - 1);
        const double speed = 0.7 + attackType_ * 0.2;
        const Vec2 me = center();
        world.projectiles.addSpark(me.x, me.y, static_cast<float>(std::cos(angle) * speed),
                                   static_cast<float>(std::sin(angle) * speed), world.rng);
        return;
    }
    if (randomWalkTime_ != 0) return;
    const Vec2 me = center();
    const Player* target = world.nearestPlayer(me);
    if (!target) return;
    const Vec2 them = target->center();
    float xd = them.x - me.x;
    float yd = them.y - me.y;
    constexpr float kTooClose = 32.0f;       // 2 tiles: back away
    constexpr float kTooFar = 15.0f * 16.0f;  // 15 tiles: teleport back towards the player
    if (xd * xd + yd * yd < kTooClose * kTooClose) {
        moveX_ = xd < 0 ? 1 : (xd > 0 ? -1 : 0);
        moveY_ = yd < 0 ? 1 : (yd > 0 ? -1 : 0);
    } else if (xd * xd + yd * yd > kTooFar * kTooFar) {
        const float distance = std::sqrt(xd * xd + yd * yd);
        teleport(them.x - xd * kTooFar / distance - 8.0f, them.y - yd * kTooFar / distance - 11.0f);
        xd = them.x - center().x;
        yd = them.y - center().y;
    }
    if (world.rng.nextInt(4) == 0 && xd * xd + yd * yd < 50.0f * 50.0f) attackDelay_ = 120;
}

void AirWizard::touchPlayer(Player& player, Events& events) { hitPlayer(player, 1, events); }

void AirWizard::dropItems(DroppedItems& drops, Random& rng) const {
    // AirWizard.die(): 5-10 cloud ore.
    const Vec2 at = center();
    drops.spawn(ItemType::CloudOre, 5 + rng.nextInt(6), at.x, at.y, rng);
}

// ---------------------------------------------------------------------------------------------------------------
// Animal

Animal::Animal(MobKind kind, float x, float y)
    : Mob(kind, x, y, 1, animalHealth(kind), kAnimalTicksPerStep, /*randomWalkTicks=*/45, /*randomWalkChance=*/40) {}

void Animal::randomizeWalk(Random& rng) {
    // PassiveMob.randomizeWalkDir: each axis is -1, 0 or 1, but zeroed half the time, so animals often stand still.
    randomWalkTime_ = randomWalkTicks_;
    moveX_ = (rng.nextInt(3) - 1) * rng.nextInt(2);
    moveY_ = (rng.nextInt(3) - 1) * rng.nextInt(2);
}

void Animal::think(const World&) {
    // Nothing beyond MobAi's random walks: animals just wander.
}

void Animal::dropLoot(DroppedItems& drops, Random& rng) const {
    // die() on normal difficulty: 1-2 of each listed item.
    const Vec2 at = center();
    switch (kind()) {
        case MobKind::Cow: dropEach(drops, rng, at, 1, 2, {ItemType::Leather, ItemType::RawBeef}); break;
        case MobKind::Pig: dropEach(drops, rng, at, 1, 2, {ItemType::RawPork}); break;
        case MobKind::Sheep:
            dropEach(drops, rng, at, 1, 2, {ItemType::WhiteWool});
            dropEach(drops, rng, at, 1, 2, {ItemType::RawBeef});
            break;
        default: break;
    }
}
