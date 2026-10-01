#pragma once

#include "texture.h"

#include <SDL3/SDL.h>

#include <string>
#include <string_view>

// Minicraft's 8x8 bitmap font (white glyphs, tinted when drawn). Monospace: every character is 8 px wide.
class Font {
public:
    static constexpr float kGlyphSize = 8.0f;

    bool load(SDL_Renderer* renderer, const std::string& path);

    void draw(SDL_Renderer* renderer, std::string_view text, float x, float y, SDL_Color color) const;
    // Draws a black copy 1 px down-right first, like Minicraft's text shadow.
    void drawShadowed(SDL_Renderer* renderer, std::string_view text, float x, float y, SDL_Color color) const;

    static float textWidth(std::string_view text) { return static_cast<float>(text.size()) * kGlyphSize; }

private:
    TexturePtr texture_;
};
