#include "world_gen.h"

#include <cmath>
#include <limits>

namespace {

constexpr int kLayers = 10;            // LevelNoise default
constexpr double kLayerSpacing = 100;  // NOISE_LAYER_DIFF
constexpr double kScales[kLayers] = {1, 4, 8, 16, 32, 64, 128, 512, 2048, 8192};
enum Scale { S1, S4, S8, S16, S32, S64, S128, S512, S2048, S8192 };

// Biome "climates" from the biome constructors: temperature, height, humidity, rarity.
struct Climate {
    float temperature;
    float height;
    float humidity;
    float rarity;
};
constexpr Climate kSurface{-0.25f, 0.0f, 0.3f, 1.0f};
constexpr Climate kDesert{0.3f, 0.0f, -0.6f, 1.0f};
constexpr Climate kForest{-0.1f, 0.0f, 0.5f, 1.0f};

// Biome.getGenerationWeight: closer climate = higher weight.
float climateWeight(const Climate& c, float temperature, float height, float humidity) {
    const float x = temperature - c.temperature;
    const float y = height - c.height;
    const float z = humidity - c.humidity;
    return c.rarity / (x * x + y * y + z * z);
}

}  // namespace

const char* biomeName(WorldGenerator::Biome biome) {
    switch (biome) {
        case WorldGenerator::Biome::Surface: return "Surface";
        case WorldGenerator::Biome::Desert: return "Desert";
        case WorldGenerator::Biome::Forest: return "Forest";
        case WorldGenerator::Biome::Ocean: return "Ocean";
        case WorldGenerator::Biome::River: return "River";
        case WorldGenerator::Biome::RiverBank: return "River bank";
        case WorldGenerator::Biome::Rock: return "Rock";
    }
    return "?";
}

double WorldGenerator::sample(int scaleIndex, int layer, int x, int y) const {
    const double scale = kScales[scaleIndex];
    const double z = static_cast<double>(layer + kLayers * scaleIndex) * kLayerSpacing;
    return noise_.noise3(static_cast<double>(x) / scale, static_cast<double>(y) / scale, z);
}

double WorldGenerator::octave(int x, int y, int layer, const double (&weights)[10]) const {
    double sum = 0.0;
    for (int i = 0; i < kLayers; ++i) {
        sum += sample(i, layer, x, y) * weights[i];
    }
    return sum;
}

double WorldGenerator::temperature(int x, int y) const {
    static constexpr double weights[10] = {0.01, 0.02, 0.04, 0.08, 0.16, 0.32, 0.64, 0.05, 0.04, 0.01};
    return octave(x, y, kLayers - 1, weights);
}

double WorldGenerator::height(int x, int y) const {
    static constexpr double weights[10] = {0.005, 0.01, 0.02, 0.01, 0.02, 0.05, 0.1, 0.2, 0.7, 0.2};
    return octave(x, y, kLayers - 2, weights);
}

double WorldGenerator::humidity(int x, int y) const {
    static constexpr double weights[10] = {0.02, 0.04, 0.07, 0.1, 0.4, 0.3, 0.1, 0.05, 0.02, 0.01};
    return octave(x, y, kLayers - 3, weights);
}

WorldGenerator::Biome WorldGenerator::biomeAt(int x, int y) const {
    const float t = static_cast<float>(temperature(x, y));
    const float h = static_cast<float>(height(x, y));
    const float m = static_cast<float>(humidity(x, y));
    // River and river bank follow the zero line of the difference between two large-scale noise layers.
    const float riverDistance = static_cast<float>(std::abs(sample(S128, kLayers - 1, x, y) - sample(S128, kLayers - 2, x, y)));
    const float rockBase = h * 3.0f - 1.0f;

    const struct {
        Biome biome;
        float weight;
    } candidates[] = {
        {Biome::Surface, climateWeight(kSurface, t, h, m)},
        {Biome::Desert, climateWeight(kDesert, t, h, m)},
        {Biome::Forest, climateWeight(kForest, t, h, m)},
        {Biome::Ocean, std::pow(-h * 4.0f, 5.0f)},                       // OceanBiome: low ground
        {Biome::River, std::min(0.5f / riverDistance, 10.0f)},            // RiverBiome
        {Biome::RiverBank, std::min(0.55f / riverDistance, 3.0f)},        // RiverBankBiome
        {Biome::Rock, std::pow(rockBase, 13.0f)},                         // RockBiome: high ground
    };
    Biome best = Biome::Surface;
    float bestWeight = -std::numeric_limits<float>::infinity();
    for (const auto& candidate : candidates) {
        if (candidate.weight > bestWeight) {
            bestWeight = candidate.weight;
            best = candidate.biome;
        }
    }
    return best;
}

Tile WorldGenerator::tileAt(int x, int y) const {
    // Flowers, cactus and stairs from Minicraft don't exist here yet: they become grass, sand and rock.
    switch (biomeAt(x, y)) {
        case Biome::Ocean:
        case Biome::River: return Tile::Water;
        case Biome::RiverBank: return Tile::Sand;
        case Biome::Rock: return Tile::Rock;
        case Biome::Forest: return sample(S1, 0, x, y) < 0.6 ? Tile::Tree : Tile::Grass;
        case Biome::Surface: {
            const double val = std::abs(sample(S64, 0, x, y) - sample(S64, 1, x, y));
            const double mval =
                std::abs(std::abs(sample(S16, 0, x, y) - sample(S16, 1, x, y)) - sample(S16, 2, x, y));
            if (val > 0.75 && mval < 0.35) return Tile::Rock;
            if (sample(S16, 2, x, y) < -0.6 && sample(S1, 0, x, y) < 0.4) return Tile::Tree;
            return Tile::Grass;
        }
        case Biome::Desert: {
            const double val = std::abs(sample(S64, 0, x, y) - sample(S64, 1, x, y));
            const double mval =
                std::abs(std::abs(sample(S16, 0, x, y) - sample(S16, 1, x, y)) - sample(S16, 2, x, y));
            if (val > 0.5 && mval < 0.5) return Tile::Rock;
            return Tile::Sand;
        }
    }
    return Tile::Grass;
}
