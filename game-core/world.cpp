#include "world.h"

#include "world_gen.h"

#include <utility>

std::string Level::name() const {
    if (depth_ > 0) return "Sky";
    if (depth_ == 0) return "Surface";
    return "Cave B" + std::to_string(-depth_);
}

void Level::clearEntities() {
    mobs.clear();
    drops.clear();
    projectiles.clear();
}

World::World() : levels_{Level(1), Level(0), Level(-1), Level(-2), Level(-3)} {}

void World::restoreLevel(int index, std::uint32_t seed, std::vector<Tile> tiles, std::vector<std::uint8_t> data) {
    Level& target = level(index);
    target.map.restore(seed, kSize, kSize, std::move(tiles), std::move(data), target.isSky());
    target.furniture.clear();
    target.clearEntities();
}

void World::generate(std::uint32_t seed, Random& rng) {
    seed_ = seed;
    const std::size_t area = static_cast<std::size_t>(kSize) * kSize;
    for (int i = 0; i < kLevelCount; ++i) {
        const int depth = level(i).depth();
        std::vector<Tile> tiles = depth > 0    ? WorldGenerator::generateSky(seed, kSize, kSize)
                                  : depth == 0 ? WorldGenerator::generate(seed, kSize, kSize)
                                               : WorldGenerator::generateUnderground(seed, kSize, kSize, -depth);
        restoreLevel(i, seed, std::move(tiles), std::vector<std::uint8_t>(area, 0));
    }
    linkStairs();
    airWizardBeaten = false;
    currentIndex_ = kSurfaceIndex;
    spawnBoss(rng);
}

void World::linkStairs() {
    for (int i = 1; i < kLevelCount; ++i) {
        TileMap& above = level(i - 1).map;
        TileMap& below = level(i).map;
        const bool belowIsSurface = i == kSurfaceIndex;
        for (int y = 1; y < kSize - 1; ++y) {
            for (int x = 1; x < kSize - 1; ++x) {
                if (above.tileAt(x, y) == Tile::StairsDown && below.tileAt(x, y) != Tile::StairsUp) {
                    // The surface's stairs up to the sky sit in a ring of hard rock (only a gem pickaxe gets
                    // through); in the caves the stairs up get a small dirt room so the player can step off.
                    for (int yy = y - 1; yy <= y + 1; ++yy) {
                        for (int xx = x - 1; xx <= x + 1; ++xx) {
                            const Tile t = below.tileAt(xx, yy);
                            if (t == Tile::StairsDown || t == Tile::StairsUp) continue;
                            below.setTile(xx, yy, belowIsSurface ? Tile::HardRock : Tile::Dirt);
                        }
                    }
                    below.setTile(x, y, Tile::StairsUp);
                } else if (below.tileAt(x, y) == Tile::StairsUp && above.tileAt(x, y) != Tile::StairsDown) {
                    above.setTile(x, y, Tile::StairsDown);
                }
                // Stairs down are cut into solid rock; climbing up onto them from below would leave the player
                // walled in, so they get a dirt room too (not in the sky, whose stairs already sit on clouds).
                if (i - 1 != kSkyIndex && above.tileAt(x, y) == Tile::StairsDown) {
                    for (int yy = y - 1; yy <= y + 1; ++yy) {
                        for (int xx = x - 1; xx <= x + 1; ++xx) {
                            if (above.tileAt(xx, yy) == Tile::Rock) above.setTile(xx, yy, Tile::Dirt);
                        }
                    }
                }
            }
        }
    }
}

void World::spawnBoss(Random& rng) {
    Level& sky = level(kSkyIndex);
    if (airWizardBeaten || sky.mobs.boss()) return;
    // The original Level puts it in the middle of the sky; search outward for a cloud to stand on.
    const float middle = static_cast<float>(kSize * TileMap::kTileSize / 2);
    for (int radius = 0; radius < kSize / 2; radius += 2) {
        if (sky.mobs.spawnNear(MobKind::AirWizard, 1, sky.map, {}, middle, middle, radius, radius, rng)) return;
    }
}

void World::setCurrent(int index) {
    current().projectiles.clear();
    currentIndex_ = index;
}

void World::clearEntities() {
    for (Level& level : levels_) level.clearEntities();
}
