#pragma once

#include "day_night.h"
#include "events.h"
#include "geometry.h"
#include "items.h"
#include "player.h"
#include "random.h"
#include "tick_input.h"
#include "world.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class Recipe;

// The game: the world, the players in it, the time of day, and the rules that tie them together (stairs, death
// and respawn, beds, crafting, chests, PvP). It advances one 60 Hz tick at a time from a TickInput (what every
// player did that tick), with randomness from one seeded generator, so the same seed and the same inputs always
// give the same game. That is what makes multiplayer work: the server sends every client the same inputs, and
// every client's simulation stays identical.
//
// The simulation never draws, plays sounds or touches files: what players should see or hear is recorded in
// events() for the clients to act on.
class Simulation {
public:
    static constexpr int kTicksPerSecond = 60;

    // A brand-new world from `seed`, every level generated, with nobody in it yet (players arrive with a Join
    // command, or addPlayer()).
    void startNewWorld(std::uint32_t seed);
    // Restarts the random numbers, the clock and the player list for a save being restored; the tiles stay.
    void resetSession(std::uint64_t rngSeed);
    // Debug: a whole new world from `seed`, keeping the players and their things.
    void regenerate(std::uint32_t seed);

    // One 60 Hz tick: everyone's commands, then everyone's movement and actions, then every level with somebody on
    // it (mobs, projectiles, growing tiles, item pickups).
    void tick(const TickInput& input);

    // A player arrives at the surface spawn point with the power glove (like the original Minicraft). Returns
    // them. The Join command does this; single-player and save loading call it directly.
    Player& addPlayer(int id, std::string name);
    void removePlayer(int id);
    Player* findPlayer(int id);
    const Player* findPlayer(int id) const;
    const std::vector<std::unique_ptr<Player>>& players() const { return players_; }

    // --- State.
    World& world() { return world_; }
    const World& world() const { return world_; }
    // The level a player is on.
    Level& levelOf(const Player& player) { return world_.level(player.level()); }
    const Level& levelOf(const Player& player) const { return world_.level(player.level()); }
    DayNight& dayNight() { return dayNight_; }
    const DayNight& dayNight() const { return dayNight_; }
    Random& rng() { return rng_; }
    Events& events() { return events_; }
    int tickCount() const { return tick_; }
    void setTickCount(int tick) { tick_ = tick; }
    int secondsPlayed() const { return tick_ / kTicksPerSecond; }
    // Single-player: sleeping skips the night. In multiplayer a bed only sets the respawn point.
    bool singlePlayer = true;

    // The surface's spawn point (sprite top-left, world pixels).
    Vec2 surfaceSpawn() const;
    // Moves a player to another World level at the same position (the stairs line up).
    void changeLevel(Player& player, int index, bool viaStairs);
    // Adds items to a player's inventory; what doesn't fit drops at their feet.
    void giveItems(Player& player, ItemType type, int count);

    // The light sources on a level: its players, placed lanterns, and torches and lava near the players. Enemies
    // never spawn in them; the client cuts the darkness away around them.
    std::vector<Light> lights(int level) const;

    // A fingerprint of the game state (players, time, every level's tiles, mobs, drops and furniture). Two
    // simulations that ran the same seed and inputs have the same hash; in multiplayer the server compares them
    // to detect a client that went out of sync.
    std::uint64_t stateHash() const;

private:
    void applyCommand(Player& player, const PlayerCommand& command);
    void movePlayer(Player& player, const PlayerInput& input);
    void tickLevel(int index);
    // The living players on a level (optionally leaving one out: the attacker, for PvP).
    std::vector<Player*> playersOn(int level, const Player* except = nullptr) const;

    void craft(Player& player, int station, int recipe);
    std::optional<int> stowHeldItem(Player& player);
    void reequipHeldItem(Player& player);
    void holdSlot(Player& player, int slot);
    void useFurniture(Player& player);
    void sleep(Player& player, const Furniture::Piece& bed);
    void transfer(Player& player, Point chestTile, bool fromChest, int index);
    void die(Player& player);
    void respawn(Player& player);

    World world_;
    std::vector<std::unique_ptr<Player>> players_;  // in the order they joined
    DayNight dayNight_;
    Random rng_;
    Events events_;
    int tick_ = 0;
};
