#include "tile_map.h"

#include "camera.h"
#include "items.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

constexpr int kWaterFrames = 8;
constexpr float kWaterFramesPerSecond = 3.0f;
constexpr float kLavaFramesPerSecond = 6.0f;  // lava.png.json: frametime 10 ticks
constexpr float kHalf = 8.0f;                 // tiles are drawn as four 8x8 quadrants
constexpr int kSaplingGrowAge = 100;          // SaplingTile: grows after ~100 random ticks
constexpr int kWheatRipeAge = 50;             // the original WheatTile: ripe at age 50

// Positions (in pixels) of each texture in assets/sprites/tiles.png.
struct AtlasPos {
    float x;
    float y;
};
constexpr AtlasPos kGrass{0, 0}, kSand{16, 0}, kRock{32, 0}, kDirt{48, 0};
constexpr AtlasPos kOak{64, 0};         // tree (transparent background), exactly one per tree tile
constexpr AtlasPos kFlower0{0, 56};     // the flower sprites, one per variant (transparent background)
constexpr AtlasPos kFarmland{0, 72}, kPath{16, 72}, kHole{32, 72};
constexpr AtlasPos kHoleBorder{0, 88};
constexpr AtlasPos kRockCorner{80, 0};  // 2x2 quadrants for rock's inner corners
constexpr AtlasPos kWaterFrame0{0, 16};
// Border sheets: 3x3 grid of 8x8 pieces. Row/column 0 = top/left edge, 1 = no edge, 2 = bottom/right edge.
constexpr AtlasPos kGrassBorder{0, 32}, kSandBorder{24, 32}, kWaterBorder{48, 32}, kRockBorder{72, 32};
// Underground, sky and building tiles (rows y = 112 and below).
constexpr AtlasPos kStairsDown{0, 112}, kStairsUp{16, 112}, kIronOre{32, 112}, kGoldOre{48, 112}, kGemOre{64, 112};
constexpr AtlasPos kCactus{80, 112}, kSapling{96, 112}, kTorch{112, 112};
constexpr AtlasPos kHardRock{0, 128}, kCloud{16, 128}, kCloudOre{32, 128}, kWoodFloor{48, 128}, kStoneFloor{64, 128};
constexpr AtlasPos kWoodDoor{80, 128}, kWoodDoorOpen{96, 128}, kStoneDoor{112, 128}, kStoneDoorOpen{0, 144};
constexpr AtlasPos kWheat0{16, 144};
// The open sky under the clouds (InfiniteFallTile draws nothing; Minicraft's sky level shows a dark background).
constexpr SDL_Color kSkyColor{16, 18, 46, 255};

void drawSky(SDL_Renderer* renderer, float x, float y) {
    const SDL_FRect area{x, y, static_cast<float>(TileMap::kTileSize), static_cast<float>(TileMap::kTileSize)};
    SDL_SetRenderDrawColor(renderer, kSkyColor.r, kSkyColor.g, kSkyColor.b, kSkyColor.a);
    SDL_RenderFillRect(renderer, &area);
}
constexpr AtlasPos kLavaBorder{0, 160}, kHardRockBorder{24, 160}, kCloudBorder{48, 160}, kWoodWallBorder{72, 160};
constexpr AtlasPos kStoneWallBorder{96, 160};
constexpr AtlasPos kHardRockCorner{0, 184}, kCloudCorner{16, 184}, kWoodWall{32, 184}, kStoneWall{48, 184};
constexpr AtlasPos kLavaFrame0{0, 200};

// A tile type drawn with Minicraft's connected-texture scheme.
struct ConnectedTexture {
    AtlasPos full;
    AtlasPos border;
    const AtlasPos* corner;  // optional inner-corner sheet
};

bool isGrassy(Tile tile) {
    return tile == Tile::Grass || tile == Tile::Tree || tile == Tile::Flower || tile == Tile::Sapling;
}
bool isSandy(Tile tile) { return tile == Tile::Sand || tile == Tile::Cactus || tile == Tile::CactusSapling; }

