#pragma once

#include "items.h"
#include "tile_map.h"

#include <SDL3/SDL.h>

class DroppedItems;
class Effects;
class Mobs;
class Player;

// What a swing does (Minicraft's Player.attack): punching bare-handed, or swinging a tool, which first tries the
// tool's use on the tile in front (axe on trees, pickaxe on rock or grass, shovel and hoe on soil) and otherwise
// attacks with it. Works on the game's world objects; holds no state of its own.
class PlayerActions {
public:
    PlayerActions(TileMap& map, Player& player, Mobs& mobs, Effects& effects, DroppedItems& drops)
        : map_(map), player_(player), mobs_(mobs), effects_(effects), drops_(drops) {}

    // Bare hands: 1 energy; mobs in reach take 1-2, the tile in front 1-3.
    void punch();
    // The held tool: 1 energy, then its tile use or an attack with it. The tool breaks at 0 durability.
    void swingTool();

private:
    void attack(Inventory::Stack* tool);
    bool useToolOnTile(Inventory::Stack& tool);
    void onTileHit(SDL_Point target, int damage, const TileMap::TileHit& hit, bool withPickaxe);

    TileMap& map_;
    Player& player_;
    Mobs& mobs_;
    Effects& effects_;
    DroppedItems& drops_;
};
