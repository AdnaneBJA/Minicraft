#pragma once

#include "texture.h"

#include <SDL3/SDL.h>

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

    // Loads the sprite strip (16x16 frames: down, up, right 1, right 2) and hud.png (for the slash pieces).
    bool load(SDL_Renderer* renderer, const std::string& spritePath, const std::string& hudPath);

    void setPosition(float x, float y);
    void update(float dt, const bool* keys, const TileMap& map);
    void draw(SDL_Renderer* renderer, const Camera& camera) const;

    // Starts a punch in the facing direction if there is energy left, spending 1. Returns false when exhausted.
    bool tryPunch();
    // Shows the slash animation (a punch that didn't hit anything).
    void showSlash();
    bool isAttacking() const { return attackTimer_ > 0.0f; }
    // The tile a punch would hit: 12 px in front of the player's centre (Minicraft's INTERACT_DIST).
    SDL_Point interactionTile() const;

    int health() const { return health_; }
    int energy() const { return energy_; }
    // Ticks left in the pause after running out of energy (bolts blink meanwhile); 0 when not exhausted.
    int energyRechargeDelay() const { return energyRechargeDelay_; }
    void refillStats();

    // Sprite rectangle (what is drawn).
    SDL_FRect bounds() const { return {x_, y_, kSize, kSize}; }
    // Collision box: a small rectangle at the feet, like Minicraft (8x6), so the head can overlap trees.
    SDL_FRect hitbox() const { return {x_ + kHitboxX, y_ + kHitboxY, kHitboxWidth, kHitboxHeight}; }

private:
    enum class Direction { Down, Up, Left, Right };

    // Moves along one axis, stopping flush against the first solid tile in the way.
    void moveX(float delta, const TileMap& map);
    void moveY(float delta, const TileMap& map);
    // One 60 Hz tick of energy recharge (Minicraft's stamina rules).
    void tickEnergy();
    void drawSlash(SDL_Renderer* renderer, float x, float y) const;

    TexturePtr texture_;
    TexturePtr hudTexture_;
    float x_ = 0.0f;
    float y_ = 0.0f;
    float walkDistance_ = 0.0f;  // pixels walked; drives the 2-frame walk animation
    Direction direction_ = Direction::Down;
    float attackTimer_ = 0.0f;  // seconds left showing the slash
    Direction attackDirection_ = Direction::Down;

    int health_ = kMaxHealth;
    int energy_ = kMaxEnergy;
    int energyRecharge_ = 0;       // ticks accumulated towards the next bolt
    int energyRechargeDelay_ = 0;  // exhaustion pause in ticks
    float statTickAccumulator_ = 0.0f;
};
