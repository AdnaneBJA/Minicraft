#pragma once

#include "random.h"

// Minicraft's "toss" motion shared by dropped items and damage numbers: a small random push, then a bouncing
// fall with a height z above the ground. Advanced in fixed 60 Hz ticks, like the original.
struct Bounce {
    float x = 0.0f;
    float y = 0.0f;
    float z = 2.0f;
    float xa = 0.0f;
    float ya = 0.0f;
    float za = 0.0f;

    // Starts the toss from (x, y). `upward` is the base vertical speed (items: 1, damage numbers: 2).
    static Bounce toss(float startX, float startY, float upward, Random& rng) {
        Bounce b;
        b.x = startX;
        b.y = startY;
        b.xa = rng.gaussian() * 0.3f;
        b.ya = rng.gaussian() * 0.2f;
        b.za = rng.nextFloat() * 0.7f + upward;
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
};
