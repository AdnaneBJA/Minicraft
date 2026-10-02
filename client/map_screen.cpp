#include "map_screen.h"

#include "collision.h"
#include "font.h"
#include "hud.h"
#include "player.h"
#include "world.h"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace {

constexpr float kCell = 8.0f;
constexpr float kMarker = 3.0f;  // markers are a few view pixels wide so single tiles stay visible when shrunk
constexpr SDL_Color kWhite{255, 255, 255, 255};
constexpr SDL_Color kPlayerColor{255, 255, 255, 255};
constexpr SDL_Color kStairsDownColor{255, 40, 40, 255};
constexpr SDL_Color kStairsUpColor{80, 220, 255, 255};
constexpr SDL_Color kSkyStairsColor{255, 0, 255, 255};
constexpr SDL_Color kBossColor{255, 230, 0, 255};

// The colour each tile shows on the map.
SDL_Color mapColor(Tile tile) {
    switch (tile) {
        case Tile::Grass:
        case Tile::Flower:
        case Tile::Sapling: return {72, 160, 64, 255};
        case Tile::Tree: return {28, 96, 32, 255};
        case Tile::Sand:
        case Tile::Cactus:
        case Tile::CactusSapling: return {222, 208, 128, 255};
        case Tile::Water: return {48, 80, 200, 255};
        case Tile::Lava: return {255, 96, 16, 255};
        case Tile::Rock: return {136, 136, 136, 255};
        case Tile::HardRock: return {64, 64, 80, 255};
        case Tile::IronOre: return {214, 150, 130, 255};
        case Tile::GoldOre: return {240, 210, 40, 255};
        case Tile::GemOre: return {190, 90, 240, 255};
        case Tile::Cloud:
        case Tile::CloudCactus: return {236, 236, 240, 255};
        case Tile::InfiniteFall: return {16, 18, 46, 255};
        case Tile::WoodPlanks:
        case Tile::WoodWall:
        case Tile::WoodDoor: return {160, 112, 64, 255};
        case Tile::StoneBricks:
        case Tile::StoneWall:
        case Tile::StoneDoor: return {170, 170, 180, 255};
        case Tile::Farmland:
        case Tile::Wheat: return {130, 92, 52, 255};
        case Tile::Hole: return {40, 32, 24, 255};
        default: return {112, 80, 48, 255};  // dirt, paths, torches, stairs (stairs get a marker on top)
    }
}

void fill(SDL_Renderer* renderer, SDL_Color color, float x, float y, float size) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    const SDL_FRect rect{x, y, size, size};
    SDL_RenderFillRect(renderer, &rect);
}

// A marker: a coloured square with a black outline, centred on (x, y).
void marker(SDL_Renderer* renderer, SDL_Color color, float x, float y) {
    fill(renderer, SDL_Color{0, 0, 0, 255}, std::floor(x - kMarker / 2.0f) - 1.0f, std::floor(y - kMarker / 2.0f) - 1.0f,
         kMarker + 2.0f);
    fill(renderer, color, std::floor(x - kMarker / 2.0f), std::floor(y - kMarker / 2.0f), kMarker);
}

}  // namespace

void MapScreen::open(SDL_Renderer* renderer, const Level& level) {
    const TileMap& map = level.map;
    width_ = map.width();
    height_ = map.height();
    stairsDown_.clear();
    stairsUp_.clear();
    std::vector<Uint8> pixels(static_cast<std::size_t>(width_ * height_ * 4));
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const Tile tile = map.tileAt(x, y);
            if (tile == Tile::StairsDown) stairsDown_.push_back({x, y});
            if (tile == Tile::StairsUp) stairsUp_.push_back({x, y});
            const SDL_Color c = mapColor(tile == Tile::Torch ? map.groundAt(x, y) : tile);
            Uint8* p = &pixels[static_cast<std::size_t>((x + y * width_) * 4)];
            p[0] = c.r;
            p[1] = c.g;
            p[2] = c.b;
            p[3] = 255;
        }
    }
    if (!texture_ || texture_->w != width_ || texture_->h != height_) {
        texture_.reset(SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, width_, height_));
        // Shrinking 256 tiles into the view: blend neighbouring tiles rather than dropping them.
        if (texture_) SDL_SetTextureScaleMode(texture_.get(), SDL_SCALEMODE_LINEAR);
    }
    if (texture_) SDL_UpdateTexture(texture_.get(), nullptr, pixels.data(), width_ * 4);
    open_ = true;
}

