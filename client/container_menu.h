#pragma once

#include <SDL3/SDL.h>

#include <optional>
#include <string>

class Audio;
class Font;
class Hud;
class Inventory;
class ItemIcons;

// The chest screen (Minicraft+'s ContainerDisplay): the chest's items and the player's, one list at a time.
// Left/right switch between the two; Space or Enter moves the selected stack to the other side.
class ContainerMenu {
public:
    // Moving the cursor plays Minicraft's select sound.
    void setAudio(Audio* audio) { audio_ = audio; }

    // Opens on the chest standing on tile (tx, ty); `title` names it ("Chest" or "Death Chest").
    void open(int tx, int ty, std::string title);
    bool isOpen() const { return open_; }
    void close() { open_ = false; }
    SDL_Point chestTile() const { return chestTile_; }

    // A stack to move: from the chest to the inventory (`fromChest`) or back.
    struct Transfer {
        bool fromChest;
        int index;
    };
    std::optional<Transfer> handleKey(SDL_Keycode key, const Inventory& chest, const Inventory& inventory);
    void draw(SDL_Renderer* renderer, const Hud& hud, const Font& font, const ItemIcons& icons, const Inventory& chest,
              const Inventory& inventory, float viewHeight) const;

private:
    Audio* audio_ = nullptr;
    bool open_ = false;
    bool chestSide_ = true;  // which list has the cursor
    int selected_ = 0;
    SDL_Point chestTile_{0, 0};
    std::string title_;
};
