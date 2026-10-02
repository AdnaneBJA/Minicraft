#pragma once

#include <cstdint>

// The simulation's random numbers: a seeded generator (xoshiro128**) owned by the Simulation and passed to whatever
// needs randomness. No global state, so the same seed and the same inputs always replay the same game, which the
// server, the bots and the Python environment rely on.
class Random {
public:
    explicit Random(std::uint64_t seed = 0) { reseed(seed); }

    void reseed(std::uint64_t seed);

    // Uniform in [0, bound); 0 if bound <= 0.
    int nextInt(int bound);
    // Uniform in [0, 1).
    float nextFloat();
    // Standard normal (Box-Muller), like Java's Random.nextGaussian().
    float gaussian();

    std::uint32_t nextBits();

private:
    std::uint32_t state_[4] = {};
};
