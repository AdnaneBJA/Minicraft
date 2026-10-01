#include "simplex.h"

#include <array>

namespace {

// Java longs wrap on overflow; do the arithmetic on uint64_t (well defined) and reinterpret as int64_t.
constexpr std::int64_t kPrimeX = 0x5205402B9270C86FLL;
constexpr std::int64_t kPrimeY = 0x598CD327003817B5LL;
constexpr std::int64_t kPrimeZ = 0x5BCC226E9FA0BACBLL;
constexpr std::int64_t kHashMultiplier = 0x53A3F72DEEC546F5LL;
constexpr std::int64_t kSeedFlip3D = -0x52D547B2E96ED629LL;
constexpr double kRoot3Over3 = 0.577350269189626;
constexpr double kRotate3DOrthogonalizer = -0.21132486540518713;  // UNSKEW_2D
constexpr int kGrads3DExponent = 8;
constexpr int kGrads3D = 1 << kGrads3DExponent;
constexpr double kNormalizer3D = 0.07969837668935331;
constexpr float kRSquared3D = 0.6f;

std::int64_t wrapMul(std::int64_t a, std::int64_t b) {
    return static_cast<std::int64_t>(static_cast<std::uint64_t>(a) * static_cast<std::uint64_t>(b));
}
std::int64_t wrapAdd(std::int64_t a, std::int64_t b) {
    return static_cast<std::int64_t>(static_cast<std::uint64_t>(a) + static_cast<std::uint64_t>(b));
}
std::int64_t wrapSub(std::int64_t a, std::int64_t b) {
    return static_cast<std::int64_t>(static_cast<std::uint64_t>(a) - static_cast<std::uint64_t>(b));
}

int fastRound(double x) { return x < 0 ? static_cast<int>(x - 0.5) : static_cast<int>(x + 0.5); }

// The 48 base gradients (x, y, z, pad) from Simplex.java, normalised and repeated to fill 256 entries.
std::array<float, kGrads3D * 4> makeGradients() {
    constexpr float a = 2.22474487139f;
    constexpr float b = 3.0862664687972017f;
    constexpr float c = 1.1721513422464978f;
    constexpr float grad3[] = {
        a,  a,  -1, 0, a,  a,  1,  0, b,  c,  0,  0, c,  b,  0,  0, -a, a,  -1, 0, -a, a,  1,  0,
        -c, b,  0,  0, -b, c,  0,  0, -1, -a, -a, 0, 1,  -a, -a, 0, 0,  -b, -c, 0, 0,  -c, -b, 0,
        -1, -a, a,  0, 1,  -a, a,  0, 0,  -c, b,  0, 0,  -b, c,  0, -a, -a, -1, 0, -a, -a, 1,  0,
        -b, -c, 0,  0, -c, -b, 0,  0, -a, -1, -a, 0, -a, 1,  -a, 0, -c, 0,  -b, 0, -b, 0,  -c, 0,
        -a, -1, a,  0, -a, 1,  a,  0, -b, 0,  c,  0, -c, 0,  b,  0, -1, a,  -a, 0, 1,  a,  -a, 0,
        0,  c,  -b, 0, 0,  b,  -c, 0, -1, a,  a,  0, 1,  a,  a,  0, 0,  b,  c,  0, 0,  c,  b,  0,
        a,  -a, -1, 0, a,  -a, 1,  0, c,  -b, 0,  0, b,  -c, 0,  0, a,  -1, -a, 0, a,  1,  -a, 0,
        b,  0,  -c, 0, c,  0,  -b, 0, a,  -1, a,  0, a,  1,  a,  0, c,  0,  b,  0, b,  0,  c,  0,
    };
    constexpr std::size_t count = sizeof(grad3) / sizeof(grad3[0]);
    std::array<float, kGrads3D * 4> gradients{};
    for (std::size_t i = 0; i < gradients.size(); ++i) {
        gradients[i] = static_cast<float>(grad3[i % count] / kNormalizer3D);
    }
    return gradients;
}

const std::array<float, kGrads3D * 4> kGradients3D = makeGradients();

float grad(std::int64_t seed, std::int64_t xrvp, std::int64_t yrvp, std::int64_t zrvp, float dx, float dy, float dz) {
    std::int64_t hash = (seed ^ xrvp) ^ (yrvp ^ zrvp);
    hash = wrapMul(hash, kHashMultiplier);
    hash ^= hash >> (64 - kGrads3DExponent + 2);  // arithmetic shift, like Java's >> on long
    const int gi = static_cast<int>(static_cast<std::int32_t>(hash)) & ((kGrads3D - 1) << 2);
    return kGradients3D[static_cast<std::size_t>(gi | 0)] * dx + kGradients3D[static_cast<std::size_t>(gi | 1)] * dy +
           kGradients3D[static_cast<std::size_t>(gi | 2)] * dz;
}

}  // namespace

