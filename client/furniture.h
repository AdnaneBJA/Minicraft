#pragma once

#include "items.h"
#include "texture.h"

#include <SDL3/SDL.h>

#include <optional>
#include <span>
#include <string>
#include <vector>

class Camera;
class TileMap;

// Furniture placed in the world (Minicraft's Furniture entities; only the workbench so far). Each piece sits centred
// on a tile, is drawn as a 16x16 sprite and blocks the player and mobs with a small box at its centre.
class Furniture {
public:
    struct Piece {
        ItemType type;
        float x;  // centre, in world pixels (Minicraft's entity position)
        float y;
        // Minicraft's Workbench radii (xr = 3, yr = 2).
        SDL_FRect hitbox() const { return {x - 3.0f, y - 2.0f, 6.0f, 4.0f}; }
    };

    // Loads furniture.png (one 16x16 sprite per furniture type).
    bool load(SDL_Renderer* renderer, const std::string& path);

    // Places furniture of `type` on tile (tx, ty), like FurnitureItem.interactOn: only on ground a mob could walk
    // on (not water, rock or trees), and only if no other furniture and none of `blockers` (mob hitboxes) is on that
    // tile. Returns false if it can't go there.
    bool place(ItemType type, int tx, int ty, const TileMap& map, std::span<const SDL_FRect> blockers);

    // The furniture standing on tile (tx, ty), if any.
    std::optional<ItemType> at(int tx, int ty) const;

    // Collision boxes of every piece, for movement.
    std::vector<SDL_FRect> hitboxes() const;

    // Draws the pieces standing behind (`behind` = true: higher on screen than `playerY`) or in front of the player.
    void draw(SDL_Renderer* renderer, const Camera& camera, float playerY, bool behind) const;
    // Draws the sprite of `type` with its top-left at world (x, y); also used for furniture carried over the head.
    void drawSprite(SDL_Renderer* renderer, const Camera& camera, ItemType type, float x, float y) const;

    void clear() { pieces_.clear(); }
    const std::vector<Piece>& all() const { return pieces_; }

private:
    TexturePtr texture_;
    std::vector<Piece> pieces_;
};
