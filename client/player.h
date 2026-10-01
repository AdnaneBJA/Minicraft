#pragma once

#include "items.h"
#include "texture.h"

#include <SDL3/SDL.h>

#include <optional>
#include <span>
#include <string>

class Camera;
class TileMap;

class Player {
public:
    static constexpr float kSize = 16.0f;
    static constexpr float kHitboxX = 4.0f;
    static constexpr float kHitboxY = 8.0f;
    static constexpr float kHitboxWidth = 8.0f;
    static constexpr float kHitboxHeight = 6.0f;
    static constexpr int kMaxHealth = 10;
    static constexpr int kMaxEnergy = 10;

    // Loads the sprite sheet (16x16 frames: down, up, right 1, right 2; walking on the top row, carrying with the
    // arms raised on the bottom row) and hud.png (for the slash pieces).
    bool load(SDL_Renderer* renderer, const std::string& spritePath, const std::string& hudPath);

    void setPosition(float x, float y);
    // Moves the player and runs its 60 Hz stat ticks; `obstacles` (furniture) block movement like solid tiles.
    // Returns the health lost during this update (drowning).
    int update(float dt, const bool* keys, const TileMap& map, std::span<const SDL_FRect> obstacles);
    void draw(SDL_Renderer* renderer, const Camera& camera) const;

    // Starts a punch in the facing direction if there is energy left, spending 1. Returns false when exhausted.
    bool tryPunch();
    // Shows the slash animation (a punch that didn't hit anything).
    void showSlash();
    bool isAttacking() const { return attackTimer_ > 0.0f; }
    // In water (the tile under the player's centre): half speed, and only the head is drawn.
    bool isSwimming() const { return swimming_; }
    // The tile a punch would hit: 12 px in front of the player's centre (Minicraft's INTERACT_DIST).
    SDL_Point interactionTile() const;
    // The area a punch reaches for mobs (Minicraft's interaction box with ATTACK_DIST = 20 px).
    SDL_FRect attackBox() const;
    // Unit vector of the facing direction (e.g. right = {1, 0}).
    SDL_Point facing() const;

    // Hit by a mob: loses health and is knocked back along (directionX, directionY), unless still in the hurt
    // cooldown. Returns true if the hit landed.
    bool takeHit(int damage, int directionX, int directionY);

    // The item in the player's hand (Minicraft's activeItem), taken out of the inventory.
    const std::optional<Inventory::Stack>& heldItem() const { return heldItem_; }
    void setHeldItem(std::optional<Inventory::Stack> item) { heldItem_ = item; }
    // Holding furniture: the player walks with it raised over the head (Minicraft's carrySprites).
    bool isCarryingFurniture() const { return heldItem_ && isFurniture(heldItem_->type); }
    // Top-left of the carried furniture's 16x16 sprite: 12 px above the player sprite (Minicraft: furniture.y =
    // yo - 4), sinking with the player in water.
    SDL_FPoint carriedFurniturePosition() const { return {x_, y_ - 12.0f + (swimming_ ? 4.0f : 0.0f)}; }

    int health() const { return health_; }
    int energy() const { return energy_; }
    // Ticks left in the pause after running out of energy (bolts blink meanwhile); 0 when not exhausted.
    int energyRechargeDelay() const { return energyRechargeDelay_; }
    void refillStats();
    bool isDead() const { return health_ <= 0; }
    // Minicraft's entity centre (8, 11 inside the sprite), used for effects attached to the player.
    SDL_FPoint center() const { return {x_ + 8.0f, y_ + 11.0f}; }

    // Sprite rectangle (what is drawn).
    SDL_FRect bounds() const { return {x_, y_, kSize, kSize}; }
    // Collision box: a small rectangle at the feet, like Minicraft (8x6), so the head can overlap trees.
    SDL_FRect hitbox() const { return {x_ + kHitboxX, y_ + kHitboxY, kHitboxWidth, kHitboxHeight}; }

private:
    enum class Direction { Down, Up, Left, Right };

    // Moves along one axis, stopping flush against the first solid tile in the way.
    void moveX(float delta, const TileMap& map, std::span<const SDL_FRect> obstacles);
    void moveY(float delta, const TileMap& map, std::span<const SDL_FRect> obstacles);
    // One 60 Hz tick of energy recharge (Minicraft's stamina rules).
    void tickEnergy();
    // Loses health unless still in the hurt cooldown; starts the cooldown and the white flash.
    void hurt(int damage);
    void tickKnockback(const TileMap& map, std::span<const SDL_FRect> obstacles);
    void drawSlash(SDL_Renderer* renderer, float x, float y) const;

    TexturePtr texture_;
    TexturePtr hudTexture_;
    TexturePtr flashTexture_;  // white silhouette of the sprite strip, shown just after being hurt
    float x_ = 0.0f;
    float y_ = 0.0f;
    float walkDistance_ = 0.0f;  // pixels walked; drives the 2-frame walk animation
    Direction direction_ = Direction::Down;
    float attackTimer_ = 0.0f;     // seconds left showing the slash
    float punchPoseTimer_ = 0.0f;  // seconds left showing the punching hand (hit or miss)
    int punchHand_ = 0;            // 0/1: which hand the last punch used; alternates every punch
    Direction attackDirection_ = Direction::Down;

    int health_ = kMaxHealth;
    int energy_ = kMaxEnergy;
    int energyRecharge_ = 0;       // ticks accumulated towards the next bolt
    int energyRechargeDelay_ = 0;  // exhaustion pause in ticks
    float statTickAccumulator_ = 0.0f;
    bool swimming_ = false;
    int hurtTime_ = 0;     // ticks of hurt cooldown left (no more damage meanwhile)
    int knockbackX_ = 0;   // remaining knockback "steps" (Minicraft's xKnockback / yKnockback)
    int knockbackY_ = 0;
    int damageTaken_ = 0;  // health lost since the start of the current update()
    int ticks_ = 0;  // 60 Hz ticks since start; drives the swimming ripple animation
    std::optional<Inventory::Stack> heldItem_;
};
