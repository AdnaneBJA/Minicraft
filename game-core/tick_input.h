#pragma once

#include <string>
#include <vector>

// Everything the simulation accepts as input. In multiplayer, the server collects these from every player and
// sends the same TickInput to every client, so every client's simulation does exactly the same thing.

// The keys a player holds during one tick.
struct PlayerInput {
    int moveX = 0;               // -1, 0, 1
    int moveY = 0;
    bool attack = false;         // Space held
    bool attackPressed = false;  // Space went down since the last tick
};

// Something a player does once, usually from a menu.
struct PlayerCommand {
    enum class Kind {
        Join,          // a player enters the world (text: their name)
        Leave,         // ... and leaves it
        Respawn,       // after dying
        Craft,         // a: station (an ItemType, or -1 for crafting by hand), b: recipe index in that list
        StowHeld,      // put the held item back in the inventory (opening a menu)
        ReequipHeld,   // take the stowed item back in hand (closing the inventory without picking anything)
        HoldSlot,      // a: inventory slot to take in hand
        UseFurniture,  // E on the furniture in front: stows the held item for a station or chest, sleeps in a bed
        Transfer,      // a, b: chest tile; c: 1 = chest to inventory, 0 = inventory to chest; d: stack index
    };
    Kind kind = Kind::Join;
    int a = 0;
    int b = 0;
    int c = 0;
    int d = 0;
    std::string text;

    static PlayerCommand join(std::string name) { return {.kind = Kind::Join, .text = std::move(name)}; }
    static PlayerCommand leave() { return {.kind = Kind::Leave}; }
    static PlayerCommand respawn() { return {.kind = Kind::Respawn}; }
    static PlayerCommand craft(int station, int recipe) { return {.kind = Kind::Craft, .a = station, .b = recipe}; }
    static PlayerCommand stowHeld() { return {.kind = Kind::StowHeld}; }
    static PlayerCommand reequipHeld() { return {.kind = Kind::ReequipHeld}; }
    static PlayerCommand holdSlot(int slot) { return {.kind = Kind::HoldSlot, .a = slot}; }
    static PlayerCommand useFurniture() { return {.kind = Kind::UseFurniture}; }
    static PlayerCommand transfer(int tileX, int tileY, bool fromChest, int index) {
        return {.kind = Kind::Transfer, .a = tileX, .b = tileY, .c = fromChest ? 1 : 0, .d = index};
    }
};

// What one player did during one tick.
struct PlayerTurn {
    int playerId = 0;
    PlayerInput input;
    std::vector<PlayerCommand> commands;
};

// What every player did during one tick, in player order.
struct TickInput {
    int tick = 0;
    std::vector<PlayerTurn> turns;
};
