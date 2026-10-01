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
enum class Tile : std::uint8_t {
    Grass, Sand, Water, Rock, Tree, Dirt, Flower, Farmland, Path, Hole,
    StairsDown, StairsUp, IronOre, GoldOre, GemOre, HardRock, Lava, Cactus, Sapling, CactusSapling, Wheat,
    Cloud, InfiniteFall, CloudCactus, Torch, WoodPlanks, StoneBricks, WoodWall, StoneWall, WoodDoor, StoneDoor,
};
constexpr int kTileCount = static_cast<int>(Tile::StoneDoor) + 1;

const char* tileName(Tile tile);

// Whether a tile blocks the player (doors only while closed: `data` != 0 means open). Water and lava are
// swimmable; the edge of the sky (InfiniteFall) is not.
bool isSolid(Tile tile, std::uint8_t data = 0);
// Whether a mob (none of them can swim) or furniture can't stand on a tile: solid tiles, water, lava, holes and
// the edge of the sky (Minicraft's WaterTile / HoleTile only let swimmers pass).
bool blocksMobs(Tile tile, std::uint8_t data = 0);
// Ore and gems, which only a pickaxe can mine.
bool isOre(Tile tile);

// Damage needed to break a tile; 0 = can't be damaged by hits. Minicraft values: tree 20, rock 50, cactus 10,
// hard rock 200. Ores roll their own break point on every hit (OreTile.hurt).
int maxHealth(Tile tile);

class TileMap {
public:
    static constexpr int kTileSize = 16;

    // The tile atlas (tiles.png) the map draws from; every level shares one, owned by the World.
    void setAtlas(SDL_Texture* atlas) { atlas_ = atlas; }

    // Replaces the map. `tiles` and `data` hold width * height entries, row by row. `sky`: drawn as the cloud
    // level (stairs sit on clouds and the edge shows the sky below).
    void restore(std::uint32_t seed, int width, int height, std::vector<Tile> tiles, std::vector<std::uint8_t> data,
                 bool sky = false);

    // Finds a walkable tile near the centre of the map; returns its top-left pixel position.
    SDL_FPoint findSpawnPoint() const;

    void draw(SDL_Renderer* renderer, const Camera& camera, float timeSeconds) const;

    struct TileHit {
        Tile tile;    // the tile that was hit (before it broke)
        bool broken;  // reached its break point and turned into what's under it
        std::uint8_t data;  // the tile's data before the hit (a crop's age, a torch's base tile)
    };
    // Applies damage to a tile. Returns nothing if the tile can't be damaged (see maxHealth).
    std::optional<TileHit> hurtTile(int tx, int ty, int damage);
    // Replaces a tile and sets its data (damage, age, a torch's base tile, a door's open flag).
    void setTile(int tx, int ty, Tile tile, std::uint8_t data = 0);
    void setData(int tx, int ty, std::uint8_t data) { data_[index(tx, ty)] = data; }
    std::uint8_t dataAt(int tx, int ty) const { return data_[index(tx, ty)]; }
    int damageAt(int tx, int ty) const { return data_[index(tx, ty)]; }

    // Minicraft's random tile ticks (Level.tick): `count` random tiles near (tx, ty) grow, spread or flow.
    void tickRandomTiles(int tx, int ty, int radius, int count);

    Tile tileAt(int tx, int ty) const { return tiles_[index(tx, ty)]; }
    // The tile as far as looks and connections go: a torch shows the tile it was placed on.
    Tile groundAt(int tx, int ty) const;
    bool inBounds(int tx, int ty) const { return tx >= 0 && ty >= 0 && tx < width_ && ty < height_; }
    // Outside the map counts as solid, so nothing can walk off the edge.
    bool isSolidAt(int tx, int ty) const { return !inBounds(tx, ty) || isSolid(tileAt(tx, ty), dataAt(tx, ty)); }
    bool blocksMobsAt(int tx, int ty) const { return !inBounds(tx, ty) || blocksMobs(tileAt(tx, ty), dataAt(tx, ty)); }
    int width() const { return width_; }
    int height() const { return height_; }
    float pixelWidth() const { return static_cast<float>(width_ * kTileSize); }
    float pixelHeight() const { return static_cast<float>(height_ * kTileSize); }
    std::uint32_t seed() const { return seed_; }
    // Which of the kFlowerVariants flowers grows on a flower tile. It comes from the seed and the tile's 8x8-tile
    // region rather than per-tile data, so nearby flowers match in patches and saves don't need to store it.
    int flowerVariant(int tx, int ty) const;
    const std::vector<Tile>& tiles() const { return tiles_; }
    const std::vector<std::uint8_t>& data() const { return data_; }

private:
    std::size_t index(int tx, int ty) const { return static_cast<std::size_t>(ty * width_ + tx); }
    void tickTile(int tx, int ty);
    void drawTile(SDL_Renderer* renderer, int tx, int ty, Tile tile, float x, float y, int lavaFrame,
                  int waterFrame) const;

    SDL_Texture* atlas_ = nullptr;
    std::vector<Tile> tiles_;
    // Per-tile data (Minicraft's tile data): accumulated damage for trees, rock and ores, the age of saplings and
    // wheat, the tile under a torch, whether a door is open.
    std::vector<std::uint8_t> data_;
    int width_ = 0;
    int height_ = 0;
    std::uint32_t seed_ = 0;
    bool sky_ = false;
};