// Which neighbouring tiles blend seamlessly with a tile of the given kind (no border between them).
bool connects(Tile kind, Tile other) {
    switch (kind) {
        case Tile::Grass: return isGrassy(other);
        case Tile::Sand: return isSandy(other);
        case Tile::Water: return other == Tile::Water;
        case Tile::Lava: return other == Tile::Lava;
        case Tile::Rock: return other == Tile::Rock;
        case Tile::HardRock: return other == Tile::HardRock;
        case Tile::Hole: return other == Tile::Hole;
        case Tile::Cloud: return other != Tile::InfiniteFall;  // CloudTile's connection checker
        case Tile::WoodWall:
        case Tile::StoneWall: return other == kind;
        default: return other == kind;
    }
}

void drawPiece(SDL_Renderer* renderer, SDL_Texture* atlas, float srcX, float srcY, float size, float x, float y,
               SDL_FlipMode flip = SDL_FLIP_NONE) {
    const SDL_FRect source{srcX, srcY, size, size};
    const SDL_FRect destination{x, y, size, size};
    SDL_RenderTextureRotated(renderer, atlas, &source, &destination, 0.0, nullptr, flip);
}

void drawSprite(SDL_Renderer* renderer, SDL_Texture* atlas, AtlasPos pos, float x, float y) {
    drawPiece(renderer, atlas, pos.x, pos.y, static_cast<float>(TileMap::kTileSize), x, y);
}

// Port of the Minicraft+ border rendering (SpriteAnimation.render). Each quadrant looks at the two neighbours it
// touches and the diagonal between them, then picks an edge piece, the plain centre, or an inner corner.
void drawConnected(SDL_Renderer* renderer, SDL_Texture* atlas, const TileMap& map, int tx, int ty, float x, float y,
                   Tile kind, const ConnectedTexture& texture) {
    const auto connected = [&](int nx, int ny) {
        return !map.inBounds(nx, ny) || connects(kind, map.groundAt(nx, ny));
    };
    for (const int v : {-1, 1}) {      // -1 = top half, 1 = bottom half
        for (const int h : {-1, 1}) {  // -1 = left half, 1 = right half
            const bool vertical = connected(tx, ty + v);
            const bool horizontal = connected(tx + h, ty);
            const float qx = x + (h < 0 ? 0.0f : kHalf);
            const float qy = y + (v < 0 ? 0.0f : kHalf);
            if (vertical && horizontal) {
                if (!connected(tx + h, ty + v) && texture.corner) {
                    // Inner corner: matching quadrant of the corner sheet, flipped both ways (as Minicraft+ does).
                    drawPiece(renderer, atlas, texture.corner->x + (h < 0 ? 0.0f : kHalf),
                              texture.corner->y + (v < 0 ? 0.0f : kHalf), kHalf, qx, qy,
                              static_cast<SDL_FlipMode>(SDL_FLIP_HORIZONTAL | SDL_FLIP_VERTICAL));
                } else {
                    // Interior: use the full tile texture, which has the speckles (and water's animation), rather
                    // than the plain centre of the border sheet. Minicraft+ calls this "singleton with connective".
                    // It takes the opposite quadrant of the full texture, as Minicraft+ does.
                    drawPiece(renderer, atlas, texture.full.x + (h < 0 ? kHalf : 0.0f),
                              texture.full.y + (v < 0 ? kHalf : 0.0f), kHalf, qx, qy);
                }
            } else {
                const float row = vertical ? 1.0f : (v < 0 ? 0.0f : 2.0f);
                const float column = horizontal ? 1.0f : (h < 0 ? 0.0f : 2.0f);
                drawPiece(renderer, atlas, texture.border.x + column * kHalf, texture.border.y + row * kHalf, kHalf,
                          qx, qy);
            }
        }
    }
}

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

