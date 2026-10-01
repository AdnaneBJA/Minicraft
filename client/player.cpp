#include "player.h"

#include "camera.h"
#include "tile_map.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kSpeed = 60.0f;              // pixels per second (1 px per tick at 60 Hz, like Minicraft)
constexpr float kSwimSpeedFactor = 0.5f;     // Minicraft skips every other movement tick in water
constexpr float kSwimOffsetY = 4.0f;         // the body sinks 4 px into the water
constexpr float kRippleX = 40.0f;            // hud.png cells (5,0) / (5,1): the two water ripple frames
constexpr float kPixelsPerWalkFrame = 8.0f;  // switch walk frame every 8 pixels walked
constexpr float kTileSize = static_cast<float>(TileMap::kTileSize);
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

// Index of the tile containing a world coordinate.
int tileIndex(float worldValue) { return static_cast<int>(std::floor(worldValue / kTileSize)); }

// Tile range covered by the half-open span [start, end): the end edge touching a tile doesn't count as overlap.
int lastTileIndex(float start, float end) {
    const bool endsOnEdge = std::fmod(end, kTileSize) == 0.0f;
    return std::max(tileIndex(start), tileIndex(end) - (endsOnEdge ? 1 : 0));
}

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
    attackDirection_ = direction_;
    attackTimer_ = 0.0f;
    punchHand_ = 1 - punchHand_;  // alternate hands, so held punches go left, right, left...
    punchPoseTimer_ = kAttackDuration;
    return true;
}

void Player::showSlash() { attackTimer_ = kAttackDuration; }

void Player::refillStats() {
    health_ = kMaxHealth;
    energy_ = kMaxEnergy;
    energyRecharge_ = 0;
    energyRechargeDelay_ = 0;
    hurtTime_ = 0;
}

void Player::hurt(int damage) {
    if (hurtTime_ > 0) return;
    health_ = std::max(0, health_ - damage);
    damageTaken_ += damage;
    hurtTime_ = kHurtTicks;
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

void Player::setPosition(float x, float y) {
    x_ = x;
    y_ = y;
}

int Player::update(float dt, const bool* keys, const TileMap& map) {
    damageTaken_ = 0;
    attackTimer_ = std::max(0.0f, attackTimer_ - dt);
    punchPoseTimer_ = std::max(0.0f, punchPoseTimer_ - dt);

    // Swimming when the tile under the player's centre is water (Mob.isSwimming: Minicraft's centre is (8, 11)).
    const SDL_FPoint middle = center();
    const int centerTileX = tileIndex(middle.x);
    const int centerTileY = tileIndex(middle.y);
    swimming_ = map.inBounds(centerTileX, centerTileY) && map.tileAt(centerTileX, centerTileY) == Tile::Water;

    statTickAccumulator_ += dt;
    while (statTickAccumulator_ >= kStatTick) {
        statTickAccumulator_ -= kStatTick;
        ++ticks_;
        if (hurtTime_ > 0) --hurtTime_;
        tickEnergy();
        // Drowning, like Minicraft: once a second in water, pay a bolt, or a heart when out of energy.
        if (swimming_ && ticks_ % kSwimDrainTicks == 0) {
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
    moveX(dx * step, map);
    moveY(dy * step, map);
    // Advance the walk cycle by the larger axis only: summing both would make diagonals animate twice as fast.
    walkDistance_ += std::max(std::abs(x_ - startX), std::abs(y_ - startY));
    return damageTaken_;
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
        const SDL_FRect headSource{static_cast<float>(column) * kSize, 0.0f, kSize, kSize / 2.0f};
        const SDL_FRect headDestination{x, y, kSize, kSize / 2.0f};
        SDL_RenderTextureRotated(renderer, sprite, &headSource, &headDestination, 0.0, nullptr, flip);
    } else {
        const SDL_FRect source{static_cast<float>(column) * kSize, 0.0f, kSize, kSize};
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
