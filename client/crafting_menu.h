#pragma once

#include "recipe.h"

#include <SDL3/SDL.h>

#include <vector>

class Font;
class Hud;
class Inventory;
class ItemIcons;

// The crafting screen (Z), drawn like Minicraft's CraftingDisplay: the recipe list in the same frame as the
// inventory (craftable recipes in white, the rest in grey), and to its right a "Have:" box with how many of the
// selected product the player owns and a "Cost:" box with "owned/needed" for each ingredient.
class CraftingMenu {
public:
    static constexpr SDL_Keycode kToggleKey = SDLK_Z;

    explicit CraftingMenu(std::vector<Recipe> recipes);

    bool isOpen() const { return open_; }
    void toggle() { open_ = !open_; }
    void close() { open_ = false; }

    // W/S or the arrows move the cursor. Space or Enter returns the selected recipe if the inventory can pay for
    // it (the caller crafts it); otherwise nullptr.
    const Recipe* handleKey(SDL_Keycode key, const Inventory& inventory);
    void draw(SDL_Renderer* renderer, const Hud& hud, const Font& font, const ItemIcons& icons,
              const Inventory& inventory) const;

private:
    std::vector<Recipe> recipes_;
    bool open_ = false;
    int selected_ = 0;
};
