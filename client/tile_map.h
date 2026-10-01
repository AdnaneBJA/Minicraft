#pragma once

#include "texture.h"

#include <SDL3/SDL.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class Camera;

enum class Tile : std::uint8_t { Grass, Sand, Water, Rock, Tree, Dirt };

const char* tileName(Tile tile);

// Whether a tile blocks movement.
bool isSolid(Tile tile);

// Damage needed to break a tile with punches; 0 = can't be damaged. Minicraft values: tree 20, rock 50.
int maxHealth(Tile tile);

class TileMap {
public:
    static constexpr int kTileSize = 16;

    bool load(SDL_Renderer* renderer, const std::string& atlasPath);

    // Procedurally generates an island: water at the edges, then sand, grass/forest, and rock in the highlands.
    void generate(std::uint32_t seed, int width, int height);

    // Finds a walkable tile near the centre of the map; returns its top-left pixel position.
    SDL_FPoint findSpawnPoint() const;

    void draw(SDL_Renderer* renderer, const Camera& camera, float timeSeconds) const;

    struct TileHit {
        Tile tile;    // the tile that was hit (before it broke)
        bool broken;  // reached maxHealth: a tree became grass, a rock became dirt
    };
    // Applies damage to a tile. Returns nothing if the tile can't be damaged (see maxHealth).
    std::optional<TileHit> hurtTile(int tx, int ty, int damage);
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

private:
    std::size_t index(int tx, int ty) const { return static_cast<std::size_t>(ty * width_ + tx); }

    TexturePtr atlas_;
    std::vector<Tile> tiles_;
    std::vector<std::uint8_t> damage_;  // accumulated damage per tile (trees and rocks)
    int width_ = 0;
    int height_ = 0;
    std::uint32_t seed_ = 0;
};
