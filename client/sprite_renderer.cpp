#include "sprite_renderer.h"

#include "camera.h"
#include "dropped_items.h"
#include "furniture.h"
#include "item_icons.h"
#include "mobs.h"
#include "player.h"
#include "projectiles.h"

#include <cmath>

namespace {

constexpr float kSize = 16.0f;
constexpr float kCell = 8.0f;
constexpr float kPixelsPerWalkFrame = 8.0f;  // switch walk frame every 8 pixels walked
constexpr float kSwimOffsetY = 4.0f;         // the body sinks 4 px into the water
constexpr float kRippleX = 40.0f;            // hud.png cells (5,0) / (5,1): the two water ripple frames
constexpr float kSlashX = 24.0f;             // hud.png cells (3,0) / (4,0): halves of a horizontal / vertical arc
constexpr int kDeathChestColumn = 8;
constexpr float kSparkColumn = 4.0f;  // the spark sits after the four arrow frames in projectiles.png

const char* mobSheet(MobKind kind) {
    switch (kind) {
        case MobKind::Zombie: return "zombie.png";
        case MobKind::Cow: return "cow.png";
        case MobKind::Pig: return "pig.png";
        case MobKind::Sheep: return "sheep.png";
        case MobKind::Skeleton: return "skeleton.png";
        case MobKind::Slime: return "slime.png";
        case MobKind::Creeper: return "creeper.png";
        case MobKind::Snake: return "snake.png";
        case MobKind::AirWizard: return "air_wizard.png";
    }
    return "";
}

// Column of each furniture type in furniture.png.
int furnitureColumn(ItemType type) {
    switch (type) {
        case ItemType::Workbench: return 0;
        case ItemType::Furnace: return 1;
        case ItemType::Oven: return 2;
        case ItemType::Anvil: return 3;
        case ItemType::Chest: return 4;
        case ItemType::Lantern: return 5;
        case ItemType::Loom: return 6;
        case ItemType::Bed: return 7;
        default: return 0;
    }
}

int arrowFrame(Point direction) {
    if (direction.x < 0) return 1;
    if (direction.y < 0) return 2;
    if (direction.y > 0) return 3;
    return 0;
}

// The sheets only have down/up/right frames; the rest are horizontal mirrors (same trick as Minicraft):
//   down: [down, down mirrored]   up: [up, up mirrored]
//   right: [right1, right2]       left: [right1 mirrored, right2 mirrored]
template <typename Direction>
void walkFrame(Direction direction, int frame, int& column, bool& mirrored) {
    switch (direction) {
        case Direction::Down: column = 0; mirrored = frame == 1; break;
        case Direction::Up: column = 1; mirrored = frame == 1; break;
        case Direction::Right: column = 2 + frame; mirrored = false; break;
        case Direction::Left: column = 2 + frame; mirrored = true; break;
    }
}

}  // namespace

bool SpriteRenderer::load(SDL_Renderer* renderer, const std::string& spriteDirectory) {
    player_ = loadTexture(renderer, spriteDirectory + "player.png");
    playerFlash_ = loadTexture(renderer, spriteDirectory + "player.png", true);
    hud_ = loadTexture(renderer, spriteDirectory + "hud.png");
    furniture_ = loadTexture(renderer, spriteDirectory + "furniture.png");
    projectiles_ = loadTexture(renderer, spriteDirectory + "projectiles.png");
    if (!player_ || !playerFlash_ || !hud_ || !furniture_ || !projectiles_) return false;
    for (int i = 0; i < kMobKinds; ++i) {
        const std::string path = spriteDirectory + mobSheet(static_cast<MobKind>(i));
        mobs_[static_cast<std::size_t>(i)] = loadTexture(renderer, path);
        mobFlashes_[static_cast<std::size_t>(i)] = loadTexture(renderer, path, true);
        if (!mobs_[static_cast<std::size_t>(i)] || !mobFlashes_[static_cast<std::size_t>(i)]) return false;
    }
    return true;
}

