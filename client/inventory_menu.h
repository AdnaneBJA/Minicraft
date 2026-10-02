#pragma once

#include "texture.h"

#include <SDL3/SDL.h>

#include <optional>
#include <string>

class Audio;
class Font;
class Hud;
class Inventory;
class ItemIcons;

// The inventory screen (E), drawn like Minicraft's: a framed list of "icon count Name" entries with a "> <" cursor
// on the selected one, the title set into the top edge, and a slots-used / capacity counter at the top right.
class InventoryMenu {
public:
    // Moving the cursor plays Minicraft's select sound.
    void setAudio(Audio* audio) { audio_ = audio; }

    static constexpr SDL_Keycode kToggleKey = SDLK_E;

    // Loads inventory_counter.png (counter box and its digits).
    bool load(SDL_Renderer* renderer, const std::string& counterPath);

    bool isOpen() const { return open_; }
    void toggle() { open_ = !open_; }
    void close() { open_ = false; }

    // W/S or the arrows move the cursor. Space or Enter returns the selected slot, which the caller puts in the
    // player's hand (Minicraft's PlayerInvDisplay); otherwise nullopt.
    std::optional<int> handleKey(SDL_Keycode key, const Inventory& inventory);
    // Long inventories scroll to keep the cursor in view within `viewHeight`.
    void draw(SDL_Renderer* renderer, const Hud& hud, const Font& font, const ItemIcons& icons,
              const Inventory& inventory, float viewHeight) const;

private:
    Audio* audio_ = nullptr;
    // Minicraft's slots counter, anchored to the frame's top-right corner.
    void drawCounter(SDL_Renderer* renderer, float frameRight, float frameTop, int used, int capacity) const;
    // Draws a number with the digit strip at row `sourceY` of inventory_counter.png (each digit w x h).
    void drawCounterNumber(SDL_Renderer* renderer, float x, float y, float sourceY, float w, float h, int number,
                           SDL_Color color) const;

    TexturePtr counterTexture_;
    bool open_ = false;
    int selected_ = 0;
};
