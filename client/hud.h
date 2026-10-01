#pragma once

#include "texture.h"

#include <SDL3/SDL.h>

#include <string>

class Player;

// On-screen UI drawn from Minicraft's hud.png: health hearts, energy bolts, and the menu frame.
// Coordinates are in view pixels (world-pixel scale, not moved by the camera).
class Hud {
public:
    bool load(SDL_Renderer* renderer, const std::string& path);

    // Hearts on the second-to-last row and energy bolts on the last row, at the bottom left (like Minicraft).
    void drawStatus(SDL_Renderer* renderer, const Player& player, float viewHeight) const;

    // Minicraft's menu frame around an interior of `columns` x `rows` 8x8 cells whose top-left is (x, y).
    void drawFrame(SDL_Renderer* renderer, float x, float y, int columns, int rows) const;

private:
    // Draws the 8x8 cell (cx, cy) of hud.png at (x, y).
    void drawCell(SDL_Renderer* renderer, int cx, int cy, float x, float y, int flip = SDL_FLIP_NONE) const;

    TexturePtr texture_;
};
