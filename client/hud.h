#pragma once

#include "items.h"
#include "texture.h"

#include <SDL3/SDL.h>

#include <string>
#include <string_view>

class Font;
class ItemIcons;
class Player;

// On-screen UI drawn from Minicraft's hud.png: health hearts, energy bolts, hunger, armour, the boss bar and the
// menu frame.
// Coordinates are in view pixels (world-pixel scale, not moved by the camera).
class Hud {
public:
    bool load(SDL_Renderer* renderer, const std::string& path);

    // Hearts on the second-to-last row and energy bolts on the last row, at the bottom left (like Minicraft);
    // hunger at the right end of the hearts row and the worn armour's icons above the hearts (Minicraft+).
    void drawStatus(SDL_Renderer* renderer, const ItemIcons& icons, const Player& player, float viewWidth,
                    float viewHeight) const;
    // With a bow in hand: the arrow icon and how many arrows are left, above the held item (Renderer.renderGui).
    void drawArrowCount(SDL_Renderer* renderer, const Font& font, const ItemIcons& icons, int arrows,
                        float viewHeight) const;
    // Minicraft+'s boss bar: `percent` of the bar lit, the boss's name under it, near the top of the view.
    void drawBossBar(SDL_Renderer* renderer, const Font& font, int percent, std::string_view name,
                     float viewWidth) const;

    // The held item on the energy row, right of the bolts: icon plus its name on a black background (Minicraft's
    // Item.renderHUD at (10 * 8, Screen.h - 8)).
    // The held tool's durability as a percentage at the right of the hearts row, red when worn and green when new
    // (Minicraft's tool durability status).
    void drawToolDurability(SDL_Renderer* renderer, const Font& font, const Inventory::Stack& tool, float viewWidth,
                            float viewHeight) const;
    void drawHeldItem(SDL_Renderer* renderer, const Font& font, const ItemIcons& icons, const Inventory::Stack& item,
                      float viewHeight) const;

    // Minicraft's menu frame around an interior of `columns` x `rows` 8x8 cells whose top-left is (x, y).
    void drawFrame(SDL_Renderer* renderer, float x, float y, int columns, int rows) const;
    // A title set into a frame's top edge at (x, y): each letter on a fill cell so the border doesn't run through it
    // (Minicraft's Menu.render).
    void drawTitle(SDL_Renderer* renderer, const Font& font, std::string_view title, float x, float y) const;

private:
    // Draws the 8x8 cell (cx, cy) of hud.png at (x, y).
    void drawCell(SDL_Renderer* renderer, int cx, int cy, float x, float y, int flip = SDL_FLIP_NONE) const;

    TexturePtr texture_;
};
