#pragma once

#include "tile_map.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

// Tile collision shared by the player and mobs: moving a box along one axis, stopping flush against the first
// solid tile in the way. `isSolid(tx, ty)` decides which tiles block (e.g. mobs that can't swim treat water as solid).
namespace collision {

constexpr float kTileSize = static_cast<float>(TileMap::kTileSize);

// Index of the tile containing a world coordinate.
inline int tileIndex(float worldValue) { return static_cast<int>(std::floor(worldValue / kTileSize)); }

// Last tile covered by the half-open span [start, end): an end edge touching a tile doesn't count as overlap.
inline int lastTileIndex(float start, float end) {
    const bool endsOnEdge = std::fmod(end, kTileSize) == 0.0f;
    return std::max(tileIndex(start), tileIndex(end) - (endsOnEdge ? 1 : 0));
}

// Returns how far `box` can move by `delta` along x (the full delta, or up to the blocking tile's edge).
template <typename IsSolid>
float allowedMoveX(const SDL_FRect& box, float delta, IsSolid isSolid) {
    if (delta == 0.0f) return 0.0f;
    const float left = box.x + delta;
    const float right = left + box.w;
    const int firstRow = tileIndex(box.y);
    const int lastRow = lastTileIndex(box.y, box.y + box.h);
    // Only the column the leading edge moves into can newly block.
    const int column = delta > 0.0f ? lastTileIndex(left, right) : tileIndex(left);
    for (int row = firstRow; row <= lastRow; ++row) {
        if (isSolid(column, row)) {
            const float edge = delta > 0.0f ? static_cast<float>(column) * kTileSize - box.w
                                            : static_cast<float>(column + 1) * kTileSize;
            return edge - box.x;
        }
    }
    return delta;
}

// Same as allowedMoveX, along y.
template <typename IsSolid>
float allowedMoveY(const SDL_FRect& box, float delta, IsSolid isSolid) {
    if (delta == 0.0f) return 0.0f;
    const float top = box.y + delta;
    const float bottom = top + box.h;
    const int firstColumn = tileIndex(box.x);
    const int lastColumn = lastTileIndex(box.x, box.x + box.w);
    const int row = delta > 0.0f ? lastTileIndex(top, bottom) : tileIndex(top);
    for (int column = firstColumn; column <= lastColumn; ++column) {
        if (isSolid(column, row)) {
            const float edge = delta > 0.0f ? static_cast<float>(row) * kTileSize - box.h
                                            : static_cast<float>(row + 1) * kTileSize;
            return edge - box.y;
        }
    }
    return delta;
}

}  // namespace collision
