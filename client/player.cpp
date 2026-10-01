#include "player.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kSpeed = 60.0f;              // pixels per second (1 px per tick at 60 Hz, like Minicraft)
constexpr float kPixelsPerWalkFrame = 8.0f;  // switch walk frame every 8 pixels walked

}  // namespace

bool Player::load(SDL_Renderer* renderer, const std::string& spritePath) {
    SDL_Surface* surface = SDL_LoadPNG(spritePath.c_str());
    if (!surface) {
        SDL_Log("Failed to load %s: %s", spritePath.c_str(), SDL_GetError());
        return false;
    }
    texture_.reset(SDL_CreateTextureFromSurface(renderer, surface));
    SDL_DestroySurface(surface);
    if (!texture_) {
        SDL_Log("Failed to create player texture: %s", SDL_GetError());
        return false;
    }
    SDL_SetTextureScaleMode(texture_.get(), SDL_SCALEMODE_NEAREST);
    return true;
}

void Player::setPosition(float x, float y) {
    x_ = x;
    y_ = y;
}

void Player::update(float dt, const bool* keys, float worldWidth, float worldHeight) {
    float dx = 0.0f;
    float dy = 0.0f;
    if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP]) dy -= 1.0f;
    if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN]) dy += 1.0f;
    if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT]) dx -= 1.0f;
    if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) dx += 1.0f;

    if (dx == 0.0f && dy == 0.0f) {
        walkDistance_ = 0.0f;  // stand still on the first frame
        return;
    }

    // Face the axis being pressed; horizontal wins when moving diagonally.
    if (dx < 0.0f) direction_ = Direction::Left;
    else if (dx > 0.0f) direction_ = Direction::Right;
    else if (dy < 0.0f) direction_ = Direction::Up;
    else direction_ = Direction::Down;

    // Normalise so diagonal movement isn't faster.
    const float length = std::sqrt(dx * dx + dy * dy);
    const float step = kSpeed * dt;
    x_ = std::clamp(x_ + dx / length * step, 0.0f, worldWidth - kSize);
    y_ = std::clamp(y_ + dy / length * step, 0.0f, worldHeight - kSize);
    walkDistance_ += step;
}

void Player::draw(SDL_Renderer* renderer) const {
    // The sheet only has down/up/right frames; the rest are horizontal mirrors (same trick as Minicraft):
    //   down: [down, down mirrored]   up: [up, up mirrored]
    //   right: [right1, right2]       left: [right1 mirrored, right2 mirrored]
    const int walkFrame = static_cast<int>(walkDistance_ / kPixelsPerWalkFrame) % 2;
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

    const SDL_FRect source{static_cast<float>(column) * kSize, 0.0f, kSize, kSize};
    const SDL_FRect destination{std::round(x_), std::round(y_), kSize, kSize};
    SDL_RenderTextureRotated(renderer, texture_.get(), &source, &destination, 0.0, nullptr,
                             mirrored ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
}