void SpriteRenderer::drawPlayer(SDL_Renderer* renderer, const Camera& camera, const Player& player) const {
    // Right after a punch, show the punching hand instead of the walk cycle: the alternate frame (the mirrored
    // sprite for up/down, the second side frame for left/right) puts the other arm forward.
    const int frame = player.isPunching() ? player.punchHand()
                                          : static_cast<int>(player.walkDistance() / kPixelsPerWalkFrame) % 2;
    int column = 0;
    bool mirrored = false;
    walkFrame(player.direction(), frame, column, mirrored);

    // Carrying furniture: the same frames from the second row, with both arms raised.
    const float row = player.isCarryingFurniture() ? kSize : 0.0f;
    const Rect bounds = player.bounds();
    const float x = camera.snap(bounds.x) - camera.x();
    float y = camera.snap(bounds.y) - camera.y();
    const SDL_FlipMode flip = mirrored ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
    // Just hurt: draw the white silhouette instead (Minicraft: hurtTime > playerHurtTime - 10).
    SDL_Texture* sprite = player.isHurtFlashing() ? playerFlash_.get() : player_.get();
    if (player.isSwimming()) {
        // Like Minicraft's Player.render: sink 4 px, draw the water ripple (alternating every 8 ticks, right half
        // mirrored) and then only the top half of the sprite, so just the head shows above the water.
        y += kSwimOffsetY;
        const float rippleY = (player.ticks() / 8) % 2 == 0 ? 0.0f : 8.0f;
        const SDL_FRect rippleSource{kRippleX, rippleY, 8.0f, 8.0f};
        const SDL_FRect rippleLeft{x, y + 3.0f, 8.0f, 8.0f};
        const SDL_FRect rippleRight{x + 8.0f, y + 3.0f, 8.0f, 8.0f};
        SDL_RenderTexture(renderer, hud_.get(), &rippleSource, &rippleLeft);
        SDL_RenderTextureRotated(renderer, hud_.get(), &rippleSource, &rippleRight, 0.0, nullptr,
                                 SDL_FLIP_HORIZONTAL);
        const SDL_FRect headSource{static_cast<float>(column) * kSize, row, kSize, kSize / 2.0f};
        const SDL_FRect headDestination{x, y, kSize, kSize / 2.0f};
        SDL_RenderTextureRotated(renderer, sprite, &headSource, &headDestination, 0.0, nullptr, flip);
    } else {
        const SDL_FRect source{static_cast<float>(column) * kSize, row, kSize, kSize};
        const SDL_FRect destination{x, y, kSize, kSize};
        SDL_RenderTextureRotated(renderer, sprite, &source, &destination, 0.0, nullptr, flip);
    }

    if (player.isAttacking()) drawSlash(renderer, player, x, y);
}

void SpriteRenderer::drawSlash(SDL_Renderer* renderer, const Player& player, float x, float y) const {
    // Same placement and mirroring as Minicraft+ (Player.render): two 8x8 halves form an arc just outside the
    // sprite, on the side the player is facing.
    const auto piece = [&](int index, float px, float py, int flip) {
        const SDL_FRect source{kSlashX + static_cast<float>(index) * kCell, 0.0f, kCell, kCell};
        const SDL_FRect destination{px, py, kCell, kCell};
        SDL_RenderTextureRotated(renderer, hud_.get(), &source, &destination, 0.0, nullptr,
                                 static_cast<SDL_FlipMode>(flip));
    };
    constexpr int none = SDL_FLIP_NONE;
    constexpr int flipX = SDL_FLIP_HORIZONTAL;
    constexpr int flipY = SDL_FLIP_VERTICAL;
    switch (player.attackDirection()) {
        case Player::Direction::Up:
            piece(0, x, y - 4.0f, none);
            piece(0, x + 8.0f, y - 4.0f, flipX);
            break;
        case Player::Direction::Down:
            piece(0, x, y + 12.0f, flipY);
            piece(0, x + 8.0f, y + 12.0f, flipX | flipY);
            break;
        case Player::Direction::Left:
            piece(1, x - 4.0f, y, flipX);
            piece(1, x - 4.0f, y + 8.0f, flipX | flipY);
            break;
        case Player::Direction::Right:
            piece(1, x + 12.0f, y, none);
            piece(1, x + 12.0f, y + 8.0f, flipY);
            break;
    }
}

void SpriteRenderer::drawMobs(SDL_Renderer* renderer, const Camera& camera, const Mobs& mobs, float playerY,
                              bool behind) const {
    for (const auto& mob : mobs.all()) {
        if ((mob->center().y < playerY) == behind) drawMob(renderer, camera, *mob);
    }
}