float SimplexNoise::noise3(double x, double y, double z) const {
    // noise3_ImproveXY: rotate so Z points up the main lattice diagonal (not a skew).
    const double xy = x + y;
    const double s2 = xy * kRotate3DOrthogonalizer;
    const double zz = z * kRoot3Over3;
    const double xr = x + s2 + zz;
    const double yr = y + s2 + zz;
    const double zr = xy * -kRoot3Over3 + zz;
    return unrotatedBase(xr, yr, zr);
}

float SimplexNoise::unrotatedBase(double xr, double yr, double zr) const {
    const int xrb = fastRound(xr);
    const int yrb = fastRound(yr);
    const int zrb = fastRound(zr);
    float xri = static_cast<float>(xr - xrb);
    float yri = static_cast<float>(yr - yrb);
    float zri = static_cast<float>(zr - zrb);

    // -1 if positive, 1 if negative.
    int xNSign = static_cast<int>(-1.0f - xri) | 1;
    int yNSign = static_cast<int>(-1.0f - yri) | 1;
    int zNSign = static_cast<int>(-1.0f - zri) | 1;

    float ax0 = static_cast<float>(xNSign) * -xri;
    float ay0 = static_cast<float>(yNSign) * -yri;
    float az0 = static_cast<float>(zNSign) * -zri;

    std::int64_t xrbp = wrapMul(xrb, kPrimeX);
    std::int64_t yrbp = wrapMul(yrb, kPrimeY);
    std::int64_t zrbp = wrapMul(zrb, kPrimeZ);

    float value = 0.0f;
    float a = (kRSquared3D - xri * xri) - (yri * yri + zri * zri);
    std::int64_t seed = seed_;
    for (int l = 0;; ++l) {
        // Closest point on the cube.
        if (a > 0) {
            value += (a * a) * (a * a) * grad(seed, xrbp, yrbp, zrbp, xri, yri, zri);
        }

        // Second-closest point.
        if (ax0 >= ay0 && ax0 >= az0) {
            float b = a + ax0 + ax0;
            if (b > 1) {
                b -= 1;
                value += (b * b) * (b * b) *
                         grad(seed, wrapSub(xrbp, wrapMul(xNSign, kPrimeX)), yrbp, zrbp, xri + static_cast<float>(xNSign), yri, zri);
            }
        } else if (ay0 > ax0 && ay0 >= az0) {
            float b = a + ay0 + ay0;
            if (b > 1) {
                b -= 1;
                value += (b * b) * (b * b) *
                         grad(seed, xrbp, wrapSub(yrbp, wrapMul(yNSign, kPrimeY)), zrbp, xri, yri + static_cast<float>(yNSign), zri);
            }
        } else {
            float b = a + az0 + az0;
            if (b > 1) {
                b -= 1;
                value += (b * b) * (b * b) *
                         grad(seed, xrbp, yrbp, wrapSub(zrbp, wrapMul(zNSign, kPrimeZ)), xri, yri, zri + static_cast<float>(zNSign));
            }
        }

        if (l == 1) break;

        ax0 = 0.5f - ax0;
        ay0 = 0.5f - ay0;
        az0 = 0.5f - az0;

        xri = static_cast<float>(xNSign) * ax0;
        yri = static_cast<float>(yNSign) * ay0;
        zri = static_cast<float>(zNSign) * az0;

        a += (0.75f - ax0) - (ay0 + az0);

        xrbp = wrapAdd(xrbp, static_cast<std::int64_t>(xNSign >> 1) & kPrimeX);
        yrbp = wrapAdd(yrbp, static_cast<std::int64_t>(yNSign >> 1) & kPrimeY);
        zrbp = wrapAdd(zrbp, static_cast<std::int64_t>(zNSign >> 1) & kPrimeZ);

        xNSign = -xNSign;
        yNSign = -yNSign;
        zNSign = -zNSign;

        seed ^= kSeedFlip3D;
    }
    return value;
}
