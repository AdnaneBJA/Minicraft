#include "tile_map.h"

#include "items.h"
#include "random.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

constexpr int kSaplingGrowAge = 100;          // SaplingTile: grows after ~100 random ticks

}  // namespace

const char* tileName(Tile tile) {
    switch (tile) {
        case Tile::Grass: return "Grass";
        case Tile::Sand: return "Sand";
        case Tile::Water: return "Water";
        case Tile::Rock: return "Rock";
        case Tile::Tree: return "Tree";
        case Tile::Dirt: return "Dirt";
        case Tile::Flower: return "Flower";
        case Tile::Farmland: return "Farmland";
        case Tile::Path: return "Path";
        case Tile::Hole: return "Hole";
        case Tile::StairsDown: return "Stairs Down";
        case Tile::StairsUp: return "Stairs Up";
        case Tile::IronOre: return "Iron Ore";
        case Tile::GoldOre: return "Gold Ore";
        case Tile::GemOre: return "Gem Ore";
        case Tile::HardRock: return "Hard Rock";
        case Tile::Lava: return "Lava";
        case Tile::Cactus: return "Cactus";
        case Tile::Sapling: return "Sapling";
        case Tile::CactusSapling: return "Cactus Sapling";
        case Tile::Wheat: return "Wheat";
        case Tile::Cloud: return "Cloud";
        case Tile::InfiniteFall: return "Infinite Fall";
        case Tile::CloudCactus: return "Cloud Cactus";
        case Tile::Torch: return "Torch";
        case Tile::WoodPlanks: return "Wood Planks";
        case Tile::StoneBricks: return "Stone Bricks";
        case Tile::WoodWall: return "Wood Wall";
        case Tile::StoneWall: return "Stone Wall";
        case Tile::WoodDoor: return "Wood Door";
        case Tile::StoneDoor: return "Stone Door";
    }
    return "?";
}

bool isOre(Tile tile) {
    return tile == Tile::IronOre || tile == Tile::GoldOre || tile == Tile::GemOre || tile == Tile::CloudCactus;
}

bool isSolid(Tile tile, std::uint8_t data) {
    switch (tile) {
        case Tile::Rock:
        case Tile::Tree:
        case Tile::IronOre:
        case Tile::GoldOre:
        case Tile::GemOre:
        case Tile::HardRock:
        case Tile::Cactus:
        case Tile::CloudCactus:
        case Tile::InfiniteFall:
        case Tile::WoodWall:
        case Tile::StoneWall: return true;
        case Tile::WoodDoor:
        case Tile::StoneDoor: return data == 0;  // closed
        default: return false;  // water and lava are swimmable
    }
}

bool blocksMobs(Tile tile, std::uint8_t data) {
    return isSolid(tile, data) || tile == Tile::Water || tile == Tile::Lava || tile == Tile::Hole;
}

int maxHealth(Tile tile) {
    switch (tile) {
        case Tile::Tree: return 20;
        case Tile::Rock: return 50;
        case Tile::Cactus: return 10;
        case Tile::HardRock: return 200;
        case Tile::WoodWall: return 20;
        case Tile::StoneWall: return 50;
        case Tile::Flower:
        case Tile::Sapling:
        case Tile::CactusSapling:
        case Tile::Wheat:
        case Tile::Torch: return 1;  // any hit picks or harvests it
        case Tile::IronOre:
        case Tile::GoldOre:
        case Tile::GemOre:
        case Tile::CloudCactus: return 56;  // the highest break point an ore can roll
        default: return 0;
    }
}

void TileMap::restore(std::uint32_t seed, int width, int height, std::vector<Tile> tiles,
                      std::vector<std::uint8_t> data, bool sky) {
    seed_ = seed;
    width_ = width;
    height_ = height;
    tiles_ = std::move(tiles);
    data_ = std::move(data);
    sky_ = sky;
}

Tile TileMap::groundAt(int tx, int ty) const {
    const Tile tile = tileAt(tx, ty);
    if (tile != Tile::Torch) return tile;
    const auto base = static_cast<Tile>(dataAt(tx, ty));
    return base == Tile::Torch ? Tile::Dirt : base;
}