void SpriteRenderer::drawMobFrame(SDL_Renderer* renderer, const Camera& camera, const Mob& mob, bool flash,
                                  int column, int row, bool mirrored, float raise) const {
    const auto kind = static_cast<std::size_t>(mob.kind());
    SDL_Texture* texture = flash ? mobFlashes_[kind].get() : mobs_[kind].get();
    const Vec2 position = mob.position();
    const SDL_FRect source{static_cast<float>(column) * kSize, static_cast<float>(row) * kSize, kSize, kSize};
    const SDL_FRect destination{std::floor(position.x) - camera.x(), std::floor(position.y) - raise - camera.y(),
                                kSize, kSize};
    SDL_RenderTextureRotated(renderer, texture, &source, &destination, 0.0, nullptr,
                             mirrored ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
}

void SpriteRenderer::drawMob(SDL_Renderer* renderer, const Camera& camera, const Mob& mob) const {
    // Each sheet has one 16 px row per mob level; mobs flash white while hurt (MobAi.render).
    const int frame = (mob.walkDistance() >> 3) & 1;
    switch (mob.kind()) {
        case MobKind::Slime: {
            // Two frames per level: on the ground, and in the air (drawn 4 px higher).
            const bool jumping = static_cast<const Slime&>(mob).isJumping();
            drawMobFrame(renderer, camera, mob, mob.isHurt(), jumping ? 1 : 0, mob.level() - 1, false,
                         jumping ? 4.0f : 0.0f);
            return;
        }
        case MobKind::Creeper: {
            // Two frames per level (standing and walking), always facing down; it flashes while the fuse burns.
            const bool flash = mob.isHurt() || static_cast<const Creeper&>(mob).isFlashing();
            drawMobFrame(renderer, camera, mob, flash, frame, mob.level() - 1, false);
            return;
        }
        case MobKind::AirWizard: {
            // Row 0 normally; row 1 (the angry colours) flickers in while it casts.
            const bool casting = static_cast<const AirWizard&>(mob).isCasting();
            int column = 0;
            bool mirrored = false;
            walkFrame(mob.direction(), frame, column, mirrored);
            drawMobFrame(renderer, camera, mob, mob.isHurt(), column, casting && (mob.ticks() / 4) % 2 == 0 ? 1 : 0,
                         mirrored);
            return;
        }
        default: {
            int column = 0;
            bool mirrored = false;
            walkFrame(mob.direction(), frame, column, mirrored);
            drawMobFrame(renderer, camera, mob, mob.isHurt(), column, mob.level() - 1, mirrored);
            return;
        }
    }
}

void SpriteRenderer::drawFurniture(SDL_Renderer* renderer, const Camera& camera, const Furniture& furniture,
                                   float playerY, bool behind) const {
    for (const Furniture::Piece& piece : furniture.all()) {
        if ((piece.y < playerY) == behind) {
            drawFurnitureSprite(renderer, camera, piece.type, piece.x - kSize / 2.0f, piece.y - kSize / 2.0f,
                                piece.deathChest);
        }
    }
}

void SpriteRenderer::drawFurnitureSprite(SDL_Renderer* renderer, const Camera& camera, ItemType type, float x,
                                         float y, bool deathChest) const {
    const int column = deathChest ? kDeathChestColumn : furnitureColumn(type);
    const SDL_FRect source{static_cast<float>(column) * kSize, 0.0f, kSize, kSize};
    const SDL_FRect destination{camera.snap(x) - camera.x(), camera.snap(y) - camera.y(), kSize, kSize};
    SDL_RenderTexture(renderer, furniture_.get(), &source, &destination);
}

void SpriteRenderer::drawDrops(SDL_Renderer* renderer, const Camera& camera, const DroppedItems& drops,
                               const ItemIcons& icons) const {
    for (const DroppedItems::Item& item : drops.items()) {
        // Blink before despawning: hidden every other 6-tick slice.
        if (item.age >= item.lifetime - DroppedItems::kBlinkTicks && (item.age / 6) % 2 == 0) continue;
        // Whole world pixels, like Minicraft's int positions.
        const float x = std::floor(item.motion.x) - 4.0f - camera.x();
        const float y = std::floor(item.motion.y) - 4.0f - camera.y();
        icons.drawShadow(renderer, item.type, x, y);
        icons.draw(renderer, item.type, x, y - std::floor(item.motion.z));
    }
}

void SpriteRenderer::drawProjectiles(SDL_Renderer* renderer, const Camera& camera,
                                     const Projectiles& projectiles) const {
    SDL_Texture* sheet = projectiles_.get();
    for (const Projectiles::Arrow& arrow : projectiles.arrows()) {
        const SDL_FRect source{static_cast<float>(arrowFrame(arrow.direction)) * kCell, 0.0f, kCell, kCell};
        const SDL_FRect destination{std::floor(arrow.x) - 4.0f - camera.x(), std::floor(arrow.y) - 4.0f - camera.y(),
                                    kCell, kCell};
        SDL_RenderTexture(renderer, sheet, &source, &destination);
    }
    const SDL_FRect sparkSource{kSparkColumn * kCell, 0.0f, kCell, kCell};
    for (const Projectiles::Spark& spark : projectiles.sparks()) {
        // Spark.render: blinks during its last 2 seconds; a shadow on the ground under the spark.
        if (spark.age >= spark.lifetime - 120 && (spark.age / 6) % 2 == 0) continue;
        const float x = std::floor(spark.x) - 4.0f - camera.x();
        const float y = std::floor(spark.y) - 4.0f - camera.y();
        // Random mirroring makes it crackle (cosmetic, so the client's own random numbers are fine).
        const auto flip = static_cast<SDL_FlipMode>(SDL_rand(4));
        const SDL_FRect shadow{x, y + 2.0f, kCell, kCell};
        SDL_SetTextureColorMod(sheet, 0, 0, 0);
        SDL_RenderTextureRotated(renderer, sheet, &sparkSource, &shadow, 0.0, nullptr, flip);
        SDL_SetTextureColorMod(sheet, 255, 255, 255);
        const SDL_FRect body{x, y - 2.0f, kCell, kCell};
        SDL_RenderTextureRotated(renderer, sheet, &sparkSource, &body, 0.0, nullptr, flip);
    }
}
