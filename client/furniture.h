#pragma once

#include "day_night.h"
#include "items.h"

#include <SDL3/SDL.h>

#include <span>
#include <vector>

class Camera;
class TileMap;

// Furniture placed on a level (Minicraft's Furniture entities): the crafting stations, chests, lanterns and beds,
// plus the death chest left where the player died. Each piece sits centred on a tile, is drawn as a 16x16 sprite
// from furniture.png and blocks the player and mobs with a small box at its centre.
class Furniture {
public:
    struct Piece {
        ItemType type;
        float x;  // centre, in world pixels (Minicraft's entity position)
        float y;
        bool deathChest = false;  // the chest holding what the player carried when they died
        Inventory contents;       // what a chest holds
        // Minicraft's radii: 3 x 2 for workbenches, lanterns and beds, 3 x 3 for the rest.
        SDL_FRect hitbox() const;
        bool isContainer() const { return type == ItemType::Chest || deathChest; }
    };

    // Places furniture of `type` on tile (tx, ty), like FurnitureItem.interactOn: only on ground a mob could walk
    // on (not water, rock or trees) and not on stairs, and only if no other furniture and none of `blockers` (mob
    // hitboxes) is on that tile. Returns false if it can't go there.
    bool place(ItemType type, int tx, int ty, const TileMap& map, std::span<const SDL_FRect> blockers);
    // Puts back a saved piece as it was.
    void restore(Piece piece) { pieces_.push_back(std::move(piece)); }
    // Leaves a death chest holding `contents` at (x, y) (Player.die's DeathChest).
    void addDeathChest(float x, float y, Inventory contents);

    // The furniture standing on tile (tx, ty), if any.
    Piece* at(int tx, int ty);
    const Piece* at(int tx, int ty) const;
    // Takes a piece out of the world (picked up with the power glove, or an emptied death chest).
    void remove(const Piece* piece);

    // Collision boxes of every piece, for movement.
    std::vector<SDL_FRect> hitboxes() const;
    // The light every placed lantern gives off (radius in world pixels).
    std::vector<Lighting::Light> lights() const;

    // Draws the pieces standing behind (`behind` = true: higher on screen than `playerY`) or in front of the player.
    void draw(SDL_Renderer* renderer, const Camera& camera, SDL_Texture* sheet, float playerY, bool behind) const;
    // Draws the sprite of `type` with its top-left at world (x, y); also used for furniture carried over the head.
    static void drawSprite(SDL_Renderer* renderer, const Camera& camera, SDL_Texture* sheet, ItemType type, float x,
                           float y, bool deathChest = false);

    void clear() { pieces_.clear(); }
    const std::vector<Piece>& all() const { return pieces_; }

private:
    std::vector<Piece> pieces_;
};
