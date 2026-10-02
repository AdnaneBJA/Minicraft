#include "item_icons.h"

bool ItemIcons::load(SDL_Renderer* renderer, const std::string& path) {
    texture_ = loadTexture(renderer, path);
    return texture_ != nullptr;
}

void ItemIcons::draw(SDL_Renderer* renderer, ItemType type, float x, float y) const {
    const SDL_FRect source{static_cast<float>(static_cast<int>(type)) * kSize, 0.0f, kSize, kSize};
    const SDL_FRect destination{x, y, kSize, kSize};
    SDL_RenderTexture(renderer, texture_.get(), &source, &destination);
}

void ItemIcons::drawShadow(SDL_Renderer* renderer, ItemType type, float x, float y) const {
    SDL_SetTextureColorMod(texture_.get(), 0, 0, 0);
    SDL_SetTextureAlphaMod(texture_.get(), 140);
    draw(renderer, type, x, y);
    SDL_SetTextureColorMod(texture_.get(), 255, 255, 255);
    SDL_SetTextureAlphaMod(texture_.get(), 255);
}
