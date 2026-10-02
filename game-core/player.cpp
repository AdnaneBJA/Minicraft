#include "player.h"

#include "collision.h"
#include "events.h"
#include "tile_map.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kSpeed = 1.0f;               // pixels per tick (60 px/s, like Minicraft)
constexpr float kSwimSpeedFactor = 0.5f;     // Minicraft skips every other movement tick in water
constexpr int kAttackTicks = 5;                  // a bare-hand punch lasts 5 ticks in Minicraft
constexpr float kInteractDistance = 12.0f;       // Minicraft's INTERACT_DIST
constexpr int kTicksPerBolt = 30;          // one bolt every ~0.5 s (Minicraft's 10 felt far too fast)
constexpr int kExhaustedDelayTicks = 40;   // pause before recharging after running out
constexpr int kSwimDrainTicks = 60;        // in water, lose a bolt (or a heart when out of energy) every second
constexpr int kHurtTicks = 30;             // Minicraft's playerHurtTime: no further damage meanwhile
constexpr int kHurtFlashTicks = 10;        // the sprite shows white for the first 10 ticks of that

constexpr int kKnockback = 6;              // Minicraft: a hit pushes 6 "steps" away from the attacker
constexpr int kLavaDamage = 4;             // Mob.tick: standing in lava

// Minicraft+'s hunger on normal difficulty.
constexpr int kMaxHungerTicks = 400;   // stamHungerTicks: one "bite" every 400 points
constexpr int kBitesPerHunger = 7;     // maxHungerStams[normal]
constexpr int kHungerTickPeriod = 30;  // hungerTickCount[normal]: time wears 1 point off every 30 ticks
constexpr int kHungerStepCount = 3;    // hungerStepCount[normal]: and walking 1 point every 3 px
constexpr int kMinStarveHealth = 3;    // minStarveHealth[normal]: starving never takes the last 3 hearts
constexpr int kStarveTicks = 120;
constexpr int kFoodEnergyCost = 2;     // FoodItem.staminaCost
constexpr int kArmorEnergyCost = 9;    // ArmorItem.staminaCost

constexpr float kAttackRange = 20.0f;      // Minicraft's ATTACK_DIST: how far a punch reaches for mobs

using collision::tileIndex;

}  // namespace

bool Player::tryPunch() {
    if (energy_ <= 0) return false;  // Minicraft only allows attacking with stamina left
    --energy_;
    // Like Minicraft's attack, punching restarts the recharge count: no energy comes back while punching, and
    // running out triggers the exhaustion pause with the blinking bolts.
    energyRecharge_ = 0;
    attackDirection_ = direction_;
    attackTicks_ = 0;
    punchHand_ = 1 - punchHand_;  // alternate hands, so held punches go left, right, left...
    punchPoseTicks_ = kAttackTicks;
    return true;
}

bool Player::payEnergy(int cost) {
    if (energy_ <= 0) return false;
    energy_ -= std::min(energy_, std::max(0, cost));
    return true;
}

void Player::showSlash() { attackTicks_ = kAttackTicks; }

bool Player::isHurtFlashing() const { return hurtTime_ > kHurtTicks - kHurtFlashTicks; }

void Player::refillStats() {
    health_ = kMaxHealth;
    energy_ = kMaxEnergy;
    hunger_ = kMaxHunger;
    hungerStamCount_ = kBitesPerHunger;
    stamHungerTicks_ = kMaxHungerTicks;
    hungerChargeDelay_ = 0;
    hungerStarveDelay_ = 0;
    energyRecharge_ = 0;
    energyRechargeDelay_ = 0;
    hurtTime_ = 0;
    knockbackX_ = 0;
    knockbackY_ = 0;
}

void Player::hurt(int damage) {
    if (hurtTime_ > 0) return;
    health_ = std::max(0, health_ - damage);
    damageTaken_ += damage;
    hurtTime_ = kHurtTicks;
}

bool Player::takeHit(int damage, int directionX, int directionY, Events& events) {
    if (hurtTime_ > 0 || damage <= 0) return false;
    const Vec2 c = center();
    int healthDamage = damage;
    if (armor_) {
        // Player.doHurt: armour takes the whole hit, and every (level + 1) points of it also cost a heart.
        healthDamage = 0;
        armorDamageBuffer_ += damage;
        const int perHeart = armorLevel(*armor_) + 1;
        while (armorDamageBuffer_ >= perHeart) {
            armorDamageBuffer_ -= perHeart;
            ++healthDamage;
        }
        events.number(damage, c.x, c.y, NumberStyle::ArmorDamage);
        armorPoints_ -= damage;
        if (armorPoints_ <= 0) {
            healthDamage -= armorPoints_;  // what the armour could not take
            removeArmor();
        }
    }
    if (healthDamage > 0) {
        events.number(healthDamage, c.x, c.y - (armor_ ? 6.0f : 0.0f), NumberStyle::PlayerDamage);
        health_ = std::max(0, health_ - healthDamage);
    }
    events.sound(Sound::PlayerHurt);
    hurtTime_ = kHurtTicks;
    knockbackX_ = directionX * kKnockback;
    knockbackY_ = directionY * kKnockback;
    return true;
}

