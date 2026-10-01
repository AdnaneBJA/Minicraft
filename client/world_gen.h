#pragma once

#include "tile_map.h"

#include <cstdint>
#include <vector>

// java.util.Random (48-bit LCG), so world generation follows the same random sequence logic as Minicraft.
class JavaRandom {
public:
    explicit JavaRandom(std::int64_t seed);

    int nextInt(int bound);
    float nextFloat();

private:
    int next(int bits);

    std::int64_t seed_;
};

// Port of the original Minicraft level generators (LevelGen). The surface is an island from midpoint-displacement
// noise with water pushed to the map edges, rocky mountains inland, sand patches with cacti, forest clumps and
// stairs down cut into the rock; on top of the original: a sand beach between the land and the ocean, and fewer
// inland lakes. Below it are three cave levels and above it the sky. Every map is width x height (powers of two).
class WorldGenerator {
public:
    // The surface. Re-rolls until the world has enough of every tile type and at least two stairs down, like
    // Minicraft's createAndValidateTopMap.
    static std::vector<Tile> generate(std::uint32_t seed, int width, int height);
    // Cave level `depth` (1 to 3 below the surface): rock with dirt tunnels, ore veins (iron, gold, then gems) and
    // water pools (lava at the bottom), plus stairs down on the upper two (createAndValidateUndergroundMap).
    static std::vector<Tile> generateUnderground(std::uint32_t seed, int width, int height, int depth);
    // The sky: clouds over an endless fall, cloud cacti and two stairs down (createAndValidateSkyMap).
    static std::vector<Tile> generateSky(std::uint32_t seed, int width, int height);

private:
    static std::vector<Tile> createTopMap(JavaRandom& random, int width, int height);
    static std::vector<Tile> createUndergroundMap(JavaRandom& random, int width, int height, int depth);
    static std::vector<Tile> createSkyMap(JavaRandom& random, int width, int height);
    static void addBeaches(std::vector<Tile>& map, int width, int height);
    // Places up to `max` stairs down on random tiles whose 3x3 surroundings are all `ground`, at least `margin`
    // tiles from the edge.
    static void addStairs(JavaRandom& random, std::vector<Tile>& map, int width, int height, Tile ground, int margin,
                          int max);
};
