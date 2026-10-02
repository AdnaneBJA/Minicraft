#pragma once

#include "items.h"
#include "mob.h"
#include "texture.h"

#include <SDL3/SDL.h>

#include <array>
#include <string>

class Camera;
class DroppedItems;
class Furniture;
class ItemIcons;
class Mobs;
class Player;
class Projectiles;

// Draws everything on a level that isn't a tile: the player, mobs, furniture, items on the ground, arrows and
// sparks. Owns their sprite sheets (and the white silhouettes shown while hurt). The simulation only exposes state;
// all the sprite-sheet layouts live here.
class SpriteRenderer {
public:
    // Loads player.png, hud.png (slash and ripple), the mob sheets, furniture.png and projectiles.png from
    // `spriteDirectory`.
    bool load(SDL_Renderer* renderer, const std::string& spriteDirectory);

    void drawPlayer(SDL_Renderer* renderer, const Camera& camera, const Player& player) const;
    // Draws the mobs standing behind (`behind` = true: higher on screen than `playerY`) or in front of the player.
    void drawMobs(SDL_Renderer* renderer, const Camera& camera, const Mobs& mobs, float playerY, bool behind) const;
    // The same split for furniture.
    void drawFurniture(SDL_Renderer* renderer, const Camera& camera, const Furniture& furniture, float playerY,
                       bool behind) const;
    // The 16x16 sprite of a furniture type with its top-left at world (x, y); also furniture carried over the head.
    void drawFurnitureSprite(SDL_Renderer* renderer, const Camera& camera, ItemType type, float x, float y,
                             bool deathChest = false) const;
    void drawDrops(SDL_Renderer* renderer, const Camera& camera, const DroppedItems& drops,
                   const ItemIcons& icons) const;
    void drawProjectiles(SDL_Renderer* renderer, const Camera& camera, const Projectiles& projectiles) const;

private:
    void drawMob(SDL_Renderer* renderer, const Camera& camera, const Mob& mob) const;
    // Sprite cell (column, row) of a mob sheet at the mob's position, `raise` px higher.
    void drawMobFrame(SDL_Renderer* renderer, const Camera& camera, const Mob& mob, bool flash, int column, int row,
                      bool mirrored, float raise = 0.0f) const;
    void drawSlash(SDL_Renderer* renderer, const Player& player, float x, float y) const;

    TexturePtr player_;
    TexturePtr playerFlash_;
    TexturePtr hud_;
    TexturePtr furniture_;
    TexturePtr projectiles_;
    std::array<TexturePtr, kMobKinds> mobs_;
    std::array<TexturePtr, kMobKinds> mobFlashes_;
};
