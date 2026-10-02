#pragma once

#include "texture.h"
#include "tile_map.h"

#include <SDL3/SDL.h>

#include <string>

class Camera;

// Draws a level's tiles from the tile atlas (tiles.png) with Minicraft+'s connected borders, animated water and
// lava, and the sprites that sit on tiles (trees, flowers, ores, stairs, crops, doors, torches).
class TileRenderer {
public:
    bool load(SDL_Renderer* renderer, const std::string& atlasPath);

    // Draws the tiles the camera can see. `timeSeconds` drives the water and lava animation.
    void draw(SDL_Renderer* renderer, const Camera& camera, const TileMap& map, float timeSeconds) const;

private:
    void drawTile(SDL_Renderer* renderer, const TileMap& map, int tx, int ty, Tile tile, float x, float y,
                  int lavaFrame, int waterFrame) const;

    TexturePtr atlas_;
};