bool Player::eat(int value) {
    if (value <= 0 || hunger_ >= kMaxHunger || !payEnergy(kFoodEnergyCost)) return false;
    hunger_ = std::min(kMaxHunger, hunger_ + value);
    return true;
}

bool Player::wearArmor(ItemType armor) {
    if (armor_ || armorLevel(armor) == 0 || !payEnergy(kArmorEnergyCost)) return false;
    armor_ = armor;
    armorPoints_ = ::armorPoints(armor);
    armorDamageBuffer_ = 0;
    return true;
}

void Player::removeArmor() {
    armor_.reset();
    armorPoints_ = 0;
    armorDamageBuffer_ = 0;
}

float Player::lightRadius() const {
    const float held = heldItem_ ? static_cast<float>(::lightRadius(heldItem_->type) * 8) : 0.0f;
    return std::max(kLightRadius, held);
}

void Player::tickHunger() {
    // Player.tick (normal difficulty): low energy, healing, time and walking all wear the stomach down.
    if (energy_ < kMaxEnergy) {
        stamHungerTicks_ -= 1;
        if (energy_ == 0) stamHungerTicks_ -= 1;
    }
    if (hungerChargeDelay_ > 0) {
        stamHungerTicks_ -= 3;
        if (hunger_ == 0) stamHungerTicks_ -= 1;
    }
    if (ticks_ % kHungerTickPeriod == 0) --stamHungerTicks_;
    if (stepCount_ >= kHungerStepCount) {
        --stamHungerTicks_;
        stepCount_ = 0;
    }
    if (stamHungerTicks_ <= 0) {
        stamHungerTicks_ += kMaxHungerTicks;
        --hungerStamCount_;
    }
    while (hungerStamCount_ <= 0) {
        hunger_ = std::max(0, hunger_ - 1);
        hungerStamCount_ += kBitesPerHunger;
    }
    // A stomach more than half full slowly heals: faster the fuller it is.
    if (health_ < kMaxHealth && hunger_ > kMaxHunger / 2) {
        const int missing = kMaxHunger - hunger_ + 2;
        if (++hungerChargeDelay_ > 20 * missing * missing) {
            ++health_;
            hungerChargeDelay_ = 0;
        }
    } else {
        hungerChargeDelay_ = 0;
    }
    // An empty one hurts every 2 seconds, down to 3 hearts.
    if (hunger_ == 0 && health_ > kMinStarveHealth) {
        if (hungerStarveDelay_ == 0) hungerStarveDelay_ = kStarveTicks;
        if (--hungerStarveDelay_ == 0) {
            health_ -= 1;
            damageTaken_ += 1;
        }
    }
}

Rect Player::attackBox() const {
    // Minicraft's Player.getInteractionBox(ATTACK_DIST): from 4 to 20 px ahead of the centre (raised by 2 px),
    // and 8 px wide to one side.
    const Vec2 c = center();
    const float x = c.x;
    const float y = c.y - 2.0f;
    int dirX = 0;
    int dirY = 0;
    switch (direction_) {
        case Direction::Up: dirY = -1; break;
        case Direction::Down: dirY = 1; break;
        case Direction::Left: dirX = -1; break;
        case Direction::Right: dirX = 1; break;
    }
    const float xClose = x + static_cast<float>(dirX) * 4.0f;
    const float yClose = y + static_cast<float>(dirY) * 4.0f;
    const float xFar = x + static_cast<float>(dirX) * kAttackRange + static_cast<float>(dirY) * 8.0f;
    const float yFar = y + static_cast<float>(dirY) * kAttackRange + static_cast<float>(dirX) * 8.0f;
    const float left = std::min(xClose, xFar);
    const float top = std::min(yClose, yFar);
    return {left, top, std::max(xClose, xFar) - left, std::max(yClose, yFar) - top};
}

Point Player::facing() const {
    switch (direction_) {
        case Direction::Up: return {0, -1};
        case Direction::Down: return {0, 1};
        case Direction::Left: return {-1, 0};
        case Direction::Right: return {1, 0};
    }
    return {0, 1};
}

void Player::tickKnockback(const TileMap& map, std::span<const Rect> obstacles) {
    // Mob.tick: move half the remaining knockback each tick, shrinking it by one step (6 -> 9 px over 6 ticks).
    if (knockbackX_ != 0) {
        moveX(static_cast<float>(knockbackX_ / 2), map, obstacles);
        knockbackX_ -= knockbackX_ > 0 ? 1 : -1;
    }
    if (knockbackY_ != 0) {
        moveY(static_cast<float>(knockbackY_ / 2), map, obstacles);
        knockbackY_ -= knockbackY_ > 0 ? 1 : -1;
    }
}

