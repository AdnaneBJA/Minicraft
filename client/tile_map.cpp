#include "tile_map.h"

#include "camera.h"

#include <algorithm>
#include <cmath>

namespace {

// Atlas layout (16x16 cells): row 0 = grass, sand, rock, tree (transparent); row 1 = 8 water animation frames.
constexpr int kWaterFrames = 8;
constexpr float kWaterFramesPerSecond = 3.0f;

// Deterministic hash of (seed, x, y) to [0, 1].
float hash01(std::uint32_t seed, int x, int y) {
    std::uint32_t h = seed * 374761393u + static_cast<std::uint32_t>(x) * 668265263u +
                      static_cast<std::uint32_t>(y) * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return static_cast<float>(h & 0xFFFFFFu) / static_cast<float>(0xFFFFFFu);
}

// Smoothly interpolated value noise.
float valueNoise(std::uint32_t seed, float x, float y) {
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const float fx = x - static_cast<float>(x0);
    const float fy = y - static_cast<float>(y0);
    const float sx = fx * fx * (3.0f - 2.0f * fx);
    const float sy = fy * fy * (3.0f - 2.0f * fy);
    const float top = std::lerp(hash01(seed, x0, y0), hash01(seed, x0 + 1, y0), sx);
    const float bottom = std::lerp(hash01(seed, x0, y0 + 1), hash01(seed, x0 + 1, y0 + 1), sx);
    return std::lerp(top, bottom, sy);
}

// Three octaves of value noise, normalised to [0, 1]. `scale` is the size of the largest features in tiles.
float fractalNoise(std::uint32_t seed, int x, int y, float scale) {
    float sum = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f / scale;
    float total = 0.0f;
    for (int octave = 0; octave < 3; ++octave) {
        sum += amplitude * valueNoise(seed + static_cast<std::uint32_t>(octave) * 1013u,
                                      static_cast<float>(x) * frequency, static_cast<float>(y) * frequency);
        total += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }
    return sum / total;
}

}  // namespace

const char* tileName(Tile tile) {
    switch (tile) {
        case Tile::Grass: return "Grass";
        case Tile::Sand: return "Sand";
        case Tile::Water: return "Water";
        case Tile::Rock: return "Rock";
        case Tile::Tree: return "Tree";
    }
    return "?";
}

bool TileMap::load(SDL_Renderer* renderer, const std::string& atlasPath) {
    SDL_Surface* surface = SDL_LoadPNG(atlasPath.c_str());
    if (!surface) {
        SDL_Log("Failed to load %s: %s", atlasPath.c_str(), SDL_GetError());
        return false;
    }
    atlas_.reset(SDL_CreateTextureFromSurface(renderer, surface));
    SDL_DestroySurface(surface);
    if (!atlas_) {
        SDL_Log("Failed to create tile texture: %s", SDL_GetError());
        return false;
    }
    SDL_SetTextureScaleMode(atlas_.get(), SDL_SCALEMODE_NEAREST);
    return true;
}

void TileMap::generate(std::uint32_t seed, int width, int height) {
    seed_ = seed;
    width_ = width;
    height_ = height;
    tiles_.assign(static_cast<std::size_t>(width * height), Tile::Grass);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            // Distance from the centre in [0, ~1.4]; pushes elevation down towards the edges to make an island.
            const float dx = (static_cast<float>(x) / static_cast<float>(width)) * 2.0f - 1.0f;
            const float dy = (static_cast<float>(y) / static_cast<float>(height)) * 2.0f - 1.0f;
            const float edge = dx * dx + dy * dy;
            const float elevation = fractalNoise(seed, x, y, 24.0f) - 0.30f * edge;
            const float forest = fractalNoise(seed + 7919u, x, y, 12.0f);

            Tile tile = Tile::Grass;
            if (elevation < 0.25f) {
                tile = Tile::Water;
            } else if (elevation < 0.30f) {
                tile = Tile::Sand;
            } else if (elevation > 0.60f) {
                tile = Tile::Rock;
            } else if ((forest > 0.6f && hash01(seed + 31u, x, y) > 0.35f) || hash01(seed + 57u, x, y) > 0.98f) {
                tile = Tile::Tree;
            }
            tiles_[static_cast<std::size_t>(y * width + x)] = tile;
        }
    }
}

SDL_FPoint TileMap::findSpawnPoint() const {
    // Search outward in growing squares from the centre for the first grass tile.
    const int cx = width_ / 2;
    const int cy = height_ / 2;
    for (int radius = 0; radius < std::max(width_, height_); ++radius) {
        for (int ty = cy - radius; ty <= cy + radius; ++ty) {
            for (int tx = cx - radius; tx <= cx + radius; ++tx) {
                if (inBounds(tx, ty) && tileAt(tx, ty) == Tile::Grass) {
                    return {static_cast<float>(tx * kTileSize), static_cast<float>(ty * kTileSize)};
                }
            }
        }
    }
    return {static_cast<float>(cx * kTileSize), static_cast<float>(cy * kTileSize)};
}

void TileMap::draw(SDL_Renderer* renderer, const Camera& camera, float timeSeconds) const {
    const int waterFrame = static_cast<int>(timeSeconds * kWaterFramesPerSecond) % kWaterFrames;

    // Only draw the tiles the camera can see.
    const int firstX = std::max(0, static_cast<int>(camera.x()) / kTileSize);
    const int firstY = std::max(0, static_cast<int>(camera.y()) / kTileSize);
    const int lastX = std::min(width_ - 1, static_cast<int>(camera.x() + camera.width()) / kTileSize);
    const int lastY = std::min(height_ - 1, static_cast<int>(camera.y() + camera.height()) / kTileSize);

    for (int ty = firstY; ty <= lastY; ++ty) {
        for (int tx = firstX; tx <= lastX; ++tx) {
            const float x = static_cast<float>(tx * kTileSize) - camera.x();
            const float y = static_cast<float>(ty * kTileSize) - camera.y();
            switch (tileAt(tx, ty)) {
                case Tile::Grass: drawSprite(renderer, 0, 0, x, y); break;
                case Tile::Sand: drawSprite(renderer, 1, 0, x, y); break;
                case Tile::Rock: drawSprite(renderer, 2, 0, x, y); break;
                case Tile::Water: drawSprite(renderer, waterFrame, 1, x, y); break;
                case Tile::Tree:
                    drawSprite(renderer, 0, 0, x, y);  // grass underneath
                    drawSprite(renderer, 3, 0, x, y);
                    break;
            }
        }
    }
}

void TileMap::drawSprite(SDL_Renderer* renderer, int column, int row, float x, float y) const {
    constexpr float size = static_cast<float>(kTileSize);
    const SDL_FRect source{static_cast<float>(column) * size, static_cast<float>(row) * size, size, size};
    const SDL_FRect destination{x, y, size, size};
    SDL_RenderTexture(renderer, atlas_.get(), &source, &destination);
}
