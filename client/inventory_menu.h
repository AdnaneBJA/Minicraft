#pragma once

#include <SDL3/SDL.h>

class Font;
class Hud;
class Inventory;
class ItemIcons;

// The inventory screen (E): Minicraft's framed list of "icon  count Name" entries; W/S or arrows move the selection.
class InventoryMenu {
public:
    static constexpr SDL_Keycode kToggleKey = SDLK_E;

    bool isOpen() const { return open_; }
    void toggle() { open_ = !open_; }
    void close() { open_ = false; }

    void handleKey(SDL_Keycode key, const Inventory& inventory);
    void draw(SDL_Renderer* renderer, const Hud& hud, const Font& font, const ItemIcons& icons,
              const Inventory& inventory) const;

private:
    bool open_ = false;
    int selected_ = 0;
};
