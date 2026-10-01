#pragma once

#include "simplex.h"
#include "tile_map.h"

#include <cstdint>

// Port of Minicraft+'s surface world generation (LevelGen, LevelNoise and the biome classes):
// 10 layers of OpenSimplex noise at scales 1..8192 give each tile a temperature, height and humidity; the biome
// with the highest weight wins, and each biome decides the tile from more noise.
class WorldGenerator {
public:
    enum class Biome { Surface, Desert, Forest, Ocean, River, RiverBank, Rock };

    explicit WorldGenerator(std::int64_t seed) : noise_(seed) {}

    Biome biomeAt(int x, int y) const;
    Tile tileAt(int x, int y) const;

private:
    // LevelNoise sample: scale index 0..9 is scale 1, 4, 8, 16, 32, 64, 128, 512, 2048, 8192.
    double sample(int scaleIndex, int layer, int x, int y) const;
    double octave(int x, int y, int layer, const double (&weights)[10]) const;
    double temperature(int x, int y) const;
    double height(int x, int y) const;
    double humidity(int x, int y) const;

    SimplexNoise noise_;
};

const char* biomeName(WorldGenerator::Biome biome);
