#pragma once

#include "day_night.h"
#include "texture.h"

#include <SDL3/SDL.h>

#include <vector>

class Camera;

// The night overlay: darkness over the whole view except dithered circles of light (Minicraft's LightOverlay).
class Lighting {
public:
    // Draws in world-pixel scale (call with the world render scale active).
    void draw(SDL_Renderer* renderer, const Camera& camera, float darkness, const std::vector<Light>& lights);

private:
    TexturePtr texture_;
    int width_ = 0;
    int height_ = 0;
    std::vector<Uint8> pixels_;
};
