#include "world_gen.h"

#include <algorithm>
#include <cmath>
#include <deque>

namespace {

constexpr std::int64_t kMultiplier = 0x5DEECE66DLL;
constexpr std::int64_t kMask = (std::int64_t{1} << 48) - 1;

constexpr double kCoastNoise = 0.5;      // how much the noise roughens the coastline
constexpr double kFalloffScale = 1.2;    // with distance^4: the coast sits ~80% of the way to the edge; the edges are always ocean
constexpr double kOceanBelow = 0.5;      // "land" value under which a tile is ocean
constexpr double kInland = 0.9;          // lakes and mountains only where the land value is above this (away from the coast)
constexpr double kLakeBelow = -0.55;     // lake noise below this floods: a few round lakes
constexpr int kBeachWidth = 2;           // land tiles within this distance of the ocean become sand

// Midpoint-displacement noise (Minicraft's LevelGen(w, h, featureSize)): random values on a grid of featureSize,
// then repeatedly filling in the square and diamond midpoints with smaller and smaller random offsets.
class NoiseMap {
public:
    NoiseMap(JavaRandom& random, int width, int height, int featureSize) : w_(width), h_(height), values_(width * height) {
        for (int y = 0; y < w_; y += featureSize) {
            for (int x = 0; x < w_; x += featureSize) {
                set(x, y, random.nextFloat() * 2 - 1);
            }
        }
        int stepSize = featureSize;
        double scale = 1.0 / w_;
        double scaleMod = 1;
        do {
            const int halfStep = stepSize / 2;
            for (int y = 0; y < w_; y += stepSize) {
                for (int x = 0; x < w_; x += stepSize) {
                    const double a = get(x, y);
                    const double b = get(x + stepSize, y);
                    const double c = get(x, y + stepSize);
                    const double d = get(x + stepSize, y + stepSize);
                    const double e = (a + b + c + d) / 4.0 + (random.nextFloat() * 2 - 1) * stepSize * scale;
                    set(x + halfStep, y + halfStep, e);
                }
            }
            for (int y = 0; y < w_; y += stepSize) {
                for (int x = 0; x < w_; x += stepSize) {
                    const double a = get(x, y);
                    const double b = get(x + stepSize, y);
                    const double c = get(x, y + stepSize);
                    const double d = get(x + halfStep, y + halfStep);
                    const double e = get(x + halfStep, y - halfStep);
                    const double f = get(x - halfStep, y + halfStep);
                    const double horizontal = (a + b + d + e) / 4.0 + (random.nextFloat() * 2 - 1) * stepSize * scale * 0.5;
                    const double vertical = (a + c + d + f) / 4.0 + (random.nextFloat() * 2 - 1) * stepSize * scale * 0.5;
                    set(x + halfStep, y, horizontal);
                    set(x, y + halfStep, vertical);
                }
            }
            stepSize /= 2;
            scale *= (scaleMod + 0.8);
            scaleMod *= 0.3;
        } while (stepSize > 1);
    }

    double at(int index) const { return values_[static_cast<std::size_t>(index)]; }

private:
    // Wraps around the map, like Minicraft's sample() / setSample() (x & (w - 1)).
    double get(int x, int y) const { return values_[static_cast<std::size_t>((x & (w_ - 1)) + (y & (h_ - 1)) * w_)]; }
    void set(int x, int y, double value) { values_[static_cast<std::size_t>((x & (w_ - 1)) + (y & (h_ - 1)) * w_)] = value; }

    int w_;
    int h_;
    std::vector<double> values_;
};

}  // namespace

JavaRandom::JavaRandom(std::int64_t seed) : seed_((seed ^ kMultiplier) & kMask) {}

int JavaRandom::next(int bits) {
    seed_ = static_cast<std::int64_t>(static_cast<std::uint64_t>(seed_) * static_cast<std::uint64_t>(kMultiplier) + 0xBULL) & kMask;
    return static_cast<int>(seed_ >> (48 - bits));
}

int JavaRandom::nextInt(int bound) {
    if ((bound & -bound) == bound) {  // power of two
        return static_cast<int>((static_cast<std::int64_t>(bound) * next(31)) >> 31);
    }
    int bits = 0;
    int value = 0;
    do {
        bits = next(31);
        value = bits % bound;
    } while (bits - value + (bound - 1) < 0);
    return value;
}

float JavaRandom::nextFloat() { return static_cast<float>(next(24)) / static_cast<float>(1 << 24); }

std::vector<Tile> WorldGenerator::generate(std::uint32_t seed, int width, int height) {
    JavaRandom random(seed);
    while (true) {
        std::vector<Tile> map = createTopMap(random, width, height);
        // createAndValidateTopMap: reject worlds without enough of each resource and terrain.
        const auto count = [&](Tile tile) { return std::count(map.begin(), map.end(), tile); };
        if (count(Tile::Rock) < 100 || count(Tile::Sand) < 100 || count(Tile::Grass) < 100 || count(Tile::Tree) < 100) {
            continue;
        }
        return map;
    }
}

