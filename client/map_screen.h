#pragma once

#include "texture.h"

#include <SDL3/SDL.h>

#include <string>
#include <vector>

class Font;
class Hud;
class Level;
class Player;

// The world map (Tab): the whole level the player is on, one pixel per tile, with markers for the player, the
// stairs down and the way to the boss (the surface's stairs up to the sky; the Air Wizard itself up there).
class MapScreen {
public:
    static constexpr SDL_Keycode kToggleKey = SDLK_TAB;

    // Builds the map image from the level's current tiles and opens the screen.
    void open(SDL_Renderer* renderer, const Level& level);
    bool isOpen() const { return open_; }
    void close() { open_ = false; }

    // Draws in view pixels over the world. The player's marker blinks.
    void draw(SDL_Renderer* renderer, const Hud& hud, const Font& font, const Level& level, const Player& player,
              float viewWidth, float viewHeight) const;

private:
    TexturePtr texture_;
    int width_ = 0;  // tiles
    int height_ = 0;
    bool open_ = false;
    std::vector<SDL_Point> stairsDown_;
    std::vector<SDL_Point> stairsUp_;
};
