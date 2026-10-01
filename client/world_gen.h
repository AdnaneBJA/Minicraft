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

// Port of the original Minicraft surface generator (LevelGen.createTopMap): an island from midpoint-displacement
// noise with water pushed to the map edges, rocky mountains inland, sand patches and forest clumps. On top of the
// original: a sand beach between the land and the ocean, and fewer inland lakes.
class WorldGenerator {
public:
    // Generates a width x height map (both powers of two). Re-rolls until the world has enough of every tile type,
    // like Minicraft's createAndValidateTopMap.
    static std::vector<Tile> generate(std::uint32_t seed, int width, int height);

private:
    static std::vector<Tile> createTopMap(JavaRandom& random, int width, int height);
    static void addBeaches(std::vector<Tile>& map, int width, int height);
};