std::vector<Tile> WorldGenerator::createTopMap(JavaRandom& random, int w, int h) {
    const NoiseMap mnoise1(random, w, h, 16);
    const NoiseMap mnoise2(random, w, h, 16);
    const NoiseMap mnoise3(random, w, h, 16);
    const NoiseMap noise1(random, w, h, 32);
    const NoiseMap noise2(random, w, h, 32);
    const NoiseMap lakeNoise(random, w, h, 16);

    std::vector<Tile> map(static_cast<std::size_t>(w * h), Tile::Grass);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int i = x + y * w;
            // Mountain noise, exactly as Minicraft computes it.
            double mval = std::abs(mnoise1.at(i) - mnoise2.at(i));
            mval = std::abs(mval - mnoise3.at(i)) * 3 - 2;

            // Island falloff. Minicraft raises the distance to the nearest edge to the 8th power so it only matters
            // near the edges; here the distance is measured from the centre (round island instead of a square) and
            // raised to the 4th power for a wider coastal band that the noise can make ragged.
            const double xd = x / (w - 1.0) * 2 - 1;
            const double yd = y / (h - 1.0) * 2 - 1;
            const double dist = std::sqrt(xd * xd + yd * yd);  // 0 in the centre, 1 at the middle of an edge
            const double falloff = dist * dist * dist * dist * kFalloffScale;
            // How "inland" the tile is. Minicraft subtracts the falloff from |noise1 - noise2|, which also carves long
            // water channels through the island; using one smooth noise instead keeps the water at the edges with a
            // ragged but clear coastline.
            const double land = 1.0 - falloff + noise1.at(i) * kCoastNoise;
            // Minicraft's mountain gate (val = |noise1 - noise2| * 3 - 2 + 1 inland; val > 0.5 and mval < -1.5),
            // loosened (0.2 / -1.2) because this island leaves out Minicraft's rock-rich water channels.
            const double val = std::abs(noise1.at(i) - noise2.at(i)) * 3 - 1;

            Tile tile = Tile::Grass;
            if (land < kOceanBelow) {
                tile = Tile::Water;
            } else if (land > kInland && lakeNoise.at(i) < kLakeBelow) {
                tile = Tile::Water;
            } else if (land > kInland && val > 0.2 && mval < -1.2) {
                tile = Tile::Rock;
            }
            map[static_cast<std::size_t>(i)] = tile;
        }
    }

    // Sand patches ("deserts"): clusters of blobs painted over grass.
    for (int i = 0; i < w * h / 2800; ++i) {
        const int xs = random.nextInt(w);
        const int ys = random.nextInt(h);
        for (int k = 0; k < 10; ++k) {
            const int x = xs + random.nextInt(21) - 10;
            const int y = ys + random.nextInt(21) - 10;
            for (int j = 0; j < 100; ++j) {
                const int xo = x + random.nextInt(5) - random.nextInt(5);
                const int yo = y + random.nextInt(5) - random.nextInt(5);
                for (int yy = yo - 1; yy <= yo + 1; ++yy) {
                    for (int xx = xo - 1; xx <= xo + 1; ++xx) {
                        if (xx >= 0 && yy >= 0 && xx < w && yy < h && map[static_cast<std::size_t>(xx + yy * w)] == Tile::Grass) {
                            map[static_cast<std::size_t>(xx + yy * w)] = Tile::Sand;
                        }
                    }
                }
            }
        }
    }

    // Forests: clumps of trees scattered over grass.
    for (int i = 0; i < w * h / 400; ++i) {
        const int x = random.nextInt(w);
        const int y = random.nextInt(h);
        for (int j = 0; j < 200; ++j) {
            const int xx = x + random.nextInt(15) - random.nextInt(15);
            const int yy = y + random.nextInt(15) - random.nextInt(15);
            if (xx >= 0 && yy >= 0 && xx < w && yy < h && map[static_cast<std::size_t>(xx + yy * w)] == Tile::Grass) {
                map[static_cast<std::size_t>(xx + yy * w)] = Tile::Tree;
            }
        }
    }

    addBeaches(map, w, h);
    return map;
}

void WorldGenerator::addBeaches(std::vector<Tile>& map, int w, int h) {
    // The ocean is the water connected to the map edge (flood fill); inland lakes keep their grass shores.
    // Then every land tile within kBeachWidth tiles of the ocean becomes sand (breadth-first distance).
    std::vector<int> distance(map.size(), -1);
    std::deque<int> queue;
    const auto visitOcean = [&](int x, int y) {
        const int i = x + y * w;
        if (distance[static_cast<std::size_t>(i)] == -1 && map[static_cast<std::size_t>(i)] == Tile::Water) {
            distance[static_cast<std::size_t>(i)] = 0;
            queue.push_back(i);
        }
    };
    for (int x = 0; x < w; ++x) {
        visitOcean(x, 0);
        visitOcean(x, h - 1);
    }
    for (int y = 0; y < h; ++y) {
        visitOcean(0, y);
        visitOcean(w - 1, y);
    }
    constexpr int dx[] = {1, -1, 0, 0, 1, 1, -1, -1};
    constexpr int dy[] = {0, 0, 1, -1, 1, -1, 1, -1};
    while (!queue.empty()) {
        const int i = queue.front();
        queue.pop_front();
        const int x = i % w;
        const int y = i / w;
        const int d = distance[static_cast<std::size_t>(i)];
        for (int n = 0; n < 8; ++n) {
            const int nx = x + dx[n];
            const int ny = y + dy[n];
            if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
            const int ni = nx + ny * w;
            if (distance[static_cast<std::size_t>(ni)] != -1) continue;
            const Tile tile = map[static_cast<std::size_t>(ni)];
            if (tile == Tile::Water && d == 0) {
                distance[static_cast<std::size_t>(ni)] = 0;  // still the same body of ocean
                queue.push_back(ni);
            } else if (tile != Tile::Water && d < kBeachWidth) {
                distance[static_cast<std::size_t>(ni)] = d + 1;
                map[static_cast<std::size_t>(ni)] = Tile::Sand;
                queue.push_back(ni);
            }
        }
    }
}
