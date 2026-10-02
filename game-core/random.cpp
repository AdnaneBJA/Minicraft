#include "random.h"

#include <algorithm>
#include <cmath>

namespace {

std::uint32_t rotl(std::uint32_t x, int k) { return (x << k) | (x >> (32 - k)); }

// SplitMix64: spreads any seed (even 0) over the generator's 128-bit state.
std::uint64_t splitMix(std::uint64_t& x) {
    std::uint64_t z = (x += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

}  // namespace

void Random::reseed(std::uint64_t seed) {
    const std::uint64_t a = splitMix(seed);
    const std::uint64_t b = splitMix(seed);
    state_[0] = static_cast<std::uint32_t>(a);
    state_[1] = static_cast<std::uint32_t>(a >> 32);
    state_[2] = static_cast<std::uint32_t>(b);
    state_[3] = static_cast<std::uint32_t>(b >> 32);
}

std::uint32_t Random::nextBits() {
    const std::uint32_t result = rotl(state_[1] * 5, 7) * 9;
    const std::uint32_t t = state_[1] << 9;
    state_[2] ^= state_[0];
    state_[3] ^= state_[1];
    state_[1] ^= state_[2];
    state_[0] ^= state_[3];
    state_[2] ^= t;
    state_[3] = rotl(state_[3], 11);
    return result;
}

int Random::nextInt(int bound) {
    if (bound <= 0) return 0;
    // Multiply-shift: maps 32 random bits onto [0, bound) without a modulo.
    return static_cast<int>((static_cast<std::uint64_t>(nextBits()) * static_cast<std::uint32_t>(bound)) >> 32);
}

float Random::nextFloat() { return static_cast<float>(nextBits() >> 8) / static_cast<float>(1u << 24); }

float Random::gaussian() {
    const float u1 = std::max(nextFloat(), 1e-6f);
    const float u2 = nextFloat();
    return std::sqrt(-2.0f * std::log(u1)) * std::cos(2.0f * 3.14159265f * u2);
}
