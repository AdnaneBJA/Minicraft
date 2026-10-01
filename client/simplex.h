#pragma once

#include <cstdint>

// 3D OpenSimplex2 noise ("fast" variant, noise3_ImproveXY), ported from Minicraft+'s util/Simplex.java, which is
// K.jpg's OpenSimplex2. Same seed and inputs give the same values as Minicraft+. Output is roughly in [-1, 1].
class SimplexNoise {
public:
    explicit SimplexNoise(std::int64_t seed) : seed_(seed) {}

    float noise3(double x, double y, double z) const;

private:
    float unrotatedBase(double xr, double yr, double zr) const;

    std::int64_t seed_;
};
