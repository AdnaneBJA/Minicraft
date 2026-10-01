#pragma once

#include "items.h"
#include "tile_map.h"

#include <SDL3/SDL.h>

#include <optional>
#include <string>
#include <vector>

class Audio;
class Effects;
class Level;
class Player;

// What a swing or a use does (Minicraft's Player.attack / Item.interactOn): punching bare-handed, swinging a tool
// (its use on the tile in front, or an attack), shooting a bow, eating, putting on armour, placing tiles and
// furniture, and picking furniture up with the power glove. Works on the game's objects; holds no state of its own
// beyond the messages it wants shown.
class PlayerActions {
public:
    PlayerActions(Level& level, Player& player, Inventory& inventory, Effects& effects, Audio& audio)
        : level_(level), player_(player), inventory_(inventory), effects_(effects), audio_(audio) {}

    // Space: punch with an empty hand, otherwise use or swing the held item.
    void useOrPunch();

    // Short notes for the player ("Gem pickaxe required!"), shown by the game.
    const std::vector<std::string>& messages() const { return messages_; }

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
    void onTileHit(SDL_Point target, int damage, const TileMap::TileHit& hit, bool withPickaxe);
    // Takes one of the held stackable item; empties the hand when it runs out.
    void consumeHeld();

    Level& level_;
    Player& player_;
    Inventory& inventory_;
    Effects& effects_;
    Audio& audio_;
    std::vector<std::string> messages_;
};
