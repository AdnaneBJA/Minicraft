#include "player.h"

#include "audio.h"
#include "camera.h"
#include "collision.h"
#include "effects.h"
#include "tile_map.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kSpeed = 60.0f;              // pixels per second (1 px per tick at 60 Hz, like Minicraft)
constexpr float kSwimSpeedFactor = 0.5f;     // Minicraft skips every other movement tick in water
constexpr float kSwimOffsetY = 4.0f;         // the body sinks 4 px into the water
constexpr float kRippleX = 40.0f;            // hud.png cells (5,0) / (5,1): the two water ripple frames
constexpr float kPixelsPerWalkFrame = 8.0f;  // switch walk frame every 8 pixels walked
constexpr float kAttackDuration = 5.0f / 60.0f;  // a bare-hand punch lasts 5 ticks in Minicraft
constexpr float kSlashPiece = 8.0f;              // hud.png cells (3,0) / (4,0): halves of a horizontal / vertical arc
constexpr float kSlashX = 24.0f;                 // x of cell (3,0) in hud.png
constexpr float kInteractDistance = 12.0f;       // Minicraft's INTERACT_DIST
constexpr float kStatTick = 1.0f / 60.0f;
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

constexpr SDL_Color kPlayerDamageColor{255, 0, 204, 255};   // Minicraft: Color.get(-1, 504)
constexpr SDL_Color kArmorDamageColor{153, 153, 153, 255};  // Color.GRAY
constexpr float kAttackRange = 20.0f;      // Minicraft's ATTACK_DIST: how far a punch reaches for mobs

using collision::tileIndex;

}  // namespace

bool Player::load(SDL_Renderer* renderer, const std::string& spritePath, const std::string& hudPath) {
    texture_ = loadTexture(renderer, spritePath);
    flashTexture_ = loadTexture(renderer, spritePath, true);
    hudTexture_ = loadTexture(renderer, hudPath);
    return texture_ && flashTexture_ && hudTexture_;
}

bool Player::tryPunch() {
    if (energy_ <= 0) return false;  // Minicraft only allows attacking with stamina left
    --energy_;
    // Like Minicraft's attack, punching restarts the recharge count: no energy comes back while punching, and
    // running out triggers the exhaustion pause with the blinking bolts.
    energyRecharge_ = 0;
    attackDirection_ = direction_;
    attackTimer_ = 0.0f;
    punchHand_ = 1 - punchHand_;  // alternate hands, so held punches go left, right, left...
    punchPoseTimer_ = kAttackDuration;
    return true;
}

bool Player::payEnergy(int cost) {
    if (energy_ <= 0) return false;
    energy_ -= std::min(energy_, std::max(0, cost));
    return true;
}

void Player::showSlash() { attackTimer_ = kAttackDuration; }

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

bool Player::takeHit(int damage, int directionX, int directionY, Effects& effects, Audio& audio) {
    if (hurtTime_ > 0 || damage <= 0) return false;
    const SDL_FPoint c = center();
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
        effects.addDamageNumber(damage, c.x, c.y, kArmorDamageColor);
        armorPoints_ -= damage;
        if (armorPoints_ <= 0) {
            healthDamage -= armorPoints_;  // what the armour could not take
            removeArmor();
        }
    }
    if (healthDamage > 0) {
        effects.addDamageNumber(healthDamage, c.x, c.y - (armor_ ? 6.0f : 0.0f), kPlayerDamageColor);
        health_ = std::max(0, health_ - healthDamage);
    }
    audio.play(Sound::PlayerHurt);
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

