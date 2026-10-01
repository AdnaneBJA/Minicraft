#include "texture.h"

TexturePtr loadTexture(SDL_Renderer* renderer, const std::string& path, bool whiteSilhouette) {
    SDL_Surface* surface = SDL_LoadPNG(path.c_str());
    if (!surface) {
        SDL_Log("Failed to load %s: %s", path.c_str(), SDL_GetError());
        return nullptr;
    }
    if (whiteSilhouette) {
        SDL_Surface* rgba = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(surface);
        surface = rgba;
        if (!surface) {
            SDL_Log("Failed to convert %s: %s", path.c_str(), SDL_GetError());
            return nullptr;
        }
        for (int y = 0; y < surface->h; ++y) {
            Uint8* row = static_cast<Uint8*>(surface->pixels) + y * surface->pitch;
            for (int x = 0; x < surface->w; ++x) {
                Uint8* pixel = row + x * 4;  // RGBA32: bytes are R, G, B, A
                if (pixel[3] != 0) pixel[0] = pixel[1] = pixel[2] = 255;
            }
        }
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
