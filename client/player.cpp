#include "player.h"

#include "camera.h"
#include "tile_map.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kSpeed = 60.0f;              // pixels per second (1 px per tick at 60 Hz, like Minicraft)
constexpr float kPixelsPerWalkFrame = 8.0f;  // switch walk frame every 8 pixels walked
constexpr float kTileSize = static_cast<float>(TileMap::kTileSize);
constexpr float kAttackDuration = 5.0f / 60.0f;  // a bare-hand punch lasts 5 ticks in Minicraft
constexpr float kSlashPiece = 8.0f;              // slash.png: [0] half of a horizontal arc, [1] half of a vertical arc
constexpr float kInteractDistance = 12.0f;       // Minicraft's INTERACT_DIST

SDL_Texture* loadTexture(SDL_Renderer* renderer, const std::string& path) {
    SDL_Surface* surface = SDL_LoadPNG(path.c_str());
    if (!surface) {
        SDL_Log("Failed to load %s: %s", path.c_str(), SDL_GetError());
        return nullptr;
    }
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_DestroySurface(surface);
    if (!texture) {
        SDL_Log("Failed to create texture for %s: %s", path.c_str(), SDL_GetError());
        return nullptr;
    }
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    return texture;
}

// Index of the tile containing a world coordinate.
int tileIndex(float worldValue) { return static_cast<int>(std::floor(worldValue / kTileSize)); }

// Tile range covered by the half-open span [start, end): the end edge touching a tile doesn't count as overlap.
int lastTileIndex(float start, float end) {
    const bool endsOnEdge = std::fmod(end, kTileSize) == 0.0f;
    return std::max(tileIndex(start), tileIndex(end) - (endsOnEdge ? 1 : 0));
}

}  // namespace

bool Player::load(SDL_Renderer* renderer, const std::string& spritePath, const std::string& slashPath) {
    texture_.reset(loadTexture(renderer, spritePath));
    slashTexture_.reset(loadTexture(renderer, slashPath));
    return texture_ && slashTexture_;
}

std::optional<SDL_Point> Player::attack(TileMap& map) {
    attackDirection_ = direction_;
    const SDL_Point target = interactionTile();
    const int damage = static_cast<int>(SDL_rand(3)) + 1;  // bare-hand punch: 1-3, like Minicraft
    if (map.hurtTile(target.x, target.y, damage)) {
        attackTimer_ = 0.0f;  // a hit shows the smash effect instead of the slash
        return target;
    }
    attackTimer_ = kAttackDuration;
    return std::nullopt;
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

void Player::setPosition(float x, float y) {
    x_ = x;
    y_ = y;
}

void Player::update(float dt, const bool* keys, const TileMap& map) {
    attackTimer_ = std::max(0.0f, attackTimer_ - dt);

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

    // Face the axis being pressed; vertical wins when moving diagonally (up+right shows the up sprite).
    if (dy < 0.0f) direction_ = Direction::Up;
    else if (dy > 0.0f) direction_ = Direction::Down;
    else if (dx < 0.0f) direction_ = Direction::Left;
    else direction_ = Direction::Right;

    // Each axis moves at full speed, so diagonal movement is intentionally faster (sqrt(2)x), like Minicraft.
    const float step = kSpeed * dt;
    // Resolve each axis separately so pushing diagonally into a wall slides along it.
    const float startX = x_;
    const float startY = y_;
    moveX(dx * step, map);
    moveY(dy * step, map);
    // Advance the walk cycle by the larger axis only: summing both would make diagonals animate twice as fast.
    walkDistance_ += std::max(std::abs(x_ - startX), std::abs(y_ - startY));
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

    const float x = camera.snap(x_) - camera.x();
    const float y = camera.snap(y_) - camera.y();
    const SDL_FRect source{static_cast<float>(column) * kSize, 0.0f, kSize, kSize};
    const SDL_FRect destination{x, y, kSize, kSize};
    SDL_RenderTextureRotated(renderer, texture_.get(), &source, &destination, 0.0, nullptr,
                             mirrored ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);

    if (isAttacking()) {
        drawSlash(renderer, x, y);
    }
}

void Player::drawSlash(SDL_Renderer* renderer, float x, float y) const {
    // Same placement and mirroring as Minicraft+ (Player.render): two 8x8 halves form an arc just outside the
    // sprite, on the side the player is facing.
    const auto piece = [&](int index, float px, float py, int flip) {
        const SDL_FRect source{static_cast<float>(index) * kSlashPiece, 0.0f, kSlashPiece, kSlashPiece};
        const SDL_FRect destination{px, py, kSlashPiece, kSlashPiece};
        SDL_RenderTextureRotated(renderer, slashTexture_.get(), &source, &destination, 0.0, nullptr,
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
