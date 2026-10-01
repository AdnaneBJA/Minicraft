#pragma once

#include <SDL3/SDL.h>

#include <memory>
#include <string>

class Camera;

class Player {
public:
    // Loads the sprite strip (16x16 frames: down, up, right 1, right 2). Returns false on failure.
    bool load(SDL_Renderer* renderer, const std::string& spritePath);

    void setPosition(float x, float y);
    void update(float dt, const bool* keys, float worldWidth, float worldHeight);
    void draw(SDL_Renderer* renderer, const Camera& camera) const;

    SDL_FRect bounds() const { return {x_, y_, kSize, kSize}; }

    static constexpr float kSize = 16.0f;

private:
    enum class Direction { Down, Up, Left, Right };

    struct TextureDeleter {
        void operator()(SDL_Texture* texture) const { SDL_DestroyTexture(texture); }
    };

    std::unique_ptr<SDL_Texture, TextureDeleter> texture_;
    float x_ = 0.0f;
    float y_ = 0.0f;
    float walkDistance_ = 0.0f;  // pixels walked; drives the 2-frame walk animation
    Direction direction_ = Direction::Down;
};