SDL_FRect Player::attackBox() const {
    // Minicraft's Player.getInteractionBox(ATTACK_DIST): from 4 to 20 px ahead of the centre (raised by 2 px),
    // and 8 px wide to one side.
    const SDL_FPoint c = center();
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

SDL_Point Player::facing() const {
    switch (direction_) {
        case Direction::Up: return {0, -1};
        case Direction::Down: return {0, 1};
        case Direction::Left: return {-1, 0};
        case Direction::Right: return {1, 0};
    }
    return {0, 1};
}

void Player::tickKnockback(const TileMap& map, std::span<const SDL_FRect> obstacles) {
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

SDL_Point Player::interactionTile() const {
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

int Player::update(float dt, const bool* keys, const TileMap& map, std::span<const SDL_FRect> obstacles) {
    damageTaken_ = 0;
    attackTimer_ = std::max(0.0f, attackTimer_ - dt);
    punchPoseTimer_ = std::max(0.0f, punchPoseTimer_ - dt);

    // Swimming when the tile under the player's centre is water (Mob.isSwimming: Minicraft's centre is (8, 11)).
    const SDL_FPoint middle = center();
    const int centerTileX = tileIndex(middle.x);
    const int centerTileY = tileIndex(middle.y);
    const Tile under = map.inBounds(centerTileX, centerTileY) ? map.tileAt(centerTileX, centerTileY) : Tile::Rock;
    inLava_ = under == Tile::Lava;
    swimming_ = under == Tile::Water || inLava_;

    statTickAccumulator_ += dt;
    while (statTickAccumulator_ >= kStatTick) {
        statTickAccumulator_ -= kStatTick;
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
    }

    float dx = 0.0f;
    float dy = 0.0f;
    if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP]) dy -= 1.0f;
    if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN]) dy += 1.0f;
    if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT]) dx -= 1.0f;
    if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) dx += 1.0f;

    if (dx == 0.0f && dy == 0.0f) {
        walkDistance_ = 0.0f;  // stand still on the first frame
        return damageTaken_;
    }

    // Face the axis being pressed; vertical wins when moving diagonally (up+right shows the up sprite).
    if (dy < 0.0f) direction_ = Direction::Up;
    else if (dy > 0.0f) direction_ = Direction::Down;
    else if (dx < 0.0f) direction_ = Direction::Left;
    else direction_ = Direction::Right;

    // Each axis moves at full speed, so diagonal movement is intentionally faster (sqrt(2)x), like Minicraft.
    const float step = kSpeed * (swimming_ ? kSwimSpeedFactor : 1.0f) * dt;
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
    return damageTaken_;
}

void Player::moveX(float delta, const TileMap& map, std::span<const SDL_FRect> obstacles) {
    const auto solid = [&](int tx, int ty) { return map.isSolidAt(tx, ty); };
    const float allowed = collision::allowedMoveX(hitbox(), delta, solid);
    x_ += collision::clampMoveX(hitbox(), allowed, obstacles);
}

void Player::moveY(float delta, const TileMap& map, std::span<const SDL_FRect> obstacles) {
    const auto solid = [&](int tx, int ty) { return map.isSolidAt(tx, ty); };
    const float allowed = collision::allowedMoveY(hitbox(), delta, solid);
    y_ += collision::clampMoveY(hitbox(), allowed, obstacles);
}

