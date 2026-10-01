#include "day_night.h"

#include "camera.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kTick = 1.0f / 60.0f;
constexpr int kQuarter = DayNight::kDayLength / 4;
constexpr float kMaxDark = 128.0f;  // Screen.MAXDARK

// Minicraft's 4x4 ordered-dither matrix: a pixel is dark when its light level / 10 is at most this value.
constexpr int kDither[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};

}  // namespace

const char* timeName(DayNight::Time time) {
    switch (time) {
        case DayNight::Time::Morning: return "Morning";
        case DayNight::Time::Day: return "Day";
        case DayNight::Time::Evening: return "Evening";
        case DayNight::Time::Night: return "Night";
    }
    return "?";
}

void DayNight::update(float dt) {
    tickAccumulator_ += dt;
    while (tickAccumulator_ >= kTick) {
        tickAccumulator_ -= kTick;
        if (++tick_ >= kDayLength) {  // back to morning
            tick_ = 0;
            pastDay1_ = true;
        }
    }
}

DayNight::Time DayNight::time() const { return static_cast<Time>(std::min(tick_ / kQuarter, 3)); }

void DayNight::setTime(Time time) {
    tick_ = static_cast<int>(time) * kQuarter;
    pastDay1_ = true;  // like Updater.changeTimeOfDay
}

void DayNight::restore(int tick, bool pastDay1) {
    tick_ = tick;
    pastDay1_ = pastDay1;
    tickAccumulator_ = 0.0f;
}

float DayNight::darkness() const {
    // Screen.overlay: fade in through the evening, full at night, fade out through the morning (except day 1).
    const float progress = static_cast<float>(tick_ % kQuarter) / static_cast<float>(kQuarter);
    float darkFactor = 0.0f;
    switch (time()) {
        case Time::Morning: darkFactor = pastDay1_ ? (1.0f - progress) * kMaxDark : 0.0f; break;
        case Time::Day: darkFactor = 0.0f; break;
        case Time::Evening: darkFactor = progress * kMaxDark; break;
        case Time::Night: darkFactor = kMaxDark; break;
    }
    return darkFactor / 160.0f;
}

void Lighting::draw(SDL_Renderer* renderer, const Camera& camera, float darkness, const std::vector<Light>& lights) {
    if (darkness <= 0.0f) return;

    // One overlay pixel per world pixel, covering the view (plus one for the camera's sub-pixel offset).
    const int originX = static_cast<int>(std::floor(camera.x()));
    const int originY = static_cast<int>(std::floor(camera.y()));
    const int width = static_cast<int>(std::ceil(camera.width())) + 1;
    const int height = static_cast<int>(std::ceil(camera.height())) + 1;
    if (!texture_ || width != width_ || height != height_) {
        texture_.reset(SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, width, height));
        if (!texture_) return;
        SDL_SetTextureBlendMode(texture_.get(), SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(texture_.get(), SDL_SCALEMODE_NEAREST);
        width_ = width;
        height_ = height;
        pixels_.assign(static_cast<std::size_t>(width * height * 4), 0);
    }

    // Unlit pixels: fully dark (light 0 always passes the dither test).
    for (std::size_t i = 0; i < pixels_.size(); i += 4) {
        pixels_[i] = pixels_[i + 1] = pixels_[i + 2] = 0;
        pixels_[i + 3] = 255;
    }
    // Lit pixels, only inside each light's bounding box. Light level falls off as 1 - (d / r)^4 (Minicraft's
    // radial gradient), and the ordered dither turns the falloff into the classic speckled edge.
    // (d / r)^4 is (d^2 / r^2)^2, so no square roots are needed; pixels already fully lit are skipped (lava lakes
    // put a light on every tile).
    for (const Light& light : lights) {
        const int x0 = std::max(0, static_cast<int>(std::floor(light.x - light.radius)) - originX);
        const int x1 = std::min(width - 1, static_cast<int>(std::ceil(light.x + light.radius)) - originX);
        const int y0 = std::max(0, static_cast<int>(std::floor(light.y - light.radius)) - originY);
        const int y1 = std::min(height - 1, static_cast<int>(std::ceil(light.y + light.radius)) - originY);
        const float inverseRadiusSquared = 1.0f / (light.radius * light.radius);
        for (int y = y0; y <= y1; ++y) {
            const int worldY = originY + y;
            const float dy = static_cast<float>(worldY) - light.y;
            for (int x = x0; x <= x1; ++x) {
                Uint8& alpha = pixels_[static_cast<std::size_t>((x + y * width) * 4 + 3)];
                if (alpha == 0) continue;
                const int worldX = originX + x;
                const float dx = static_cast<float>(worldX) - light.x;
                const float squared = (dx * dx + dy * dy) * inverseRadiusSquared;  // (d / r)^2
                if (squared >= 1.0f) continue;
                const int level = static_cast<int>(255.0f * (1.0f - squared * squared));
                // Pattern anchored to the world (like Minicraft's (x + xScroll) & 3), so it doesn't crawl as you move.
                const bool dark = level / 10 <= kDither[(worldX & 3) + (worldY & 3) * 4];
                const Uint8 shade = dark ? static_cast<Uint8>(255 - level) : 0;
                alpha = std::min(alpha, shade);  // overlapping lights: keep the brightest
            }
        }
    }
    SDL_UpdateTexture(texture_.get(), nullptr, pixels_.data(), width * 4);
    SDL_SetTextureAlphaMod(texture_.get(), static_cast<Uint8>(std::clamp(darkness, 0.0f, 1.0f) * 255.0f));
    const SDL_FRect destination{static_cast<float>(originX) - camera.x(), static_cast<float>(originY) - camera.y(),
                                static_cast<float>(width), static_cast<float>(height)};
    SDL_RenderTexture(renderer, texture_.get(), nullptr, &destination);
}
