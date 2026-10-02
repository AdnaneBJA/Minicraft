// World generation: every level is there, the stairs line up between levels, and the boss waits in the sky.
#include "random.h"
#include "world.h"

#include <gtest/gtest.h>

#include <algorithm>

namespace {

int count(const TileMap& map, Tile tile) {
    return static_cast<int>(std::count(map.tiles().begin(), map.tiles().end(), tile));
}

}  // namespace

class WorldGeneration : public ::testing::TestWithParam<std::uint32_t> {};

TEST_P(WorldGeneration, StairsDownAlwaysLeadToStairsUpBelow) {
    Random rng(GetParam());
    World world;
    world.generate(GetParam(), rng);
    for (int i = 0; i + 1 < World::kLevelCount; ++i) {
        const TileMap& above = world.level(i).map;
        const TileMap& below = world.level(i + 1).map;
        int pairs = 0;
        for (int y = 0; y < World::kSize; ++y) {
            for (int x = 0; x < World::kSize; ++x) {
                const bool down = above.tileAt(x, y) == Tile::StairsDown;
                const bool up = below.tileAt(x, y) == Tile::StairsUp;
                ASSERT_EQ(down, up) << "level " << i << " at " << x << "," << y;
                pairs += down;
            }
        }
        EXPECT_GE(pairs, 2) << "level " << i;
    }
}

TEST_P(WorldGeneration, EachCaveHasItsOre) {
    Random rng(GetParam());
    World world;
    world.generate(GetParam(), rng);
    EXPECT_GT(count(world.level(2).map, Tile::IronOre), 20);
    EXPECT_GT(count(world.level(3).map, Tile::GoldOre), 20);
    EXPECT_GT(count(world.level(4).map, Tile::GemOre), 20);
    EXPECT_GT(count(world.level(4).map, Tile::Lava), 0);
}

TEST_P(WorldGeneration, TheAirWizardWaitsInTheSky) {
    Random rng(GetParam());
    World world;
    world.generate(GetParam(), rng);
    EXPECT_NE(world.level(World::kSkyIndex).mobs.boss(), nullptr);
}

TEST_P(WorldGeneration, SameSeedSameWorld) {
    Random rngA(GetParam());
    Random rngB(GetParam());
    World a;
    World b;
    a.generate(GetParam(), rngA);
    b.generate(GetParam(), rngB);
    for (int i = 0; i < World::kLevelCount; ++i) EXPECT_EQ(a.level(i).map.tiles(), b.level(i).map.tiles());
}

INSTANTIATE_TEST_SUITE_P(Seeds, WorldGeneration, ::testing::Values(1337u, 42u, 7u));
