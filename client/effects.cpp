#include "effects.h"

#include "camera.h"
#include "font.h"
#include "tile_map.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kSmashDuration = 10.0f / 60.0f;  // 10 ticks, like Minicraft's SmashParticle
constexpr int kNumberLifetime = 60;               // ticks, like Minicraft's TextParticle
constexpr float kTileSize = static_cast<float>(TileMap::kTileSize);

}  // namespace

bool Effects::load(SDL_Renderer* renderer, const std::string& smashPath) {
    smashTexture_ = loadTexture(renderer, smashPath);
    return smashTexture_ != nullptr;
}

void Effects::addSmash(int tx, int ty) {
    smashes_.push_back({static_cast<float>(tx) * kTileSize, static_cast<float>(ty) * kTileSize, kSmashDuration});
}

void Effects::addDamageNumber(int damage, float x, float y, SDL_Color color) {
    numbers_.push_back({std::to_string(damage), Bounce::toss(x, y, 2.0f), 0, color});
}

void Effects::clear() {
    smashes_.clear();
    numbers_.clear();
}

void Effects::update(float dt) {
    for (Smash& smash : smashes_) {
        smash.timeLeft -= dt;
    }
    std::erase_if(smashes_, [](const Smash& smash) { return smash.timeLeft <= 0.0f; });

    tickAccumulator_ += dt;
    while (tickAccumulator_ >= Bounce::kTick) {
        tickAccumulator_ -= Bounce::kTick;
        for (DamageNumber& number : numbers_) {
            number.motion.tick();
            ++number.age;
        }
    }
    std::erase_if(numbers_, [](const DamageNumber& number) { return number.age >= kNumberLifetime; });
}

void Effects::draw(SDL_Renderer* renderer, const Camera& camera, const Font& font) const {
    for (const Smash& smash : smashes_) {
        const SDL_FRect destination{smash.x - camera.x(), smash.y - camera.y(), kTileSize, kTileSize};
        SDL_RenderTexture(renderer, smashTexture_.get(), nullptr, &destination);
    }
    for (const DamageNumber& number : numbers_) {
        // Centred on its point and lifted by its height, like TextParticle.render (x - length * 4, y - z).
        const float x = std::floor(number.motion.x) - Font::textWidth(number.text) / 2.0f - camera.x();
        const float y = std::floor(number.motion.y) - std::floor(number.motion.z) - camera.y();
        font.drawShadowed(renderer, number.text, x, y, number.color);
    }
}
