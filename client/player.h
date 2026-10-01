#pragma once

#include <SDL3/SDL.h>

#include <memory>
#include <string>

class Camera;
class TileMap;

class Player {
public:
    // Loads the sprite strip (16x16 frames: down, up, right 1, right 2) and the attack slash (two 8x8 pieces).
    // Returns false on failure.
    bool load(SDL_Renderer* renderer, const std::string& spritePath, const std::string& slashPath);

    void setPosition(float x, float y);
    void update(float dt, const bool* keys, const TileMap& map);
    // Starts a punch in the direction the player is facing. The slash shows for a short moment.
    void attack();
    bool isAttacking() const { return attackTimer_ > 0.0f; }
    void draw(SDL_Renderer* renderer, const Camera& camera) const;

    // Sprite rectangle (what is drawn).
    SDL_FRect bounds() const { return {x_, y_, kSize, kSize}; }
    // Collision box: a small rectangle at the feet, like Minicraft (8x6), so the head can overlap trees.
    SDL_FRect hitbox() const { return {x_ + kHitboxX, y_ + kHitboxY, kHitboxWidth, kHitboxHeight}; }

    static constexpr float kSize = 16.0f;
    static constexpr float kHitboxX = 4.0f;
    static constexpr float kHitboxY = 8.0f;
    static constexpr float kHitboxWidth = 8.0f;
    static constexpr float kHitboxHeight = 6.0f;

private:
    enum class Direction { Down, Up, Left, Right };

    // Moves along one axis, stopping flush against the first solid tile in the way.
    void moveX(float delta, const TileMap& map);
    void moveY(float delta, const TileMap& map);

    struct TextureDeleter {
        void operator()(SDL_Texture* texture) const { SDL_DestroyTexture(texture); }
    };

    void drawSlash(SDL_Renderer* renderer, float x, float y) const;

    std::unique_ptr<SDL_Texture, TextureDeleter> texture_;
    std::unique_ptr<SDL_Texture, TextureDeleter> slashTexture_;
    float x_ = 0.0f;
    float y_ = 0.0f;
    float walkDistance_ = 0.0f;  // pixels walked; drives the 2-frame walk animation
    Direction direction_ = Direction::Down;
    float attackTimer_ = 0.0f;  // seconds left showing the slash
    Direction attackDirection_ = Direction::Down;
};
