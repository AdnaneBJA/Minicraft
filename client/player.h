#pragma once

#include <SDL3/SDL.h>

#include <memory>
#include <string>

class Camera;
class TileMap;

class Player {
public:
    // Loads the sprite strip (16x16 frames: down, up, right 1, right 2). Returns false on failure.
    bool load(SDL_Renderer* renderer, const std::string& spritePath);

    void setPosition(float x, float y);
    void update(float dt, const bool* keys, const TileMap& map);
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

    std::unique_ptr<SDL_Texture, TextureDeleter> texture_;
    float x_ = 0.0f;
    float y_ = 0.0f;
    float walkDistance_ = 0.0f;  // pixels walked; drives the 2-frame walk animation
    Direction direction_ = Direction::Down;
};
