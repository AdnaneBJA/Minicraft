#include "inventory_menu.h"

#include "font.h"
#include "hud.h"
#include "items.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace {

constexpr const char* kTitle = "Inventory";
constexpr const char* kCursorLeft = "> ";
constexpr const char* kCursorRight = " <";
constexpr float kCell = 8.0f;
constexpr float kFrameLeft = 8.0f;  // outer top-left of the frame, in view pixels
constexpr float kFrameTop = 8.0f;
constexpr SDL_Color kWhite{255, 255, 255, 255};
constexpr SDL_Color kTitleColor{255, 255, 0, 255};
constexpr SDL_Color kCapacityColor{153, 153, 153, 255};

// Same text as Minicraft's StackableItem.getDisplayName(): " <count> <name>".
std::string entryText(const Inventory::Stack& stack) {
    return " " + std::to_string(std::min(stack.count, 999)) + " " + itemName(stack.type);
}

// Minicraft's counter colour: green when empty, yellow at half, red when full (PlayerInvDisplay.colorByHeaviness).
SDL_Color heavinessColor(int used, int capacity) {
    const float fill = static_cast<float>(used) / static_cast<float>(capacity) - 1.0f;
    const float heaviness = std::clamp(-(fill * fill) + 1.0f, 0.0f, 1.0f);
    if (heaviness < 0.5f) {
        return SDL_Color{static_cast<Uint8>(heaviness / 0.5f * 255.0f), 255, 0, 255};
    }
    return SDL_Color{255, static_cast<Uint8>((1.0f - heaviness) / 0.5f * 255.0f), 0, 255};
}

}  // namespace

bool InventoryMenu::load(SDL_Renderer* renderer, const std::string& counterPath) {
    counterTexture_ = loadTexture(renderer, counterPath);
    return counterTexture_ != nullptr;
}

void InventoryMenu::handleKey(SDL_Keycode key, const Inventory& inventory) {
    const int count = static_cast<int>(inventory.stacks().size());
    if (count == 0) return;
    if (key == SDLK_W || key == SDLK_UP) selected_ = (selected_ + count - 1) % count;  // wraps, like Minicraft
    if (key == SDLK_S || key == SDLK_DOWN) selected_ = (selected_ + 1) % count;
}

void InventoryMenu::draw(SDL_Renderer* renderer, const Hud& hud, const Font& font, const ItemIcons& icons,
                         const Inventory& inventory) const {
    if (!open_) return;
    const auto& stacks = inventory.stacks();
    const float cursorWidth = Font::textWidth(kCursorLeft);

    // Interior: room for "> " + icon + text + " <" on every row, and wide enough that the centred title and the
    // counter in the top-right corner don't overlap.
    int columns = static_cast<int>(std::string(kTitle).size()) + 4;
    for (const auto& stack : stacks) {
        columns = std::max(columns, 2 + 1 + static_cast<int>(entryText(stack).size()) + 2);
    }
    const int rows = std::max(1, static_cast<int>(stacks.size()));
    const float interiorLeft = kFrameLeft + kCell;
    const float interiorTop = kFrameTop + kCell;
    hud.drawFrame(renderer, interiorLeft, interiorTop, columns, rows);
    const float frameWidth = static_cast<float>(columns + 2) * kCell;

    // Title set into the top edge, centred on the whole frame, each letter on a fill cell so the border lines
    // don't run through it (Menu.render).
    const float titleX = std::floor(kFrameLeft + (frameWidth - Font::textWidth(kTitle)) / 2.0f);
    const std::string title = kTitle;
    for (std::size_t i = 0; i < title.size(); ++i) {
        const float x = titleX + static_cast<float>(i) * kCell;
        hud.drawFillCell(renderer, x, kFrameTop);
        font.draw(renderer, title.substr(i, 1), x, kFrameTop, kTitleColor);
    }

    const int selected = std::min(selected_, static_cast<int>(stacks.size()) - 1);
    for (std::size_t i = 0; i < stacks.size(); ++i) {
        const float y = interiorTop + static_cast<float>(i) * kCell;
        const float entryX = interiorLeft + cursorWidth;
        const std::string text = entryText(stacks[i]);
        icons.draw(renderer, stacks[i].type, entryX, y);
        font.draw(renderer, text, entryX + ItemIcons::kSize, y, kWhite);
        if (static_cast<int>(i) == selected) {
            font.draw(renderer, kCursorLeft, interiorLeft, y, kWhite);
            font.draw(renderer, kCursorRight, entryX + ItemIcons::kSize + Font::textWidth(text), y, kWhite);
        }
    }

    drawCounter(renderer, kFrameLeft + frameWidth, kFrameTop, static_cast<int>(stacks.size()), Inventory::kMaxSlots);
}

void InventoryMenu::drawCounter(SDL_Renderer* renderer, float frameRight, float frameTop, int used,
                                int capacity) const {
    // Positions and source rectangles from Minicraft+'s PlayerInvDisplay.render (focused inventory).
    const auto blit = [&](float sx, float sy, float w, float h, float x, float y) {
        const SDL_FRect source{sx, sy, w, h};
        const SDL_FRect destination{x, y, w, h};
        SDL_RenderTexture(renderer, counterTexture_.get(), &source, &destination);
    };
    SDL_SetTextureColorMod(counterTexture_.get(), 255, 255, 255);
    const float right = frameRight + 2.0f;
    if (used < 10) {
        // Narrow box: the left cap plus the right part, skipping the middle that holds a second digit.
        blit(12, 12, 3, 13, right - 18.0f, frameTop - 3.0f);
        blit(20, 12, 15, 13, right - 15.0f, frameTop - 3.0f);
        drawCounterNumber(renderer, right - 16.0f, frameTop - 1.0f, 5, 5, 7, used, heavinessColor(used, capacity));
    } else {
        blit(12, 12, 23, 13, right - 23.0f, frameTop - 3.0f);
        drawCounterNumber(renderer, right - 21.0f, frameTop - 1.0f, 5, 5, 7, used, heavinessColor(used, capacity));
    }
    drawCounterNumber(renderer, right - 10.0f, frameTop + 3.0f, 0, 4, 5, capacity, kCapacityColor);
}

void InventoryMenu::drawCounterNumber(SDL_Renderer* renderer, float x, float y, float sourceY, float w, float h,
                                      int number, SDL_Color color) const {
    SDL_SetTextureColorMod(counterTexture_.get(), color.r, color.g, color.b);
    const std::string digits = std::to_string(number);
    for (std::size_t i = 0; i < digits.size(); ++i) {
        const SDL_FRect source{w * static_cast<float>(digits[i] - '0'), sourceY, w, h};
        const SDL_FRect destination{x + static_cast<float>(i) * w, y, w, h};
        SDL_RenderTexture(renderer, counterTexture_.get(), &source, &destination);
    }
    SDL_SetTextureColorMod(counterTexture_.get(), 255, 255, 255);
}
