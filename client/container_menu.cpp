#include "container_menu.h"

#include "audio.h"
#include "font.h"
#include "hud.h"
#include "item_icons.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace {

constexpr const char* kCursorLeft = "> ";
constexpr const char* kCursorRight = " <";
constexpr const char* kInventoryTitle = "Inventory";
constexpr float kCell = 8.0f;
constexpr float kFrameLeft = 8.0f;
constexpr float kFrameTop = 8.0f;
constexpr SDL_Color kWhite{255, 255, 255, 255};
constexpr SDL_Color kGrey{153, 153, 153, 255};

}  // namespace

void ContainerMenu::open(int tx, int ty, std::string title) {
    chestTile_ = {tx, ty};
    title_ = std::move(title);
    chestSide_ = true;
    selected_ = 0;
    open_ = true;
}

std::optional<ContainerMenu::Transfer> ContainerMenu::handleKey(SDL_Keycode key, const Inventory& chest,
                                                                const Inventory& inventory) {
    if (key == SDLK_A || key == SDLK_LEFT || key == SDLK_D || key == SDLK_RIGHT) {
        chestSide_ = !chestSide_;
        if (audio_) audio_->play(Sound::Select);
        selected_ = 0;
        return std::nullopt;
    }
    const int count = static_cast<int>((chestSide_ ? chest : inventory).stacks().size());
    if (count == 0) return std::nullopt;
    selected_ = std::min(selected_, count - 1);
    if (key == SDLK_W || key == SDLK_UP) selected_ = (selected_ + count - 1) % count;
    if (key == SDLK_S || key == SDLK_DOWN) selected_ = (selected_ + 1) % count;
    if (audio_ && (key == SDLK_W || key == SDLK_UP || key == SDLK_S || key == SDLK_DOWN)) audio_->play(Sound::Select);
    if (key == SDLK_SPACE || key == SDLK_RETURN || key == SDLK_KP_ENTER) return Transfer{chestSide_, selected_};
    return std::nullopt;
}

void ContainerMenu::draw(SDL_Renderer* renderer, const Hud& hud, const Font& font, const ItemIcons& icons,
                         const Inventory& chest, const Inventory& inventory, float viewHeight) const {
    if (!open_) return;
    const Inventory& shown = chestSide_ ? chest : inventory;
    const std::string title = chestSide_ ? title_ : kInventoryTitle;
    const auto& stacks = shown.stacks();
    const float cursorWidth = Font::textWidth(kCursorLeft);

    // "< Title >" says the other list is a key press away.
    const std::string header = "< " + title + " >";
    int columns = static_cast<int>(header.size()) + 4;
    for (const auto& stack : stacks) columns = std::max(columns, 2 + 1 + static_cast<int>(displayName(stack).size()) + 2);
    const int count = static_cast<int>(stacks.size());
    const int rows = std::clamp(static_cast<int>((viewHeight - kFrameTop - 4.0f * kCell) / kCell), 1,
                                std::max(1, count));
    const float interiorLeft = kFrameLeft + kCell;
    const float interiorTop = kFrameTop + kCell;
    hud.drawFrame(renderer, interiorLeft, interiorTop, columns, rows);
    const float frameWidth = static_cast<float>(columns + 2) * kCell;
    hud.drawTitle(renderer, font, header, std::floor(kFrameLeft + (frameWidth - Font::textWidth(header)) / 2.0f),
                  kFrameTop);

    if (count == 0) {
        font.draw(renderer, " Empty", interiorLeft + cursorWidth, interiorTop, kGrey);
        return;
    }
    const int selected = std::min(selected_, count - 1);
    const int first = std::clamp(selected - rows / 2, 0, std::max(0, count - rows));
    for (int row = 0; row < rows && first + row < count; ++row) {
        const int i = first + row;
        const float y = interiorTop + static_cast<float>(row) * kCell;
        const float entryX = interiorLeft + cursorWidth;
        const std::string text = displayName(stacks[static_cast<std::size_t>(i)]);
        icons.draw(renderer, stacks[static_cast<std::size_t>(i)].type, entryX, y);
        font.draw(renderer, text, entryX + ItemIcons::kSize, y, kWhite);
        if (i == selected) {
            font.draw(renderer, kCursorLeft, interiorLeft, y, kWhite);
            font.draw(renderer, kCursorRight, entryX + ItemIcons::kSize + Font::textWidth(text), y, kWhite);
        }
    }
}
