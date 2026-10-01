#include "tile_map.h"

#include "camera.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr int kWaterFrames = 8;
constexpr float kWaterFramesPerSecond = 3.0f;
constexpr float kHalf = 8.0f;  // tiles are drawn as four 8x8 quadrants

// Positions (in pixels) of each texture in assets/sprites/tiles.png.
struct AtlasPos {
    float x;
    float y;
};
constexpr AtlasPos kGrass{0, 0}, kSand{16, 0}, kRock{32, 0}, kDirt{48, 0};
constexpr AtlasPos kOak{64, 0};         // tree (transparent background), exactly one per tree tile
constexpr AtlasPos kRockCorner{80, 0};  // 2x2 quadrants for rock's inner corners
constexpr AtlasPos kWaterFrame0{0, 16};
// Border sheets: 3x3 grid of 8x8 pieces. Row/column 0 = top/left edge, 1 = no edge, 2 = bottom/right edge.
constexpr AtlasPos kGrassBorder{0, 32}, kSandBorder{24, 32}, kWaterBorder{48, 32}, kRockBorder{72, 32};

// A tile type drawn with Minicraft's connected-texture scheme.
struct ConnectedTexture {
    AtlasPos full;
    AtlasPos border;
    const AtlasPos* corner;  // optional inner-corner sheet
};

// Which neighbouring tiles blend seamlessly with a tile of the given kind (no border between them).
bool connects(Tile kind, Tile other) {
    switch (kind) {
        case Tile::Grass:
        case Tile::Tree: return other == Tile::Grass || other == Tile::Tree;
        case Tile::Sand: return other == Tile::Sand;
        case Tile::Water: return other == Tile::Water;
        case Tile::Rock: return other == Tile::Rock;
    }
    return false;
}

void drawPiece(SDL_Renderer* renderer, SDL_Texture* atlas, float srcX, float srcY, float size, float x, float y,
               SDL_FlipMode flip = SDL_FLIP_NONE) {
    const SDL_FRect source{srcX, srcY, size, size};
    const SDL_FRect destination{x, y, size, size};
    SDL_RenderTextureRotated(renderer, atlas, &source, &destination, 0.0, nullptr, flip);
}

// Port of the Minicraft+ border rendering (SpriteAnimation.render). Each quadrant looks at the two neighbours it
// touches and the diagonal between them, then picks an edge piece, the plain centre, or an inner corner.
void drawConnected(SDL_Renderer* renderer, SDL_Texture* atlas, const TileMap& map, int tx, int ty, float x, float y,
                   Tile kind, const ConnectedTexture& texture) {
    const auto connected = [&](int nx, int ny) { return !map.inBounds(nx, ny) || connects(kind, map.tileAt(nx, ny)); };
    for (const int v : {-1, 1}) {      // -1 = top half, 1 = bottom half
        for (const int h : {-1, 1}) {  // -1 = left half, 1 = right half
            const bool vertical = connected(tx, ty + v);
            const bool horizontal = connected(tx + h, ty);
            const float qx = x + (h < 0 ? 0.0f : kHalf);
            const float qy = y + (v < 0 ? 0.0f : kHalf);
            if (vertical && horizontal) {
                if (!connected(tx + h, ty + v) && texture.corner) {
                    // Inner corner: matching quadrant of the corner sheet, flipped both ways (as Minicraft+ does).
                    drawPiece(renderer, atlas, texture.corner->x + (h < 0 ? 0.0f : kHalf),
                              texture.corner->y + (v < 0 ? 0.0f : kHalf), kHalf, qx, qy,
                              static_cast<SDL_FlipMode>(SDL_FLIP_HORIZONTAL | SDL_FLIP_VERTICAL));
                } else {
                    // Interior: use the full tile texture, which has the speckles (and water's animation), rather
                    // than the plain centre of the border sheet. Minicraft+ calls this "singleton with connective".
                    // It takes the opposite quadrant of the full texture, as Minicraft+ does.
                    drawPiece(renderer, atlas, texture.full.x + (h < 0 ? kHalf : 0.0f),
                              texture.full.y + (v < 0 ? kHalf : 0.0f), kHalf, qx, qy);
                }
            } else {
                const float row = vertical ? 1.0f : (v < 0 ? 0.0f : 2.0f);
                const float column = horizontal ? 1.0f : (h < 0 ? 0.0f : 2.0f);
                drawPiece(renderer, atlas, texture.border.x + column * kHalf, texture.border.y + row * kHalf, kHalf,
                          qx, qy);
            }
        }
    }
}

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

bool isSolid(Tile tile) {
    return tile == Tile::Water || tile == Tile::Rock || tile == Tile::Tree;
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
    damage_.assign(static_cast<std::size_t>(width * height), 0);

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

bool TileMap::hurtTile(int tx, int ty, int damage) {
    if (!inBounds(tx, ty) || tileAt(tx, ty) != Tile::Tree) {
        return false;
    }
    const int total = damageAt(tx, ty) + damage;
    if (total >= kTreeHealth) {
        tiles_[index(tx, ty)] = Tile::Grass;
        damage_[index(tx, ty)] = 0;
    } else {
        damage_[index(tx, ty)] = static_cast<std::uint8_t>(total);
    }
    return true;
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
    SDL_Texture* atlas = atlas_.get();
    constexpr float size = static_cast<float>(kTileSize);
    const ConnectedTexture grass{kGrass, kGrassBorder, nullptr};
    const ConnectedTexture sand{kSand, kSandBorder, nullptr};
    const ConnectedTexture rock{kRock, kRockBorder, &kRockCorner};
    const ConnectedTexture water{{kWaterFrame0.x + static_cast<float>(waterFrame) * size, kWaterFrame0.y},
                                 kWaterBorder, nullptr};

    // Only draw the tiles the camera can see.
    const int firstX = std::max(0, static_cast<int>(camera.x()) / kTileSize);
    const int firstY = std::max(0, static_cast<int>(camera.y()) / kTileSize);
    const int lastX = std::min(width_ - 1, static_cast<int>(camera.x() + camera.width()) / kTileSize);
    const int lastY = std::min(height_ - 1, static_cast<int>(camera.y() + camera.height()) / kTileSize);

    for (int ty = firstY; ty <= lastY; ++ty) {
        for (int tx = firstX; tx <= lastX; ++tx) {
            const float x = static_cast<float>(tx * kTileSize) - camera.x();
            const float y = static_cast<float>(ty * kTileSize) - camera.y();
            // Everything sits on dirt, which shows through the transparent rims of the border pieces.
            drawPiece(renderer, atlas, kDirt.x, kDirt.y, size, x, y);
            const Tile tile = tileAt(tx, ty);
            switch (tile) {
                case Tile::Grass: drawConnected(renderer, atlas, *this, tx, ty, x, y, tile, grass); break;
                case Tile::Sand: drawConnected(renderer, atlas, *this, tx, ty, x, y, tile, sand); break;
                case Tile::Rock: drawConnected(renderer, atlas, *this, tx, ty, x, y, tile, rock); break;
                case Tile::Water: drawConnected(renderer, atlas, *this, tx, ty, x, y, tile, water); break;
                case Tile::Tree:
                    drawConnected(renderer, atlas, *this, tx, ty, x, y, tile, grass);  // grass underneath
                    drawPiece(renderer, atlas, kOak.x, kOak.y, size, x, y);
                    break;
            }
        }
    }
}
