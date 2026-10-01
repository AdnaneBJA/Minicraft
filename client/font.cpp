#include "font.h"

#include <array>

namespace {

// Position of each printable ASCII character (32..126) in font.png's 32-column grid; -1 = no glyph (e.g. space).
// Generated from the character order in Minicraft+'s Font.java.
constexpr std::array<int, 95> kGlyphIndex = {
    -1,  38,  41,  82,  135, 47,  136, 40,  48,  49,  131, 43,  37,  42,  36,  45,  26,  27,  28,
    29,  30,  31,  32,  33,  34,  35,  52,  53,  50,  44,  51,  39,  55,  0,   1,   2,   3,   4,
    5,   6,   7,   8,   9,   10,  11,  12,  13,  14,  15,  16,  17,  18,  19,  20,  21,  22,  23,
    24,  25,  80,  46,  81,  54,  86,  -1,  141, 142, 143, 144, 145, 146, 147, 148, 149, 150, 151,
    152, 153, 154, 155, 156, 157, 158, 159, 160, 161, 162, 163, 164, 165, 166, 84,  83,  85,  -1};

int glyphIndex(char c) {
    const int code = static_cast<unsigned char>(c);
    if (code < 32 || code > 126) return -1;
    return kGlyphIndex[static_cast<std::size_t>(code - 32)];
}

}  // namespace

bool Font::load(SDL_Renderer* renderer, const std::string& path) {
    texture_ = loadTexture(renderer, path);
    return texture_ != nullptr;
}

void Font::draw(SDL_Renderer* renderer, std::string_view text, float x, float y, SDL_Color color) const {
    SDL_SetTextureColorMod(texture_.get(), color.r, color.g, color.b);
    SDL_SetTextureAlphaMod(texture_.get(), color.a);
    for (std::size_t i = 0; i < text.size(); ++i) {
        const int index = glyphIndex(text[i]);
        if (index < 0) continue;
        const SDL_FRect source{static_cast<float>(index % 32) * kGlyphSize, static_cast<float>(index / 32) * kGlyphSize,
                               kGlyphSize, kGlyphSize};
        const SDL_FRect destination{x + static_cast<float>(i) * kGlyphSize, y, kGlyphSize, kGlyphSize};
        SDL_RenderTexture(renderer, texture_.get(), &source, &destination);
    }
}

void Font::drawShadowed(SDL_Renderer* renderer, std::string_view text, float x, float y, SDL_Color color) const {
    draw(renderer, text, x + 1.0f, y + 1.0f, SDL_Color{0, 0, 0, color.a});
    draw(renderer, text, x, y, color);
}
