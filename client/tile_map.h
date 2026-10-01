#pragma once

#include "texture.h"

#include <SDL3/SDL.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class Camera;

// New values go at the end: saves store tiles by number.
enum class Tile : std::uint8_t { Grass, Sand, Water, Rock, Tree, Dirt, Flower, Farmland, Path, Hole };

const char* tileName(Tile tile);

// Whether a tile blocks movement.
bool isSolid(Tile tile);
// Whether a mob (none of them can swim) or furniture can't stand on a tile: solid tiles, water and holes
// (Minicraft's WaterTile / HoleTile only let swimmers pass).
bool blocksMobs(Tile tile);

// Damage needed to break a tile with punches; 0 = can't be damaged. Minicraft values: tree 20, rock 50.
int maxHealth(Tile tile);

class TileMap {
public:
    static constexpr int kTileSize = 16;

    bool load(SDL_Renderer* renderer, const std::string& atlasPath);

    // Generates the surface like the original Minicraft (see WorldGenerator): an island with an ocean around it,
    // sand beaches, forests, sand patches, rocky mountains and a few lakes. Width and height must be powers of two.
    void generate(std::uint32_t seed, int width, int height);
    // Replaces the map with a saved one. `tiles` and `damage` hold width * height entries, row by row.
    void restore(std::uint32_t seed, int width, int height, std::vector<Tile> tiles, std::vector<std::uint8_t> damage);

    // Finds a walkable tile near the centre of the map; returns its top-left pixel position.
    SDL_FPoint findSpawnPoint() const;

    void draw(SDL_Renderer* renderer, const Camera& camera, float timeSeconds) const;

    struct TileHit {
        Tile tile;    // the tile that was hit (before it broke)
        bool broken;  // reached maxHealth: a tree became grass, a rock became dirt
    };
    // Applies damage to a tile. Returns nothing if the tile can't be damaged (see maxHealth).
    std::optional<TileHit> hurtTile(int tx, int ty, int damage);
    // Replaces a tile (a tool digging, tilling or paving it) and clears its damage.
    void setTile(int tx, int ty, Tile tile);
    int damageAt(int tx, int ty) const { return damage_[index(tx, ty)]; }

    Tile tileAt(int tx, int ty) const { return tiles_[index(tx, ty)]; }
    bool inBounds(int tx, int ty) const { return tx >= 0 && ty >= 0 && tx < width_ && ty < height_; }
    // Outside the map counts as solid, so nothing can walk off the edge.
    bool isSolidAt(int tx, int ty) const { return !inBounds(tx, ty) || isSolid(tileAt(tx, ty)); }
    int width() const { return width_; }
    int height() const { return height_; }
    float pixelWidth() const { return static_cast<float>(width_ * kTileSize); }
    float pixelHeight() const { return static_cast<float>(height_ * kTileSize); }
    std::uint32_t seed() const { return seed_; }
    // Which of the kFlowerVariants flowers grows on a flower tile. It comes from the seed and the tile's 8x8-tile
    // region rather than per-tile data, so nearby flowers match in patches and saves don't need to store it.
    int flowerVariant(int tx, int ty) const;
    const std::vector<Tile>& tiles() const { return tiles_; }
    const std::vector<std::uint8_t>& damage() const { return damage_; }

private:
    std::size_t index(int tx, int ty) const { return static_cast<std::size_t>(ty * width_ + tx); }

    TexturePtr atlas_;
    std::vector<Tile> tiles_;
    std::vector<std::uint8_t> damage_;  // accumulated damage per tile (trees and rocks)
    int width_ = 0;
    int height_ = 0;
    std::uint32_t seed_ = 0;
};
