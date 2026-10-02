#pragma once

#include <algorithm>

// Plain geometry for the simulation (game-core has no SDL). World coordinates are pixels, 16 per tile.
struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct Point {
    int x = 0;
    int y = 0;
};

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

// Whether two rectangles overlap. Edges that only touch count as overlapping, like SDL_HasRectIntersectionFloat,
// which the collision rules were tuned against.
inline bool intersects(const Rect& a, const Rect& b) {
    if (a.w < 0.0f || a.h < 0.0f || b.w < 0.0f || b.h < 0.0f) return false;
    return std::max(a.x, b.x) <= std::min(a.x + a.w, b.x + b.w) &&
           std::max(a.y, b.y) <= std::min(a.y + a.h, b.y + b.h);
}
