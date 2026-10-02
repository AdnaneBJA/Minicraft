#pragma once

#include "recipe.h"

#include <SDL3/SDL.h>

#include <string>
#include <vector>

class Audio;
class Font;
class Hud;
class Inventory;
class ItemIcons;

// The crafting screen (Z), drawn like Minicraft's CraftingDisplay: the recipe list in the same frame as the
// inventory (craftable recipes in white, the rest in grey), and to its right a "Have:" box with how many of the
// selected product the player owns and a "Cost:" box with "owned/needed" for each ingredient.
class CraftingMenu {
public:
    // Moving the cursor plays Minicraft's select sound.
    void setAudio(Audio* audio) { audio_ = audio; }

    static constexpr SDL_Keycode kToggleKey = SDLK_Z;

    // Opens on a list of recipes; `title` is set into the top edge of the recipe frame ("Crafting" by hand, or
    // the station's name).
    void open(std::vector<Recipe> recipes, std::string title);
    bool isOpen() const { return open_; }
    void close() { open_ = false; }

    // W/S or the arrows move the cursor. Space or Enter returns the selected recipe if the inventory can pay for
    // it (the caller crafts it); otherwise nullptr.
    const Recipe* handleKey(SDL_Keycode key, const Inventory& inventory);
    // Where the cursor is in the recipe list (the recipe handleKey returned).
    int selectedIndex() const { return selected_; }
    // Long lists scroll to keep the cursor in view within `viewHeight`.
    void draw(SDL_Renderer* renderer, const Hud& hud, const Font& font, const ItemIcons& icons,
              const Inventory& inventory, float viewHeight) const;

private:
    Audio* audio_ = nullptr;
    std::vector<Recipe> recipes_;
    std::string title_;
    bool open_ = false;
    int selected_ = 0;
};
