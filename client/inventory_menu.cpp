#include "inventory_menu.h"

#include "font.h"
#include "hud.h"
#include "items.h"

#include <algorithm>
#include <string>

namespace {

constexpr const char* kTitle = "Inventory";
constexpr float kLeft = 16.0f;  // top-left of the frame's interior, in view pixels
constexpr float kTop = 16.0f;
constexpr SDL_Color kTitleColor{255, 255, 0, 255};
constexpr SDL_Color kSelectedColor{255, 255, 255, 255};
constexpr SDL_Color kUnselectedColor{153, 153, 153, 255};

// Same text as Minicraft's StackableItem.getDisplayName(): " <count> <name>".
std::string entryText(const Inventory::Stack& stack) {
    return " " + std::to_string(std::min(stack.count, 999)) + " " + itemName(stack.type);
}

}  // namespace

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

    // Size the frame to the title and the longest entry (icon cell + text).
    int columns = static_cast<int>(std::string(kTitle).size()) + 2;
    for (const auto& stack : stacks) {
        columns = std::max(columns, 1 + static_cast<int>(entryText(stack).size()));
    }
    const int rows = std::max(1, static_cast<int>(stacks.size()));
    hud.drawFrame(renderer, kLeft, kTop, columns, rows);

    // Title centred on the top edge of the frame.
    const float frameWidth = static_cast<float>(columns) * Font::kGlyphSize;
    font.draw(renderer, kTitle, kLeft + (frameWidth - Font::textWidth(kTitle)) / 2.0f, kTop - Font::kGlyphSize,
              kTitleColor);

    const int selected = std::min(selected_, static_cast<int>(stacks.size()) - 1);
    for (std::size_t i = 0; i < stacks.size(); ++i) {
        const float y = kTop + static_cast<float>(i) * Font::kGlyphSize;
        icons.draw(renderer, stacks[i].type, kLeft, y);
        font.draw(renderer, entryText(stacks[i]), kLeft + ItemIcons::kSize, y,
                  static_cast<int>(i) == selected ? kSelectedColor : kUnselectedColor);
    }
}
