#include "hud.h"
#include "item_icons.h"

#include "font.h"
#include "player.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace {

constexpr float kCell = 8.0f;

// hud.png cells (column, row).
constexpr int kHeartColumn = 0;  // row 0 = full, row 1 = empty
constexpr int kBoltColumn = 1;   // row 0 = full, row 1 = empty, row 2 = white (blink when exhausted)
constexpr int kHungerColumn = 2; // row 0 = full, row 1 = empty
constexpr int kBossBarInactiveRow = 4;
constexpr int kBossBarActiveRow = 5;
constexpr float kRightBlock = 80.0f;  // hunger: the last 10 cells of the hearts row
constexpr int kFrameRow = 6;     // columns: 0 = corner, 1 = top/bottom edge, 2 = left/right edge, 3 = fill
constexpr SDL_Color kTitleColor{255, 255, 0, 255};

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

void Hud::drawStatus(SDL_Renderer* renderer, const ItemIcons& icons, const Player& player, float viewWidth,
                     float viewHeight) const {
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
    const float hungerX = viewWidth - kRightBlock;
    for (int i = 0; i < Player::kMaxHunger; ++i) {
        drawCell(renderer, kHungerColumn, i < player.hunger() ? 0 : 1, hungerX + static_cast<float>(i) * kCell,
                 heartsY);
    }
    // One armour icon per 10 points left.
    if (const auto& armor = player.armor()) {
        const int pieces = (player.armorPoints() * 10 + Player::kMaxArmor - 1) / Player::kMaxArmor;
        for (int i = 0; i < pieces; ++i) icons.draw(renderer, *armor, static_cast<float>(i) * kCell, heartsY - kCell);
    }
}

void Hud::drawArrowCount(SDL_Renderer* renderer, const Font& font, const ItemIcons& icons, int arrows,
                         float viewHeight) const {
    const float x = 10.0f * kCell + 4.0f;
    const float y = viewHeight - 2.0f * kCell;
    const std::string text = "x" + std::to_string(std::min(arrows, 999));
    const SDL_FRect background{x, y, kCell + Font::textWidth(text), kCell};
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderFillRect(renderer, &background);
    icons.draw(renderer, ItemType::Arrow, x, y);
    font.draw(renderer, text, x + kCell, y, SDL_Color{255, 255, 255, 255});
}

void Hud::drawBossBar(SDL_Renderer* renderer, const Font& font, int percent, std::string_view name,
                      float viewWidth) const {
    // Renderer.renderBossbar: 100 two-pixel slices, grey behind and red for the health left.
    constexpr int kSlices = 100;
    const float x = std::floor((viewWidth - kSlices * 2.0f) / 2.0f);
    const float y = 4.0f;
    const auto slice = [&](int row, int index) {
        const SDL_FRect source{3.0f * kCell, static_cast<float>(row) * kCell, 2.0f, kCell};
        const SDL_FRect destination{x + static_cast<float>(index) * 2.0f, y, 2.0f, kCell};
        SDL_RenderTexture(renderer, texture_.get(), &source, &destination);
    };
    for (int i = 0; i < kSlices; ++i) slice(kBossBarInactiveRow, i);
    for (int i = 0; i < std::clamp(percent, 0, kSlices); ++i) slice(kBossBarActiveRow, i);
    font.drawShadowed(renderer, name, std::floor((viewWidth - Font::textWidth(name)) / 2.0f), y + kCell + 1.0f,
                      SDL_Color{255, 255, 255, 255});
}

void Hud::drawToolDurability(SDL_Renderer* renderer, const Font& font, const Inventory::Stack& tool,
                             float viewWidth, float viewHeight) const {
    const int max = maxDurability(tool.type);
    if (max <= 0) return;
    const int percent = tool.durability * 100 / max;
    const auto green = static_cast<Uint8>(static_cast<float>(percent) * 2.55f);
    const std::string text = std::to_string(percent) + "%";
    // Minicraft draws it at (164, h - 16); here it sits just left of the hunger bar so narrow views fit both.
    const float x = viewWidth - kRightBlock - kCell - Font::textWidth(text);
    const float y = viewHeight - 2.0f * kCell;
    const SDL_FRect background{x, y, Font::textWidth(text), kCell};
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderFillRect(renderer, &background);
    font.draw(renderer, text, x, y, SDL_Color{static_cast<Uint8>(255 - green), green, 0, 255});
}

void Hud::drawHeldItem(SDL_Renderer* renderer, const Font& font, const ItemIcons& icons, const Inventory::Stack& item,
                       float viewHeight) const {
    const float x = 10.0f * kCell;
    const float y = viewHeight - kCell;
    icons.draw(renderer, item.type, x, y);
    const std::string name = displayName(item);
    const SDL_FRect background{x + ItemIcons::kSize, y, Font::textWidth(name), kCell};
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderFillRect(renderer, &background);
    font.draw(renderer, name, background.x, y, SDL_Color{255, 255, 255, 255});
}

void Hud::drawTitle(SDL_Renderer* renderer, const Font& font, std::string_view title, float x, float y) const {
    for (std::size_t i = 0; i < title.size(); ++i) {
        const float letterX = x + static_cast<float>(i) * kCell;
        drawCell(renderer, 3, kFrameRow, letterX, y);
        font.draw(renderer, title.substr(i, 1), letterX, y, kTitleColor);
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