std::optional<TileMap::TileHit> TileMap::hurtTile(int tx, int ty, int damage, Random& rng) {
    if (!inBounds(tx, ty)) return std::nullopt;
    const Tile tile = tileAt(tx, ty);
    const int health = maxHealth(tile);
    if (health == 0) return std::nullopt;
    // OreTile.hurt: an ore rolls its break point (20-56) on every hit.
    const int breakPoint = isOre(tile) ? rng.nextInt(10) * 4 + 20 : health;
    const std::uint8_t before = dataAt(tx, ty);
    const int total = before + damage;
    if (total >= breakPoint) {
        Tile left = Tile::Grass;  // trees, flowers and saplings leave grass, as in Minicraft
        switch (tile) {
            case Tile::Rock:
            case Tile::IronOre:
            case Tile::GoldOre:
            case Tile::GemOre:
            case Tile::HardRock:
            case Tile::Wheat: left = Tile::Dirt; break;
            case Tile::Cactus:
            case Tile::CactusSapling: left = Tile::Sand; break;
            case Tile::CloudCactus: left = Tile::Cloud; break;
            case Tile::WoodWall: left = Tile::WoodPlanks; break;
            case Tile::StoneWall: left = Tile::StoneBricks; break;
            case Tile::Torch: left = groundAt(tx, ty); break;
            default: break;
        }
        setTile(tx, ty, left);
        return TileHit{tile, true, before};
    }
    data_[index(tx, ty)] = static_cast<std::uint8_t>(std::min(total, 255));
    return TileHit{tile, false, before};
}

Vec2 TileMap::findSpawnPoint() const {
    // Search outward in growing squares from the centre for the first grass tile.
    const int cx = width_ / 2;
    const int cy = height_ / 2;
    for (int radius = 0; radius < std::max(width_, height_); ++radius) {
        for (int ty = cy - radius; ty <= cy + radius; ++ty) {
            for (int tx = cx - radius; tx <= cx + radius; ++tx) {
                if (inBounds(tx, ty) && tileAt(tx, ty) == Tile::Grass) {
                    return {static_cast<float>(tx * kTileSize), static_cast<float>(ty * kTileSize)};
                }
            }
        }
    }
    return {static_cast<float>(cx * kTileSize), static_cast<float>(cy * kTileSize)};
}

void TileMap::tickRandomTiles(int tx, int ty, int radius, int count, Random& rng) {
    for (int i = 0; i < count; ++i) {
        const int x = tx + rng.nextInt(2 * radius + 1) - radius;
        const int y = ty + rng.nextInt(2 * radius + 1) - radius;
        if (inBounds(x, y)) tickTile(x, y, rng);
    }
}

void TileMap::tickTile(int tx, int ty, Random& rng) {
    const Tile tile = tileAt(tx, ty);
    const std::uint8_t data = dataAt(tx, ty);
    // A random neighbour (Minicraft picks one axis, then one side of it).
    const auto neighbour = [&]() {
        Point p{tx, ty};
        if (rng.nextInt(2) == 0) p.x += rng.nextInt(2) * 2 - 1;
        else p.y += rng.nextInt(2) * 2 - 1;
        return p;
    };
    switch (tile) {
        case Tile::Grass: {
            // GrassTile.tick: now and then, grass spreads onto a neighbouring dirt tile.
            if (rng.nextInt(40) != 0) return;
            const Point n = neighbour();
            if (inBounds(n.x, n.y) && tileAt(n.x, n.y) == Tile::Dirt) setTile(n.x, n.y, Tile::Grass);
            return;
        }
        case Tile::Water:
        case Tile::Lava: {
            // WaterTile / LavaTile.tick: liquids flow into neighbouring holes.
            const Point n = neighbour();
            if (inBounds(n.x, n.y) && tileAt(n.x, n.y) == Tile::Hole) setTile(n.x, n.y, tile);
            return;
        }
        case Tile::Sapling:
        case Tile::CactusSapling:
            if (data + 1 > kSaplingGrowAge) setTile(tx, ty, tile == Tile::Sapling ? Tile::Tree : Tile::Cactus);
            else setData(tx, ty, static_cast<std::uint8_t>(data + 1));
            return;
        case Tile::Wheat:
            // The original WheatTile.tick: half the time, age by one until ripe.
            if (rng.nextInt(2) == 0 && data < TileMap::kWheatRipeAge) setData(tx, ty, static_cast<std::uint8_t>(data + 1));
            return;
        case Tile::Tree:
        case Tile::Rock:
        case Tile::Cactus:
        case Tile::HardRock:
        case Tile::WoodWall:
        case Tile::StoneWall:
            // Damage heals slowly (TreeTile / HardRockTile / CactusTile.tick).
            if (data > 0) setData(tx, ty, static_cast<std::uint8_t>(data - 1));
            return;
        default: return;
    }
}

int TileMap::flowerVariant(int tx, int ty) const {
    // Hash of (seed, region x, region y), so every flower in an 8x8-tile region is the same kind.
    std::uint32_t h = seed_ * 374761393u + static_cast<std::uint32_t>(tx >> 3) * 668265263u +
                      static_cast<std::uint32_t>(ty >> 3) * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return static_cast<int>(h % static_cast<std::uint32_t>(kFlowerVariants));
}

void TileMap::setTile(int tx, int ty, Tile tile, std::uint8_t data) {
    if (!inBounds(tx, ty)) return;
    tiles_[index(tx, ty)] = tile;
    data_[index(tx, ty)] = data;
}
