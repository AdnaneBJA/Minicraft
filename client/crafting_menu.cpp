#include "crafting_menu.h"

#include "font.h"
#include "hud.h"
#include "items.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace {

constexpr const char* kTitle = "Crafting";
constexpr const char* kHaveTitle = "Have:";
constexpr const char* kCostTitle = "Cost:";
constexpr const char* kCursorLeft = "> ";
constexpr const char* kCursorRight = " <";
constexpr float kCell = 8.0f;
constexpr float kFrameLeft = 8.0f;  // outer top-left of the recipe frame, in view pixels
constexpr float kFrameTop = 8.0f;
constexpr SDL_Color kWhite{255, 255, 255, 255};
constexpr SDL_Color kGrey{153, 153, 153, 255};  // Minicraft's Color.GRAY: recipes the player can't afford

using InfoRow = std::pair<ItemType, std::string>;

// Minicraft's RecipeEntry text: " <name>", plus " x<amount>" when a recipe makes more than one.
std::string recipeText(const Recipe& recipe) {
    std::string text = std::string(" ") + itemName(recipe.product());
    if (recipe.amount() > 1) text += " x" + std::to_string(recipe.amount());
    return text;
}

// Outer size of a frame around `cells` interior cells.
float frameSize(int cells) { return static_cast<float>(cells + 2) * kCell; }

// A framed list of "icon text" rows with its title at the top left (the ItemListing boxes right of Minicraft's
// recipe list), outer top-left at (left, top). Returns the frame's outer bottom.
float drawInfoBox(SDL_Renderer* renderer, const Hud& hud, const Font& font, const ItemIcons& icons, const char* title,
                  float left, float top, const std::vector<InfoRow>& rows) {
    int columns = static_cast<int>(std::string(title).size());
    for (const auto& row : rows) columns = std::max(columns, 1 + static_cast<int>(row.second.size()));
    const int rowCount = std::max(1, static_cast<int>(rows.size()));
    hud.drawFrame(renderer, left + kCell, top + kCell, columns, rowCount);
    hud.drawTitle(renderer, font, title, left + kCell, top);
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const float y = top + kCell + static_cast<float>(i) * kCell;
        icons.draw(renderer, rows[i].first, left + kCell, y);
        font.draw(renderer, rows[i].second, left + kCell + ItemIcons::kSize, y, kWhite);
    }
    return top + frameSize(rowCount);
}

}  // namespace

CraftingMenu::CraftingMenu(std::vector<Recipe> recipes) : recipes_(std::move(recipes)) {}

const Recipe* CraftingMenu::handleKey(SDL_Keycode key, const Inventory& inventory) {
    const int count = static_cast<int>(recipes_.size());
    if (count == 0) return nullptr;
    if (key == SDLK_W || key == SDLK_UP) selected_ = (selected_ + count - 1) % count;  // wraps, like Minicraft
    if (key == SDLK_S || key == SDLK_DOWN) selected_ = (selected_ + 1) % count;
    if (key == SDLK_SPACE || key == SDLK_RETURN || key == SDLK_KP_ENTER) {
        const Recipe& recipe = recipes_[selected_];
        if (recipe.canCraft(inventory)) return &recipe;
    }
    return nullptr;
}

void CraftingMenu::draw(SDL_Renderer* renderer, const Hud& hud, const Font& font, const ItemIcons& icons,
                        const Inventory& inventory) const {
    if (!open_ || recipes_.empty()) return;
    const float cursorWidth = Font::textWidth(kCursorLeft);

    // Recipe list: sized and drawn like the inventory screen.
    int columns = static_cast<int>(std::string(kTitle).size()) + 4;
    for (const auto& recipe : recipes_) {
        columns = std::max(columns, 2 + 1 + static_cast<int>(recipeText(recipe).size()) + 2);
    }
    const int rows = static_cast<int>(recipes_.size());
    const float interiorLeft = kFrameLeft + kCell;
    const float interiorTop = kFrameTop + kCell;
    hud.drawFrame(renderer, interiorLeft, interiorTop, columns, rows);
    hud.drawTitle(renderer, font, kTitle,
                  std::floor(kFrameLeft + (frameSize(columns) - Font::textWidth(kTitle)) / 2.0f), kFrameTop);

    for (std::size_t i = 0; i < recipes_.size(); ++i) {
        const Recipe& recipe = recipes_[i];
        const float y = interiorTop + static_cast<float>(i) * kCell;
        const float entryX = interiorLeft + cursorWidth;
        const std::string text = recipeText(recipe);
        icons.draw(renderer, recipe.product(), entryX, y);
        font.draw(renderer, text, entryX + ItemIcons::kSize, y, recipe.canCraft(inventory) ? kWhite : kGrey);
        if (static_cast<int>(i) == selected_) {
            font.draw(renderer, kCursorLeft, interiorLeft, y, kWhite);
            font.draw(renderer, kCursorRight, entryX + ItemIcons::kSize + Font::textWidth(text), y, kWhite);
        }
    }

    // "Have:" one cell right of the list, level with its top. "Cost:" below it with its bottom level with the
    // list's bottom, like Minicraft, but pushed down so it never overlaps "Have:" when the list is short.
    const Recipe& selected = recipes_[selected_];
    const float boxLeft = kFrameLeft + frameSize(columns) + kCell;
    const float haveBottom =
        drawInfoBox(renderer, hud, font, icons, kHaveTitle, boxLeft, kFrameTop,
                    {{selected.product(), " " + std::to_string(inventory.count(selected.product()))}});
    std::vector<InfoRow> costRows;
    for (const auto& cost : selected.costs()) {
        costRows.emplace_back(cost.type,
                              " " + std::to_string(inventory.count(cost.type)) + "/" + std::to_string(cost.count));
    }
    const float listBottom = kFrameTop + frameSize(rows);
    const float costTop = std::max(haveBottom, listBottom - frameSize(std::max(1, static_cast<int>(costRows.size()))));
    drawInfoBox(renderer, hud, font, icons, kCostTitle, boxLeft, costTop, costRows);
}
