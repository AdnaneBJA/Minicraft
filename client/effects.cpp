#include "effects.h"

#include "camera.h"
#include "tile_map.h"

#include <algorithm>

namespace {

constexpr float kSmashDuration = 10.0f / 60.0f;  // 10 ticks, like Minicraft's SmashParticle
constexpr float kTileSize = static_cast<float>(TileMap::kTileSize);

}  // namespace

bool Effects::load(SDL_Renderer* renderer, const std::string& smashPath) {
    SDL_Surface* surface = SDL_LoadPNG(smashPath.c_str());
    if (!surface) {
        SDL_Log("Failed to load %s: %s", smashPath.c_str(), SDL_GetError());
        return false;
    }
    smashTexture_.reset(SDL_CreateTextureFromSurface(renderer, surface));
    SDL_DestroySurface(surface);
    if (!smashTexture_) {
        SDL_Log("Failed to create smash texture: %s", SDL_GetError());
        return false;
    }
    SDL_SetTextureScaleMode(smashTexture_.get(), SDL_SCALEMODE_NEAREST);
    return true;
}

void Effects::addSmash(int tx, int ty) {
    smashes_.push_back({static_cast<float>(tx) * kTileSize, static_cast<float>(ty) * kTileSize, kSmashDuration});
}

void Effects::update(float dt) {
    for (Smash& smash : smashes_) {
        smash.timeLeft -= dt;
    }
    std::erase_if(smashes_, [](const Smash& smash) { return smash.timeLeft <= 0.0f; });
}

void Effects::draw(SDL_Renderer* renderer, const Camera& camera) const {
    for (const Smash& smash : smashes_) {
        const SDL_FRect destination{smash.x - camera.x(), smash.y - camera.y(), kTileSize, kTileSize};
        SDL_RenderTexture(renderer, smashTexture_.get(), nullptr, &destination);
    }
}
