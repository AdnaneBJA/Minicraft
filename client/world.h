#pragma once

#include "dropped_items.h"
#include "furniture.h"
#include "mobs.h"
#include "projectiles.h"
#include "tile_map.h"

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <string>

// One level of the world (Minicraft's Level): its tiles and everything on them.
class Level {
public:
    explicit Level(int depth) : depth_(depth) {}

    // 1 = sky, 0 = surface, -1..-3 = caves.
    int depth() const { return depth_; }
    bool isSky() const { return depth_ > 0; }
    bool isUnderground() const { return depth_ < 0; }
    // "Surface", "Sky" or "Cave B1".."Cave B3".
    std::string name() const;

    // Mobs, dropped items and projectiles go; tiles and furniture stay.
    void clearEntities();

    TileMap map;
    Mobs mobs;
    DroppedItems drops;
    Furniture furniture;
    Projectiles projectiles;

private:
    int depth_;
};

// The five levels of a world, top to bottom: the sky, the surface and three caves. Stairs down on one level lead to
// stairs up at the same spot on the level below (Minicraft's World.levels).
class World {
public:
    static constexpr int kLevelCount = 5;
    static constexpr int kSkyIndex = 0;
    static constexpr int kSurfaceIndex = 1;
    static constexpr int kSize = 256;  // tiles per side on every level

    World();

    // The shared tile atlas (tiles.png).
    bool load(SDL_Renderer* renderer, const std::string& tilesPath);

    // Generates every level from `seed` and links their stairs; the player starts on the surface.
    void generate(std::uint32_t seed);
    // Puts a saved level's tiles back. Call linkStairs() once every level is restored.
    void restoreLevel(int index, std::uint32_t seed, std::vector<Tile> tiles, std::vector<std::uint8_t> data);
    // Makes the stairs match up between neighbouring levels (Level's stair check): stairs down above become stairs
    // up below with a room around them (hard rock under the sky, dirt in the caves), and stairs up below get stairs
    // down above. Stairs down (below the sky) get a dirt room too, so arriving on them from below never walls the
    // player in. Used after generating and after loading a save.
    void linkStairs();
    // Puts the Air Wizard in the middle of the sky if it hasn't been beaten and isn't there.
    void spawnBoss();

    Level& level(int index) { return levels_[static_cast<std::size_t>(index)]; }
    const Level& level(int index) const { return levels_[static_cast<std::size_t>(index)]; }
    Level& current() { return level(currentIndex_); }
    const Level& current() const { return level(currentIndex_); }
    int currentIndex() const { return currentIndex_; }
    // Moves the player's view to another level (they keep their position).
    void setCurrent(int index);
    std::uint32_t seed() const { return seed_; }
    void clearEntities();

    bool airWizardBeaten = false;

private:
    TexturePtr atlas_;
    std::array<Level, kLevelCount> levels_;
    int currentIndex_ = kSurfaceIndex;
    std::uint32_t seed_ = 0;
};
