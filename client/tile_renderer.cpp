#include "tile_renderer.h"

#include "camera.h"
#include "tile_map.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr int kWaterFrames = 8;
constexpr float kWaterFramesPerSecond = 3.0f;
constexpr float kLavaFramesPerSecond = 6.0f;  // lava.png.json: frametime 10 ticks
constexpr float kHalf = 8.0f;                 // tiles are drawn as four 8x8 quadrants
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

bool TileRenderer::load(SDL_Renderer* renderer, const std::string& atlasPath) {
    atlas_ = loadTexture(renderer, atlasPath);
    return atlas_ != nullptr;
}

void TileRenderer::draw(SDL_Renderer* renderer, const Camera& camera, const TileMap& map, float timeSeconds) const {
    const int waterFrame = static_cast<int>(timeSeconds * kWaterFramesPerSecond) % kWaterFrames;
    const int lavaFrame = static_cast<int>(timeSeconds * kLavaFramesPerSecond) % kWaterFrames;

    // Only draw the tiles the camera can see.
    const int firstX = std::max(0, static_cast<int>(camera.x()) / TileMap::kTileSize);
    const int firstY = std::max(0, static_cast<int>(camera.y()) / TileMap::kTileSize);
    const int lastX = std::min(map.width() - 1, static_cast<int>(camera.x() + camera.width()) / TileMap::kTileSize);
    const int lastY = std::min(map.height() - 1, static_cast<int>(camera.y() + camera.height()) / TileMap::kTileSize);

    for (int ty = firstY; ty <= lastY; ++ty) {
        for (int tx = firstX; tx <= lastX; ++tx) {
            const float x = static_cast<float>(tx * TileMap::kTileSize) - camera.x();
            const float y = static_cast<float>(ty * TileMap::kTileSize) - camera.y();
            const Tile tile = map.tileAt(tx, ty);
            if (tile == Tile::Torch) {
                // TorchTile.render: the tile it was placed on, then the torch.
                drawTile(renderer, map, tx, ty, map.groundAt(tx, ty), x, y, lavaFrame, waterFrame);
                drawSprite(renderer, atlas_.get(), kTorch, x, y);
            } else {
                drawTile(renderer, map, tx, ty, tile, x, y, lavaFrame, waterFrame);
            }
        }
    }
}

void TileRenderer::drawTile(SDL_Renderer* renderer, const TileMap& map, int tx, int ty, Tile tile, float x, float y,
                            int lavaFrame, int waterFrame) const {
    SDL_Texture* atlas = atlas_.get();
    constexpr float size = static_cast<float>(TileMap::kTileSize);
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
    if (map.isSky()) {
        drawSky(renderer, x, y);
    } else {
        drawSprite(renderer, atlas, kDirt, x, y);
    }
    const auto on = [&](Tile base, const ConnectedTexture& texture, AtlasPos sprite) {
        drawConnected(renderer, atlas, map, tx, ty, x, y, base, texture);
        drawSprite(renderer, atlas, sprite, x, y);
    };
    switch (tile) {
        case Tile::Grass: drawConnected(renderer, atlas, map, tx, ty, x, y, tile, grass); break;
        case Tile::Sand: drawConnected(renderer, atlas, map, tx, ty, x, y, tile, sand); break;
        case Tile::Rock: drawConnected(renderer, atlas, map, tx, ty, x, y, tile, rock); break;
        case Tile::HardRock: drawConnected(renderer, atlas, map, tx, ty, x, y, tile, hardRock); break;
        case Tile::Water: drawConnected(renderer, atlas, map, tx, ty, x, y, tile, water); break;
        case Tile::Lava: drawConnected(renderer, atlas, map, tx, ty, x, y, tile, lava); break;
        case Tile::Cloud: drawConnected(renderer, atlas, map, tx, ty, x, y, tile, cloud); break;
        case Tile::Hole: drawConnected(renderer, atlas, map, tx, ty, x, y, tile, hole); break;
        case Tile::WoodWall: drawConnected(renderer, atlas, map, tx, ty, x, y, tile, woodWall); break;
        case Tile::StoneWall: drawConnected(renderer, atlas, map, tx, ty, x, y, tile, stoneWall); break;
        case Tile::Tree: on(Tile::Grass, grass, kOak); break;
        case Tile::Sapling: on(Tile::Grass, grass, kSapling); break;
        case Tile::Cactus: on(Tile::Sand, sand, kCactus); break;
        case Tile::CactusSapling: on(Tile::Sand, sand, kSapling); break;
        case Tile::CloudCactus: on(Tile::Cloud, cloud, kCloudOre); break;
        case Tile::Flower:
            drawConnected(renderer, atlas, map, tx, ty, x, y, Tile::Grass, grass);
            drawPiece(renderer, atlas, kFlower0.x + static_cast<float>(map.flowerVariant(tx, ty)) * size, kFlower0.y,
                      size, x, y);
            break;
        case Tile::StairsDown:
        case Tile::StairsUp:
            if (map.isSky()) drawConnected(renderer, atlas, map, tx, ty, x, y, Tile::Cloud, cloud);
            drawSprite(renderer, atlas, tile == Tile::StairsDown ? kStairsDown : kStairsUp, x, y);
            break;
        case Tile::IronOre: drawSprite(renderer, atlas, kIronOre, x, y); break;
        case Tile::GoldOre: drawSprite(renderer, atlas, kGoldOre, x, y); break;
        case Tile::GemOre: drawSprite(renderer, atlas, kGemOre, x, y); break;
        case Tile::Wheat: {
            // WheatTile.render: farmland, then one of 6 growth stages.
            drawSprite(renderer, atlas, kFarmland, x, y);
            const int stage = std::min(5, map.dataAt(tx, ty) * 5 / TileMap::kWheatRipeAge);
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
            drawSprite(renderer, atlas, map.dataAt(tx, ty) ? kWoodDoorOpen : kWoodDoor, x, y);
            break;
        case Tile::StoneDoor:
            drawSprite(renderer, atlas, kStoneFloor, x, y);
            drawSprite(renderer, atlas, map.dataAt(tx, ty) ? kStoneDoorOpen : kStoneDoor, x, y);
            break;
    }
}
