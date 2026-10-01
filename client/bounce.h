#pragma once

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

// Minicraft's "toss" motion shared by dropped items and damage numbers: a small random push, then a bouncing
// fall with a height z above the ground. Advanced in fixed 60 Hz ticks, like the original.
struct Bounce {
    static constexpr float kTick = 1.0f / 60.0f;

    float x = 0.0f;
    float y = 0.0f;
    float z = 2.0f;
    float xa = 0.0f;
    float ya = 0.0f;
    float za = 0.0f;

    // Starts the toss from (x, y). `upward` is the base vertical speed (items: 1, damage numbers: 2).
    static Bounce toss(float startX, float startY, float upward) {
        Bounce b;
        b.x = startX;
        b.y = startY;
        b.xa = gaussian() * 0.3f;
        b.ya = gaussian() * 0.2f;
        b.za = SDL_randf() * 0.7f + upward;
        return b;
    }

    void tick() {
        x += xa;
        y += ya;
        z += za;
        if (z < 0.0f) {  // hit the ground: bounce at half speed and lose some sideways speed
            z = 0.0f;
            za *= -0.5f;
            xa *= 0.6f;
            ya *= 0.6f;
        }
        za -= 0.15f;
    }

    // Standard normal random number (Box-Muller), like Java's Random.nextGaussian().
    static float gaussian() {
        const float u1 = std::max(SDL_randf(), 1e-6f);
        const float u2 = SDL_randf();
        return std::sqrt(-2.0f * std::log(u1)) * std::cos(2.0f * 3.14159265f * u2);
    }
};
