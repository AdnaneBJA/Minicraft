#include "furniture.h"

#include "camera.h"
#include "collision.h"
#include "tile_map.h"

#include <algorithm>

namespace {

constexpr float kSpriteSize = 16.0f;

// Column of each furniture type in furniture.png.
int spriteColumn(ItemType type) {
    switch (type) {
        case ItemType::Workbench: return 0;
        default: return 0;
    }
}

}  // namespace

bool Furniture::load(SDL_Renderer* renderer, const std::string& path) {
    texture_ = loadTexture(renderer, path);
    return texture_ != nullptr;
}

bool Furniture::place(ItemType type, int tx, int ty, const TileMap& map, std::span<const SDL_FRect> blockers) {
    // Tile.mayPass for furniture: anything but solid tiles and water (furniture can't swim).
    if (map.isSolidAt(tx, ty) || map.tileAt(tx, ty) == Tile::Water) return false;
    const float size = static_cast<float>(TileMap::kTileSize);
    const SDL_FRect tile{static_cast<float>(tx) * size, static_cast<float>(ty) * size, size, size};
    const bool taken = std::any_of(pieces_.begin(), pieces_.end(), [&](const Piece& piece) {
        return collision::tileIndex(piece.x) == tx && collision::tileIndex(piece.y) == ty;
    });
    const bool blocked = std::any_of(blockers.begin(), blockers.end(),
                                     [&](const SDL_FRect& box) { return SDL_HasRectIntersectionFloat(&box, &tile); });
    if (taken || blocked) return false;
    pieces_.push_back({type, tile.x + size / 2.0f, tile.y + size / 2.0f});
    return true;
}

std::optional<ItemType> Furniture::at(int tx, int ty) const {
    for (const Piece& piece : pieces_) {
        if (collision::tileIndex(piece.x) == tx && collision::tileIndex(piece.y) == ty) return piece.type;
    }
    return std::nullopt;
}

std::vector<SDL_FRect> Furniture::hitboxes() const {
    std::vector<SDL_FRect> boxes;
    boxes.reserve(pieces_.size());
    for (const Piece& piece : pieces_) boxes.push_back(piece.hitbox());
    return boxes;
}

void Furniture::draw(SDL_Renderer* renderer, const Camera& camera, float playerY, bool behind) const {
    for (const Piece& piece : pieces_) {
        if ((piece.y < playerY) == behind) {
            drawSprite(renderer, camera, piece.type, piece.x - kSpriteSize / 2.0f, piece.y - kSpriteSize / 2.0f);
        }
    }
}

void Furniture::drawSprite(SDL_Renderer* renderer, const Camera& camera, ItemType type, float x, float y) const {
    const SDL_FRect source{static_cast<float>(spriteColumn(type)) * kSpriteSize, 0.0f, kSpriteSize, kSpriteSize};
    const SDL_FRect destination{camera.snap(x) - camera.x(), camera.snap(y) - camera.y(), kSpriteSize, kSpriteSize};
    SDL_RenderTexture(renderer, texture_.get(), &source, &destination);
}