std::optional<TileMap::TileHit> TileMap::hurtTile(int tx, int ty, int damage) {
    if (!inBounds(tx, ty)) return std::nullopt;
    const Tile tile = tileAt(tx, ty);
    const int health = maxHealth(tile);
    if (health == 0) return std::nullopt;
    // OreTile.hurt: an ore rolls its break point (20-56) on every hit.
    const int breakPoint = isOre(tile) ? static_cast<int>(SDL_rand(10)) * 4 + 20 : health;
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

SDL_FPoint TileMap::findSpawnPoint() const {
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

void TileMap::tickRandomTiles(int tx, int ty, int radius, int count) {
    for (int i = 0; i < count; ++i) {
        const int x = tx + static_cast<int>(SDL_rand(2 * radius + 1)) - radius;
        const int y = ty + static_cast<int>(SDL_rand(2 * radius + 1)) - radius;
        if (inBounds(x, y)) tickTile(x, y);
    }
}

void TileMap::tickTile(int tx, int ty) {
    const Tile tile = tileAt(tx, ty);
    const std::uint8_t data = dataAt(tx, ty);
    // A random neighbour (Minicraft picks one axis, then one side of it).
    const auto neighbour = [&]() {
        SDL_Point p{tx, ty};
        if (SDL_rand(2) == 0) p.x += static_cast<int>(SDL_rand(2)) * 2 - 1;
        else p.y += static_cast<int>(SDL_rand(2)) * 2 - 1;
        return p;
    };
    switch (tile) {
        case Tile::Grass: {
            // GrassTile.tick: now and then, grass spreads onto a neighbouring dirt tile.
            if (SDL_rand(40) != 0) return;
            const SDL_Point n = neighbour();
            if (inBounds(n.x, n.y) && tileAt(n.x, n.y) == Tile::Dirt) setTile(n.x, n.y, Tile::Grass);
            return;
        }
        case Tile::Water:
        case Tile::Lava: {
            // WaterTile / LavaTile.tick: liquids flow into neighbouring holes.
            const SDL_Point n = neighbour();
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
            if (SDL_rand(2) == 0 && data < kWheatRipeAge) setData(tx, ty, static_cast<std::uint8_t>(data + 1));
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

void TileMap::draw(SDL_Renderer* renderer, const Camera& camera, float timeSeconds) const {
    const int waterFrame = static_cast<int>(timeSeconds * kWaterFramesPerSecond) % kWaterFrames;
    const int lavaFrame = static_cast<int>(timeSeconds * kLavaFramesPerSecond) % kWaterFrames;

    // Only draw the tiles the camera can see.
    const int firstX = std::max(0, static_cast<int>(camera.x()) / kTileSize);
    const int firstY = std::max(0, static_cast<int>(camera.y()) / kTileSize);
    const int lastX = std::min(width_ - 1, static_cast<int>(camera.x() + camera.width()) / kTileSize);
    const int lastY = std::min(height_ - 1, static_cast<int>(camera.y() + camera.height()) / kTileSize);

    for (int ty = firstY; ty <= lastY; ++ty) {
        for (int tx = firstX; tx <= lastX; ++tx) {
            const float x = static_cast<float>(tx * kTileSize) - camera.x();
            const float y = static_cast<float>(ty * kTileSize) - camera.y();
            const Tile tile = tileAt(tx, ty);
            if (tile == Tile::Torch) {
                // TorchTile.render: the tile it was placed on, then the torch.
                drawTile(renderer, tx, ty, groundAt(tx, ty), x, y, lavaFrame, waterFrame);
                drawSprite(renderer, atlas_, kTorch, x, y);
            } else {
                drawTile(renderer, tx, ty, tile, x, y, lavaFrame, waterFrame);
            }
        }
    }
}

void TileMap::drawTile(SDL_Renderer* renderer, int tx, int ty, Tile tile, float x, float y, int lavaFrame,
                       int waterFrame) const {
    SDL_Texture* atlas = atlas_;
    constexpr float size = static_cast<float>(kTileSize);
    static const ConnectedTexture grass{kGrass, kGrassBorder, nullptr};
    static const ConnectedTexture sand{kSand, kSandBorder, nullptr};
    static const ConnectedTexture rock{kRock, kRockBorder, &kRockCorner};
    static const ConnectedTexture hardRock{kHardRock, kHardRockBorder, &kHardRockCorner};
    static const ConnectedTexture cloud{kCloud, kCloudBorder, &kCloudCorner};
    static const ConnectedTexture hole{kHole, kHoleBorder, nullptr};
    static const ConnectedTexture woodWall{kWoodWall, kWoodWallBorder, nullptr};
    static const ConnectedTexture stoneWall{kStoneWall, kStoneWallBorder, nullptr};
    const ConnectedTexture water{{kWaterFrame0.x + static_cast<float>(waterFrame) * size, kWaterFrame0.y},
                                 kWaterBorder, nullptr};
    const ConnectedTexture lava{{kLavaFrame0.x + static_cast<float>(lavaFrame) * size, kLavaFrame0.y}, kLavaBorder,
                                nullptr};

    // Everything sits on dirt (or the open sky below the clouds), which shows through the transparent rims of the
    // border pieces.
    if (sky_) {
        drawSky(renderer, x, y);
    } else {
        drawSprite(renderer, atlas, kDirt, x, y);
    }
    const auto on = [&](Tile base, const ConnectedTexture& texture, AtlasPos sprite) {
        drawConnected(renderer, atlas, *this, tx, ty, x, y, base, texture);
        drawSprite(renderer, atlas, sprite, x, y);
    };
    switch (tile) {
        case Tile::Grass: drawConnected(renderer, atlas, *this, tx, ty, x, y, tile, grass); break;
        case Tile::Sand: drawConnected(renderer, atlas, *this, tx, ty, x, y, tile, sand); break;
        case Tile::Rock: drawConnected(renderer, atlas, *this, tx, ty, x, y, tile, rock); break;
        case Tile::HardRock: drawConnected(renderer, atlas, *this, tx, ty, x, y, tile, hardRock); break;
        case Tile::Water: drawConnected(renderer, atlas, *this, tx, ty, x, y, tile, water); break;
        case Tile::Lava: drawConnected(renderer, atlas, *this, tx, ty, x, y, tile, lava); break;
        case Tile::Cloud: drawConnected(renderer, atlas, *this, tx, ty, x, y, tile, cloud); break;
        case Tile::Hole: drawConnected(renderer, atlas, *this, tx, ty, x, y, tile, hole); break;
        case Tile::WoodWall: drawConnected(renderer, atlas, *this, tx, ty, x, y, tile, woodWall); break;
        case Tile::StoneWall: drawConnected(renderer, atlas, *this, tx, ty, x, y, tile, stoneWall); break;
        case Tile::Tree: on(Tile::Grass, grass, kOak); break;
        case Tile::Sapling: on(Tile::Grass, grass, kSapling); break;
        case Tile::Cactus: on(Tile::Sand, sand, kCactus); break;
        case Tile::CactusSapling: on(Tile::Sand, sand, kSapling); break;
        case Tile::CloudCactus: on(Tile::Cloud, cloud, kCloudOre); break;
        case Tile::Flower:
            drawConnected(renderer, atlas, *this, tx, ty, x, y, Tile::Grass, grass);
            drawPiece(renderer, atlas, kFlower0.x + static_cast<float>(flowerVariant(tx, ty)) * size, kFlower0.y,
                      size, x, y);
            break;
        case Tile::StairsDown:
        case Tile::StairsUp:
            if (sky_) drawConnected(renderer, atlas, *this, tx, ty, x, y, Tile::Cloud, cloud);
            drawSprite(renderer, atlas, tile == Tile::StairsDown ? kStairsDown : kStairsUp, x, y);
            break;
        case Tile::IronOre: drawSprite(renderer, atlas, kIronOre, x, y); break;
        case Tile::GoldOre: drawSprite(renderer, atlas, kGoldOre, x, y); break;
        case Tile::GemOre: drawSprite(renderer, atlas, kGemOre, x, y); break;
        case Tile::Wheat: {
            // WheatTile.render: farmland, then one of 6 growth stages.
            drawSprite(renderer, atlas, kFarmland, x, y);
            const int stage = std::min(5, dataAt(tx, ty) * 5 / kWheatRipeAge);
            drawSprite(renderer, atlas, {kWheat0.x + static_cast<float>(stage) * size, kWheat0.y}, x, y);
            break;
        }
        case Tile::Dirt:
        case Tile::Torch: break;  // just the dirt base (torches are drawn by draw())
        case Tile::InfiniteFall: drawSky(renderer, x, y); break;
        case Tile::Farmland: drawSprite(renderer, atlas, kFarmland, x, y); break;
        case Tile::Path: drawSprite(renderer, atlas, kPath, x, y); break;
        case Tile::WoodPlanks: drawSprite(renderer, atlas, kWoodFloor, x, y); break;
        case Tile::StoneBricks: drawSprite(renderer, atlas, kStoneFloor, x, y); break;
        case Tile::WoodDoor:
            drawSprite(renderer, atlas, kWoodFloor, x, y);
            drawSprite(renderer, atlas, dataAt(tx, ty) ? kWoodDoorOpen : kWoodDoor, x, y);
            break;
        case Tile::StoneDoor:
            drawSprite(renderer, atlas, kStoneFloor, x, y);
            drawSprite(renderer, atlas, dataAt(tx, ty) ? kStoneDoorOpen : kStoneDoor, x, y);
            break;
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
