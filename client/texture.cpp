#include "texture.h"

TexturePtr loadTexture(SDL_Renderer* renderer, const std::string& path) {
    SDL_Surface* surface = SDL_LoadPNG(path.c_str());
    if (!surface) {
        SDL_Log("Failed to load %s: %s", path.c_str(), SDL_GetError());
        return nullptr;
    }
    TexturePtr texture(SDL_CreateTextureFromSurface(renderer, surface));
    SDL_DestroySurface(surface);
    if (!texture) {
        SDL_Log("Failed to create texture for %s: %s", path.c_str(), SDL_GetError());
        return nullptr;
    }
    SDL_SetTextureScaleMode(texture.get(), SDL_SCALEMODE_NEAREST);
    return texture;
}