void Player::tickEnergy() {
    // Based on Minicraft's stamina recharge: running out triggers a 40-tick pause, then one bolt comes back every
    // ~31 ticks (about 2 per second; a full refill takes ~5 s).
    if (energy_ <= 0 && energyRechargeDelay_ == 0 && energyRecharge_ == 0) {
        energyRechargeDelay_ = kExhaustedDelayTicks;
    }
    if (energyRechargeDelay_ > 0 && energy_ < kMaxEnergy) --energyRechargeDelay_;
    if (energyRechargeDelay_ == 0) {
        ++energyRecharge_;
        if (swimming_) energyRecharge_ = 0;  // no recharge while swimming
        while (energyRecharge_ > kTicksPerBolt) {
            energyRecharge_ -= kTicksPerBolt;
            if (energy_ < kMaxEnergy) ++energy_;
        }
    }
}

Point Player::interactionTile() const {
    // Minicraft measures from the entity centre, which sits at (8, 11) inside the sprite, raised by 2 px.
    float px = x_ + 8.0f;
    float py = y_ + 9.0f;
    switch (direction_) {
        case Direction::Up: py -= kInteractDistance; break;
        case Direction::Down: py += kInteractDistance; break;
        case Direction::Left: px -= kInteractDistance; break;
        case Direction::Right: px += kInteractDistance; break;
    }
    return {tileIndex(px), tileIndex(py)};
}

void Player::restoreStats(int health, int energy, int hunger, std::optional<ItemType> armor, int armorPoints) {
    refillStats();
    health_ = health;
    energy_ = energy;
    hunger_ = hunger;
    armor_ = armor;
    armorPoints_ = armor ? armorPoints : 0;
    armorDamageBuffer_ = 0;
}

void Player::setPosition(float x, float y) {
    x_ = x;
    y_ = y;
}

void Player::tick(const PlayerInput& input, const TileMap& map, std::span<const Rect> obstacles, Events& events) {
    damageTaken_ = 0;
    if (attackTicks_ > 0) --attackTicks_;
    if (punchPoseTicks_ > 0) --punchPoseTicks_;

    // Swimming when the tile under the player's centre is water (Mob.isSwimming: Minicraft's centre is (8, 11)).
    const Vec2 middle = center();
    const int centerTileX = tileIndex(middle.x);
    const int centerTileY = tileIndex(middle.y);
    const Tile under = map.inBounds(centerTileX, centerTileY) ? map.tileAt(centerTileX, centerTileY) : Tile::Rock;
    inLava_ = under == Tile::Lava;
    swimming_ = under == Tile::Water || inLava_;

    ++ticks_;
    if (hurtTime_ > 0) --hurtTime_;
    tickKnockback(map, obstacles);
    tickEnergy();
    tickHunger();
    if (inLava_) hurt(kLavaDamage);  // Mob.tick: lava burns
    // Drowning, like Minicraft: once a second in water, pay a bolt, or a heart when out of energy.
    if (swimming_ && !inLava_ && ticks_ % kSwimDrainTicks == 0) {
        if (energy_ > 0) --energy_;
        else hurt(1);
    }
    if (damageTaken_ > 0) {
        events.number(damageTaken_, middle.x, middle.y, NumberStyle::PlayerDamage);
        events.sound(Sound::PlayerHurt);
    }

    const float dx = static_cast<float>(input.moveX);
    const float dy = static_cast<float>(input.moveY);
    if (dx == 0.0f && dy == 0.0f) {
        walkDistance_ = 0.0f;  // stand still on the first frame
        return;
    }

    // Face the axis being pressed; vertical wins when moving diagonally (up+right shows the up sprite).
    if (dy < 0.0f) direction_ = Direction::Up;
    else if (dy > 0.0f) direction_ = Direction::Down;
    else if (dx < 0.0f) direction_ = Direction::Left;
    else direction_ = Direction::Right;

    // Each axis moves at full speed, so diagonal movement is intentionally faster (sqrt(2)x), like Minicraft.
    const float step = kSpeed * (swimming_ ? kSwimSpeedFactor : 1.0f);
    // Resolve each axis separately so pushing diagonally into a wall slides along it.
    const float startX = x_;
    const float startY = y_;
    moveX(dx * step, map, obstacles);
    moveY(dy * step, map, obstacles);
    // Advance the walk cycle by the larger axis only: summing both would make diagonals animate twice as fast.
    const float walked = std::max(std::abs(x_ - startX), std::abs(y_ - startY));
    walkDistance_ += walked;
    stepAccumulator_ += walked;
    while (stepAccumulator_ >= 1.0f) {  // whole pixels walked count towards hunger
        stepAccumulator_ -= 1.0f;
        ++stepCount_;
    }
}

void Player::moveX(float delta, const TileMap& map, std::span<const Rect> obstacles) {
    const auto solid = [&](int tx, int ty) { return map.isSolidAt(tx, ty); };
    const float allowed = collision::allowedMoveX(hitbox(), delta, solid);
    x_ += collision::clampMoveX(hitbox(), allowed, obstacles);
}

void Player::moveY(float delta, const TileMap& map, std::span<const Rect> obstacles) {
    const auto solid = [&](int tx, int ty) { return map.isSolidAt(tx, ty); };
    const float allowed = collision::allowedMoveY(hitbox(), delta, solid);
    y_ += collision::clampMoveY(hitbox(), allowed, obstacles);
}

