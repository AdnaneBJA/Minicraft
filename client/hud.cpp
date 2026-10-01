#include "hud.h"

#include "player.h"

namespace {

constexpr float kCell = 8.0f;

// hud.png cells (column, row).
constexpr int kHeartColumn = 0;  // row 0 = full, row 1 = empty
constexpr int kBoltColumn = 1;   // row 0 = full, row 1 = empty, row 2 = white (blink when exhausted)
constexpr int kFrameRow = 6;     // columns: 0 = corner, 1 = top/bottom edge, 2 = left/right edge, 3 = fill

}  // namespace

bool Hud::load(SDL_Renderer* renderer, const std::string& path) {
    texture_ = loadTexture(renderer, path);
    return texture_ != nullptr;
}

void Hud::drawCell(SDL_Renderer* renderer, int cx, int cy, float x, float y, int flip) const {
    const SDL_FRect source{static_cast<float>(cx) * kCell, static_cast<float>(cy) * kCell, kCell, kCell};
    const SDL_FRect destination{x, y, kCell, kCell};
    SDL_RenderTextureRotated(renderer, texture_.get(), &source, &destination, 0.0, nullptr,
                             static_cast<SDL_FlipMode>(flip));
}

void Hud::drawStatus(SDL_Renderer* renderer, const Player& player, float viewHeight) const {
    const float heartsY = viewHeight - 2.0f * kCell;
    const float boltsY = viewHeight - kCell;
    // While exhausted, every bolt blinks white/grey (Minicraft: staminaRechargeDelay / 4 % 2).
    const bool exhausted = player.energyRechargeDelay() > 0;
    const bool blinkWhite = (player.energyRechargeDelay() / 4) % 2 == 0;
    for (int i = 0; i < Player::kMaxHealth; ++i) {
        drawCell(renderer, kHeartColumn, i < player.health() ? 0 : 1, static_cast<float>(i) * kCell, heartsY);
    }
    for (int i = 0; i < Player::kMaxEnergy; ++i) {
        int row = i < player.energy() ? 0 : 1;
        if (exhausted) row = blinkWhite ? 2 : 1;
        drawCell(renderer, kBoltColumn, row, static_cast<float>(i) * kCell, boltsY);
    }
}

void Hud::drawFrame(SDL_Renderer* renderer, float x, float y, int columns, int rows) const {
    constexpr int flipX = SDL_FLIP_HORIZONTAL;
    constexpr int flipY = SDL_FLIP_VERTICAL;
    const float right = x + static_cast<float>(columns) * kCell;
    const float bottom = y + static_cast<float>(rows) * kCell;
    drawCell(renderer, 0, kFrameRow, x - kCell, y - kCell);
    drawCell(renderer, 0, kFrameRow, right, y - kCell, flipX);
    drawCell(renderer, 0, kFrameRow, x - kCell, bottom, flipY);
    drawCell(renderer, 0, kFrameRow, right, bottom, flipX | flipY);
    for (int c = 0; c < columns; ++c) {
        const float cellX = x + static_cast<float>(c) * kCell;
        drawCell(renderer, 1, kFrameRow, cellX, y - kCell);
        drawCell(renderer, 1, kFrameRow, cellX, bottom, flipY);
    }
    for (int r = 0; r < rows; ++r) {
        const float cellY = y + static_cast<float>(r) * kCell;
        drawCell(renderer, 2, kFrameRow, x - kCell, cellY);
        drawCell(renderer, 2, kFrameRow, right, cellY, flipX);
        for (int c = 0; c < columns; ++c) {
            drawCell(renderer, 3, kFrameRow, x + static_cast<float>(c) * kCell, cellY);
        }
    }
}
