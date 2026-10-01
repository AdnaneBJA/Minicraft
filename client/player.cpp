#include "player.h"

#include "camera.h"
#include "tile_map.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kSpeed = 60.0f;              // pixels per second (1 px per tick at 60 Hz, like Minicraft)
constexpr float kPixelsPerWalkFrame = 8.0f;  // switch walk frame every 8 pixels walked
constexpr float kTileSize = static_cast<float>(TileMap::kTileSize);

// Index of the tile containing a world coordinate.
int tileIndex(float worldValue) { return static_cast<int>(std::floor(worldValue / kTileSize)); }

// Tile range covered by the half-open span [start, end): the end edge touching a tile doesn't count as overlap.
int lastTileIndex(float start, float end) {
    const bool endsOnEdge = std::fmod(end, kTileSize) == 0.0f;
    return std::max(tileIndex(start), tileIndex(end) - (endsOnEdge ? 1 : 0));
}

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

void Player::update(float dt, const bool* keys, const TileMap& map) {
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
    // Resolve each axis separately so pushing diagonally into a wall slides along it.
    const float startX = x_;
    const float startY = y_;
    moveX(dx / length * step, map);
    moveY(dy / length * step, map);
    walkDistance_ += std::abs(x_ - startX) + std::abs(y_ - startY);
}

void Player::moveX(float delta, const TileMap& map) {
    if (delta == 0.0f) return;
    const SDL_FRect box = hitbox();
    const float left = box.x + delta;
    const float right = left + box.w;
    const int firstRow = tileIndex(box.y);
    const int lastRow = lastTileIndex(box.y, box.y + box.h);
    // Only the column the leading edge moves into can newly block us.
    const int column = delta > 0.0f ? lastTileIndex(left, right) : tileIndex(left);
    for (int row = firstRow; row <= lastRow; ++row) {
        if (map.isSolidAt(column, row)) {
            // Stop flush against the tile edge.
            const float edge = delta > 0.0f ? static_cast<float>(column) * kTileSize - box.w
                                            : static_cast<float>(column + 1) * kTileSize;
            x_ = edge - kHitboxX;
            return;
        }
    }
    x_ += delta;
}

void Player::moveY(float delta, const TileMap& map) {
    if (delta == 0.0f) return;
    const SDL_FRect box = hitbox();
    const float top = box.y + delta;
    const float bottom = top + box.h;
    const int firstColumn = tileIndex(box.x);
    const int lastColumn = lastTileIndex(box.x, box.x + box.w);
    const int row = delta > 0.0f ? lastTileIndex(top, bottom) : tileIndex(top);
    for (int column = firstColumn; column <= lastColumn; ++column) {
        if (map.isSolidAt(column, row)) {
            const float edge = delta > 0.0f ? static_cast<float>(row) * kTileSize - box.h
                                            : static_cast<float>(row + 1) * kTileSize;
            y_ = edge - kHitboxY;
            return;
        }
    }
    y_ += delta;
}

void Player::draw(SDL_Renderer* renderer, const Camera& camera) const {
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
    const SDL_FRect destination{camera.snap(x_) - camera.x(), camera.snap(y_) - camera.y(), kSize, kSize};
    SDL_RenderTextureRotated(renderer, texture_.get(), &source, &destination, 0.0, nullptr,
                             mirrored ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
}
