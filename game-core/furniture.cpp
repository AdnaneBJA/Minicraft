#include "furniture.h"

#include "collision.h"
#include "tile_map.h"

#include <algorithm>
#include <utility>

Rect Furniture::Piece::hitbox() const {
    const bool flat = type == ItemType::Workbench || type == ItemType::Lantern || type == ItemType::Bed;
    const float yr = flat ? 2.0f : 3.0f;
    return {x - 3.0f, y - yr, 6.0f, yr * 2.0f};
}

bool Furniture::place(ItemType type, int tx, int ty, const TileMap& map, std::span<const Rect> blockers) {
    // Tile.mayPass for furniture: anything but solid tiles, liquids and holes (furniture can't swim), nor stairs.
    if (!map.inBounds(tx, ty) || map.blocksMobsAt(tx, ty)) return false;
    const Tile tile = map.tileAt(tx, ty);
    if (tile == Tile::StairsDown || tile == Tile::StairsUp) return false;
    const float size = static_cast<float>(TileMap::kTileSize);
    const Rect area{static_cast<float>(tx) * size, static_cast<float>(ty) * size, size, size};
    const bool blocked = std::any_of(blockers.begin(), blockers.end(),
                                     [&](const Rect& box) { return intersects(box, area); });
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

std::vector<Rect> Furniture::hitboxes() const {
    std::vector<Rect> boxes;
    boxes.reserve(pieces_.size());
    for (const Piece& piece : pieces_) boxes.push_back(piece.hitbox());
    return boxes;
}

std::vector<Light> Furniture::lights() const {
    std::vector<Light> lights;
    for (const Piece& piece : pieces_) {
        // Level.renderLight: centred 1 px left and 4 px up of the entity position, radius in 8 px steps.
        if (const int radius = lightRadius(piece.type); radius > 0) {
            lights.push_back({piece.x - 1.0f, piece.y - 4.0f, static_cast<float>(radius * 8)});
        }
    }
    return lights;
}
