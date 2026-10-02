#pragma once

#include "geometry.h"
#include "items.h"
#include "player.h"
#include "tile_map.h"

#include <optional>
#include <span>

class Events;
class Level;
class Random;

// What a swing or a use does (Minicraft's Player.attack / Item.interactOn): punching bare-handed, swinging a tool
// (its use on the tile in front, or an attack), shooting a bow, eating, putting on armour, placing tiles and
// furniture, and picking furniture up with the power glove. Works on the game's objects; holds no state of its
// own; what the player should see or hear goes into the events.
class PlayerActions {
public:
    // `others`: the other living players on the same level, who punches, tools and arrows can hit (PvP).
    PlayerActions(Level& level, Player& player, std::span<Player* const> others, Events& events, Random& rng)
        : level_(level), player_(player), inventory_(player.inventory()), others_(others), events_(events),
          rng_(rng) {}

    // Space: punch with an empty hand, otherwise use or swing the held item.
    void useOrPunch();


private:
    // Bare hands: 1 energy; mobs in reach take 1-2, the tile in front 1-3.
    void punch();
    // The held tool: 1 energy, then its tile use or an attack with it. The tool breaks at 0 durability.
    void swingTool();
    // A bow: shoots one arrow from the inventory in the facing direction.
    bool shootBow(Inventory::Stack& bow);
    void attack(Inventory::Stack* tool);
    bool useToolOnTile(Inventory::Stack& tool);
    // Food, armour, tiles, furniture and the power glove.
    void useItem();
    // TileItem.interactOn: the tile the held item turns `target` into, if it can be placed there.
    std::optional<Tile> placedTile(ItemType item, Tile target) const;
    void onTileHit(Point target, int damage, const TileMap::TileHit& hit, bool withPickaxe);
    // Takes one of the held stackable item; empties the hand when it runs out.
    void consumeHeld();

    Level& level_;
    Player& player_;
    Inventory& inventory_;
    std::span<Player* const> others_;
    Events& events_;
    Random& rng_;
};