void MapScreen::draw(SDL_Renderer* renderer, const Hud& hud, const Font& font, const Level& level, const Player& player,
                     float viewWidth, float viewHeight) const {
    if (!open_ || !texture_) return;

    // The map fills the view's height inside a frame (whole 8 px cells), with the legend to its right.
    const int cells = std::max(4, static_cast<int>((viewHeight - 3.0f * kCell) / kCell));
    const float size = static_cast<float>(cells) * kCell;
    const float left = kCell * 2.0f;
    const float top = kCell * 2.0f;
    hud.drawFrame(renderer, left, top, cells, cells);
    const std::string title = "Map - " + level.name();
    hud.drawTitle(renderer, font, title, std::floor(left + (size - Font::textWidth(title)) / 2.0f), top - kCell);
    const SDL_FRect area{left, top, size, size};
    SDL_RenderTexture(renderer, texture_.get(), nullptr, &area);

    const float perTile = size / static_cast<float>(width_);
    const auto at = [&](float tileX, float tileY) { return SDL_FPoint{left + tileX * perTile, top + tileY * perTile}; };

    // Stairs down everywhere; stairs up mean the way to the sky (and the boss) on the surface, the way back up in
    // the caves. In the sky, the Air Wizard himself.
    const bool surface = level.depth() == 0;
    const SDL_Color upColor = surface ? kSkyStairsColor : kStairsUpColor;
    for (const SDL_Point& s : stairsDown_) {
        const SDL_FPoint p = at(static_cast<float>(s.x) + 0.5f, static_cast<float>(s.y) + 0.5f);
        marker(renderer, kStairsDownColor, p.x, p.y);
    }
    for (const SDL_Point& s : stairsUp_) {
        const SDL_FPoint p = at(static_cast<float>(s.x) + 0.5f, static_cast<float>(s.y) + 0.5f);
        marker(renderer, upColor, p.x, p.y);
    }
    const Mob* boss = level.mobs.boss();
    if (boss) {
        const SDL_FPoint c = boss->center();
        const SDL_FPoint p = at(c.x / TileMap::kTileSize, c.y / TileMap::kTileSize);
        marker(renderer, kBossColor, p.x, p.y);
    }
    // The player blinks so they stand out from the stairs.
    const bool blinkOn = (SDL_GetTicks() / 300) % 2 == 0;
    const SDL_FPoint c = player.center();
    const SDL_FPoint p = at(c.x / TileMap::kTileSize, c.y / TileMap::kTileSize);
    if (blinkOn) marker(renderer, kPlayerColor, p.x, p.y);

    // Legend.
    struct Entry {
        SDL_Color color;
        std::string_view text;
    };
    std::vector<Entry> legend{{kPlayerColor, "You"}};
    if (!stairsDown_.empty()) legend.push_back({kStairsDownColor, surface ? "Caves" : "Stairs down"});
    if (!stairsUp_.empty()) legend.push_back({upColor, surface ? "Sky (boss)" : "Stairs up"});
    if (boss) legend.push_back({kBossColor, "Air Wizard"});
    float legendX = left + size + kCell * 2.0f;
    if (legendX + 12.0f * kCell > viewWidth) return;  // no room in a narrow view: the colours speak for themselves
    float y = top;
    for (const Entry& entry : legend) {
        marker(renderer, entry.color, legendX + 2.0f, y + 4.0f);
        font.drawShadowed(renderer, entry.text, legendX + 8.0f, y, kWhite);
        y += kCell + 4.0f;
    }
    font.drawShadowed(renderer, "TAB to close", legendX, top + size - kCell, SDL_Color{153, 153, 153, 255});
}