void Player::draw(SDL_Renderer* renderer, const Camera& camera) const {
    // The sheet only has down/up/right frames; the rest are horizontal mirrors (same trick as Minicraft):
    //   down: [down, down mirrored]   up: [up, up mirrored]
    //   right: [right1, right2]       left: [right1 mirrored, right2 mirrored]
    // Right after a punch, show the punching hand instead of the walk cycle: the alternate frame (the mirrored
    // sprite for up/down, the second side frame for left/right) puts the other arm forward.
    const int walkFrame = punchPoseTimer_ > 0.0f ? punchHand_
                                                 : static_cast<int>(walkDistance_ / kPixelsPerWalkFrame) % 2;
    int column = 0;
    bool mirrored = false;
    switch (direction_) {
        case Direction::Down:
            column = 0;
            mirrored = walkFrame == 1;
            break;
        case Direction::Up:
            column = 1;
            mirrored = walkFrame == 1;
            break;
        case Direction::Right:
            column = 2 + walkFrame;
            break;
        case Direction::Left:
            column = 2 + walkFrame;
            mirrored = true;
            break;
    }

    // Carrying furniture: the same frames from the second row, with both arms raised.
    const float row = isCarryingFurniture() ? kSize : 0.0f;
    const float x = camera.snap(x_) - camera.x();
    float y = camera.snap(y_) - camera.y();
    const SDL_FlipMode flip = mirrored ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
    // Just hurt: draw the white silhouette instead (Minicraft: hurtTime > playerHurtTime - 10).
    SDL_Texture* sprite = hurtTime_ > kHurtTicks - kHurtFlashTicks ? flashTexture_.get() : texture_.get();
    if (swimming_) {
        // Like Minicraft's Player.render: sink 4 px, draw the water ripple (alternating every 8 ticks, right half
        // mirrored) and then only the top half of the sprite, so just the head shows above the water.
        y += kSwimOffsetY;
        const float rippleY = (ticks_ / 8) % 2 == 0 ? 0.0f : 8.0f;
        const SDL_FRect rippleSource{kRippleX, rippleY, 8.0f, 8.0f};
        const SDL_FRect rippleLeft{x, y + 3.0f, 8.0f, 8.0f};
        const SDL_FRect rippleRight{x + 8.0f, y + 3.0f, 8.0f, 8.0f};
        SDL_RenderTexture(renderer, hudTexture_.get(), &rippleSource, &rippleLeft);
        SDL_RenderTextureRotated(renderer, hudTexture_.get(), &rippleSource, &rippleRight, 0.0, nullptr,
                                 SDL_FLIP_HORIZONTAL);
        const SDL_FRect headSource{static_cast<float>(column) * kSize, row, kSize, kSize / 2.0f};
        const SDL_FRect headDestination{x, y, kSize, kSize / 2.0f};
        SDL_RenderTextureRotated(renderer, sprite, &headSource, &headDestination, 0.0, nullptr, flip);
    } else {
        const SDL_FRect source{static_cast<float>(column) * kSize, row, kSize, kSize};
        const SDL_FRect destination{x, y, kSize, kSize};
        SDL_RenderTextureRotated(renderer, sprite, &source, &destination, 0.0, nullptr, flip);
    }

    if (isAttacking()) {
        drawSlash(renderer, x, y);
    }
}

void Player::drawSlash(SDL_Renderer* renderer, float x, float y) const {
    // Same placement and mirroring as Minicraft+ (Player.render): two 8x8 halves form an arc just outside the
    // sprite, on the side the player is facing.
    const auto piece = [&](int index, float px, float py, int flip) {
        const SDL_FRect source{kSlashX + static_cast<float>(index) * kSlashPiece, 0.0f, kSlashPiece, kSlashPiece};
        const SDL_FRect destination{px, py, kSlashPiece, kSlashPiece};
        SDL_RenderTextureRotated(renderer, hudTexture_.get(), &source, &destination, 0.0, nullptr,
                                 static_cast<SDL_FlipMode>(flip));
    };
    constexpr int none = SDL_FLIP_NONE;
    constexpr int flipX = SDL_FLIP_HORIZONTAL;
    constexpr int flipY = SDL_FLIP_VERTICAL;
    switch (attackDirection_) {
        case Direction::Up:
            piece(0, x, y - 4.0f, none);
            piece(0, x + 8.0f, y - 4.0f, flipX);
            break;
        case Direction::Down:
            piece(0, x, y + 12.0f, flipY);
            piece(0, x + 8.0f, y + 12.0f, flipX | flipY);
            break;
        case Direction::Left:
            piece(1, x - 4.0f, y, flipX);
            piece(1, x - 4.0f, y + 8.0f, flipX | flipY);
            break;
        case Direction::Right:
            piece(1, x + 12.0f, y, none);
            piece(1, x + 12.0f, y + 8.0f, flipY);
            break;
    }
}
