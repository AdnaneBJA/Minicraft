#pragma once

#include "items.h"
#include "texture.h"

#include <SDL3/SDL.h>

#include <string>

// The 8x8 item icons (items.png, one column per ItemType).
class ItemIcons {
public:
    static constexpr float kSize = 8.0f;

    bool load(SDL_Renderer* renderer, const std::string& path);
    void draw(SDL_Renderer* renderer, ItemType type, float x, float y) const;
    // Dark silhouette of the icon, used as a drop shadow under items lying on the ground.
    void drawShadow(SDL_Renderer* renderer, ItemType type, float x, float y) const;

private:
    TexturePtr texture_;
};
