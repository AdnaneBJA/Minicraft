#include "furniture.h"

#include "camera.h"
#include "collision.h"
#include "tile_map.h"

#include <algorithm>
#include <utility>

namespace {

constexpr float kSpriteSize = 16.0f;
constexpr int kDeathChestColumn = 8;

// Column of each furniture type in furniture.png.
int spriteColumn(ItemType type) {
    switch (type) {
        case ItemType::Workbench: return 0;
        case ItemType::Furnace: return 1;
        case ItemType::Oven: return 2;
        case ItemType::Anvil: return 3;
        case ItemType::Chest: return 4;
        case ItemType::Lantern: return 5;
        case ItemType::Loom: return 6;
        case ItemType::Bed: return 7;
        default: return 0;
    }
}

}  // namespace

SDL_FRect Furniture::Piece::hitbox() const {
    const bool flat = type == ItemType::Workbench || type == ItemType::Lantern || type == ItemType::Bed;
    const float yr = flat ? 2.0f : 3.0f;
    return {x - 3.0f, y - yr, 6.0f, yr * 2.0f};
}

bool Furniture::place(ItemType type, int tx, int ty, const TileMap& map, std::span<const SDL_FRect> blockers) {
    // Tile.mayPass for furniture: anything but solid tiles, liquids and holes (furniture can't swim), nor stairs.
    if (!map.inBounds(tx, ty) || map.blocksMobsAt(tx, ty)) return false;
    const Tile tile = map.tileAt(tx, ty);
    if (tile == Tile::StairsDown || tile == Tile::StairsUp) return false;
    const float size = static_cast<float>(TileMap::kTileSize);
    const SDL_FRect area{static_cast<float>(tx) * size, static_cast<float>(ty) * size, size, size};
    const bool blocked = std::any_of(blockers.begin(), blockers.end(),
                                     [&](const SDL_FRect& box) { return SDL_HasRectIntersectionFloat(&box, &area); });
    if (at(tx, ty) || blocked) return false;
    pieces_.push_back({type, area.x + size / 2.0f, area.y + size / 2.0f});
    return true;
}

void Furniture::addDeathChest(float x, float y, Inventory contents) {
    Piece chest{ItemType::Chest, x, y, true, std::move(contents)};
    pieces_.push_back(std::move(chest));
}

Furniture::Piece* Furniture::at(int tx, int ty) {
    for (Piece& piece : pieces_) {
        if (collision::tileIndex(piece.x) == tx && collision::tileIndex(piece.y) == ty) return &piece;
    }
    return nullptr;
}

const Furniture::Piece* Furniture::at(int tx, int ty) const { return const_cast<Furniture*>(this)->at(tx, ty); }

void Furniture::remove(const Piece* piece) {
    std::erase_if(pieces_, [&](const Piece& p) { return &p == piece; });
}

std::vector<SDL_FRect> Furniture::hitboxes() const {
    std::vector<SDL_FRect> boxes;
    boxes.reserve(pieces_.size());
    for (const Piece& piece : pieces_) boxes.push_back(piece.hitbox());
    return boxes;
}

std::vector<Lighting::Light> Furniture::lights() const {
    std::vector<Lighting::Light> lights;
    for (const Piece& piece : pieces_) {
        // Level.renderLight: centred 1 px left and 4 px up of the entity position, radius in 8 px steps.
        if (const int radius = lightRadius(piece.type); radius > 0) {
            lights.push_back({piece.x - 1.0f, piece.y - 4.0f, static_cast<float>(radius * 8)});
        }
    }
    return lights;
}

void Furniture::draw(SDL_Renderer* renderer, const Camera& camera, SDL_Texture* sheet, float playerY,
                     bool behind) const {
    for (const Piece& piece : pieces_) {
        if ((piece.y < playerY) == behind) {
            drawSprite(renderer, camera, sheet, piece.type, piece.x - kSpriteSize / 2.0f, piece.y - kSpriteSize / 2.0f,
                       piece.deathChest);
        }
    }
}

void Furniture::drawSprite(SDL_Renderer* renderer, const Camera& camera, SDL_Texture* sheet, ItemType type, float x,
                           float y, bool deathChest) {
    const int column = deathChest ? kDeathChestColumn : spriteColumn(type);
    const SDL_FRect source{static_cast<float>(column) * kSpriteSize, 0.0f, kSpriteSize, kSpriteSize};
    const SDL_FRect destination{camera.snap(x) - camera.x(), camera.snap(y) - camera.y(), kSpriteSize, kSpriteSize};
    SDL_RenderTexture(renderer, sheet, &source, &destination);
}
