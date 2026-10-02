#pragma once

#include "day_night.h"
#include "events.h"
#include "geometry.h"
#include "items.h"
#include "player.h"
#include "random.h"
#include "world.h"

#include <cstdint>
#include <optional>
#include <vector>

class Recipe;

// One player's game: the world, the player and their inventory, the time of day, and the rules that tie them
// together (stairs, death and respawn, sleeping, crafting, chests). Everything advances in fixed 60 Hz ticks from a
// PlayerInput, with randomness from one seeded generator, so the same seed and inputs always give the same game.
// The simulation never draws, plays sounds or touches files: what the player should see or hear is recorded in
// events() for the client to act on.
class Simulation {
public:
    static constexpr int kTicksPerSecond = 60;

    // A brand-new world from `seed`: every level generated, the player at the surface spawn point with the power
    // glove (like the original Minicraft).
    void startNewWorld(std::uint32_t seed);
    // A fresh session on the current world (before restoring a save): stats, inventory, time of day and spawn
    // point back to the start. The tiles stay. Randomness restarts from `rngSeed`.
    void resetSession(std::uint64_t rngSeed);
    // Debug: a whole new world from `seed`, keeping the player's things.
    void regenerate(std::uint32_t seed);

    // One 60 Hz tick. Does nothing while the player is dead (waiting for respawn()).
    void tick(const PlayerInput& input);

    // --- What the player does through the menus.
    // Crafts `recipe` from the inventory; products that don't fit drop at the player's feet. False if unaffordable.
    bool craft(const Recipe& recipe);
    // Puts the held item back in the inventory, or drops it if there's no room (Minicraft's tryAddToInvOrDrop).
    // Returns the slot it went into, or nothing if there was no held item or it was dropped.
    std::optional<int> stowHeldItem();
    // Takes the stack in inventory slot `index` into the player's hand.
    void holdSlot(int index);
    // Adds items to the inventory; what doesn't fit drops at the player's feet.
    void giveItems(ItemType type, int count);

    // E on the furniture in front (Furniture.use).
    struct FurnitureUse {
        enum class Kind { None, Station, Chest, Slept, CantSleep };
        Kind kind = Kind::None;
        ItemType station = ItemType::Workbench;  // Station: whose recipes to show
        Point tile;                              // Chest: where it stands
        bool deathChest = false;
    };
    FurnitureUse useFurniture();
    // Moves the stack at `index` between the chest on `chestTile` and the inventory (`fromChest`: chest to
    // inventory). What doesn't fit stays where it was; an emptied death chest disappears. Returns false if the chest
    // is gone (the menu should close).
    bool transfer(Point chestTile, bool fromChest, int index);

    // Back at the bed the player last slept in, or the surface spawn point, with full stats.
    void respawn();
    // Moves the player to another World level at the same position (the stairs line up).
    void changeLevel(int index, bool viaStairs);

    // --- State.
    World& world() { return world_; }
    const World& world() const { return world_; }
    Level& level() { return world_.current(); }
    const Level& level() const { return world_.current(); }
    Player& player() { return player_; }
    const Player& player() const { return player_; }
    Inventory& inventory() { return inventory_; }
    const Inventory& inventory() const { return inventory_; }
    DayNight& dayNight() { return dayNight_; }
    const DayNight& dayNight() const { return dayNight_; }
    Random& rng() { return rng_; }
    Events& events() { return events_; }

    // The surface's spawn point (sprite top-left, world pixels).
    Vec2 surfaceSpawn() const;
    // Where the player respawns: a bed's level (World index) and position, or level -1 for the surface spawn.
    int spawnLevel() const { return spawnLevel_; }
    Vec2 spawnPoint() const { return spawnPoint_; }
    void setSpawn(int level, Vec2 point) {
        spawnLevel_ = level;
        spawnPoint_ = point;
    }
    int ticksPlayed() const { return ticksPlayed_; }
    void setTicksPlayed(int ticks) { ticksPlayed_ = ticks; }
    int secondsPlayed() const { return ticksPlayed_ / kTicksPerSecond; }
    // Arriving on stairs (or loading a save made on them) doesn't take them straight away.
    void setOnStairs(bool onStairs) { onStairs_ = onStairs; }

    // The light sources around the player on the current level: the player, placed lanterns, and torches and lava
    // within reach. Enemies never spawn in them; the client cuts the darkness away around them.
    std::vector<Light> lights() const;

    // A fingerprint of the game state (player, inventory, time, every level's tiles, mobs, drops and furniture).
    // Two simulations that ran the same seed and inputs have the same hash.
    std::uint64_t stateHash() const;

private:
    void die();
    void sleep(const Furniture::Piece& bed);
    void useOrPunch();
    // Puts a stack in the inventory, or drops what doesn't fit at the player's feet.
    void addOrDrop(const Inventory::Stack& stack);

    World world_;
    Player player_;
    Inventory inventory_;
    DayNight dayNight_;
    Random rng_;
    Events events_;
    int spawnLevel_ = -1;
    Vec2 spawnPoint_;
    int ticksPlayed_ = 0;
    int punchRepeatTicks_ = 0;  // held Space: ticks until the next automatic punch
    bool onStairs_ = false;     // standing on stairs (stepping onto them takes them)
    bool dead_ = false;
};
