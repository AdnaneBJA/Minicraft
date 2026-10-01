#pragma once

#include <SDL3/SDL.h>

#include <memory>
#include <string>

struct TextureDeleter {
    void operator()(SDL_Texture* texture) const { SDL_DestroyTexture(texture); }
};

// Owning handle to an SDL texture.
using TexturePtr = std::unique_ptr<SDL_Texture, TextureDeleter>;

// Loads a PNG as a texture with nearest-neighbour scaling (pixel art). Logs and returns null on failure.
TexturePtr loadTexture(SDL_Renderer* renderer, const std::string& path);
