#include "simulation.h"

#include "collision.h"
#include "player_actions.h"
#include "recipe.h"

#include <algorithm>
#include <bit>

namespace {

// Holding Space works like Minicraft: the press punches once, and only once the key has been held for a moment
// (Minicraft waits for the OS key repeat to make the key "sticky") does it unload rapid punches until energy runs
// out.
constexpr int kPunchHoldDelayTicks = 30;
constexpr int kRapidPunchTicks = 3;  // 20 punches/s
constexpr int kLightScanTiles = 24;  // torches and lava this far from a player give light

// FNV-1a, for stateHash.
class Hasher {
public:
    void bytes(const void* data, std::size_t size) {
        const auto* p = static_cast<const unsigned char*>(data);
        for (std::size_t i = 0; i < size; ++i) {
            hash_ ^= p[i];
            hash_ *= 1099511628211ULL;
        }
    }
    void value(int v) { bytes(&v, sizeof v); }
    void value(float v) { value(std::bit_cast<int>(v)); }
    std::uint64_t result() const { return hash_; }

private:
    std::uint64_t hash_ = 14695981039346656037ULL;
};

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// The world and its players

void Simulation::resetSession(std::uint64_t rngSeed) {
    rng_.reseed(rngSeed);
    events_.take();
    players_.clear();
    dayNight_ = DayNight{};
    tick_ = 0;
}

void Simulation::startNewWorld(std::uint32_t seed) {
    resetSession(seed);
    world_.generate(seed, rng_);
}

void Simulation::regenerate(std::uint32_t seed) {
    rng_.reseed(seed);
    world_.generate(seed, rng_);
    const Vec2 spawn = surfaceSpawn();
    for (auto& player : players_) {
        player->setLevel(World::kSurfaceIndex);
        player->setPosition(spawn.x, spawn.y);
        player->setOnStairs(false);
    }
}

Vec2 Simulation::surfaceSpawn() const { return world_.level(World::kSurfaceIndex).map.findSpawnPoint(); }

Player& Simulation::addPlayer(int id, std::string name) {
    auto player = std::make_unique<Player>(id, std::move(name));
    const Vec2 spawn = surfaceSpawn();
    player->setPosition(spawn.x, spawn.y);
    player->setLevel(World::kSurfaceIndex);
    player->inventory().add(ItemType::PowerGlove);  // like the original Minicraft, players start with the glove
    player->startLife(tick_);
    players_.push_back(std::move(player));
    return *players_.back();
}

void Simulation::removePlayer(int id) {
    std::erase_if(players_, [&](const auto& player) { return player->id() == id; });
}

Player* Simulation::findPlayer(int id) {
    for (auto& player : players_) {
        if (player->id() == id) return player.get();
    }
    return nullptr;
}

const Player* Simulation::findPlayer(int id) const { return const_cast<Simulation*>(this)->findPlayer(id); }

std::vector<Player*> Simulation::playersOn(int level, const Player* except) const {
    std::vector<Player*> result;
    for (const auto& player : players_) {
        if (player->level() == level && !player->waitingToRespawn() && player.get() != except) {
            result.push_back(player.get());
        }
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------
// One tick

void Simulation::tick(const TickInput& input) {
    ++tick_;

    // 1. Commands first: players joining and leaving, menus, respawns.
    for (const PlayerTurn& turn : input.turns) {
        for (const PlayerCommand& command : turn.commands) {
            if (command.kind == PlayerCommand::Kind::Join) {
                if (!findPlayer(turn.playerId)) addPlayer(turn.playerId, command.text);
                continue;
            }
            if (command.kind == PlayerCommand::Kind::Leave) {
                removePlayer(turn.playerId);
                continue;
            }
            if (Player* player = findPlayer(turn.playerId)) applyCommand(*player, command);
        }
    }

    // 2. Every player moves and acts, in the order the server listed them.
    for (const PlayerTurn& turn : input.turns) {
        Player* player = findPlayer(turn.playerId);
        if (player && !player->waitingToRespawn()) movePlayer(*player, turn.input);
    }

    // 3. The world: time, then every level somebody is on.
    dayNight_.step();
    for (int level = 0; level < World::kLevelCount; ++level) {
        if (!playersOn(level).empty()) tickLevel(level);
    }

    // 4. Whoever ran out of health dies.
    for (auto& player : players_) {
        if (player->isDead() && !player->waitingToRespawn()) die(*player);
    }
    events_.setContext(0, -1);
}

void Simulation::movePlayer(Player& player, const PlayerInput& input) {
    Level& here = levelOf(player);
    events_.setContext(player.level(), player.id());
    const std::vector<Rect> obstacles = here.furniture.hitboxes();
    player.tick(input, here.map, obstacles, events_);

    // Space: a fresh press always acts; held, it repeats after a short delay.
    const auto act = [&] {
        const std::vector<Player*> others = playersOn(player.level(), &player);
        PlayerActions(here, player, others, events_, rng_).useOrPunch();
    };
    if (input.attackPressed) {
        act();
        player.setPunchRepeatTicks(kPunchHoldDelayTicks);
    } else if (input.attack) {
        player.setPunchRepeatTicks(player.punchRepeatTicks() - 1);
        if (player.punchRepeatTicks() <= 0) {
            act();
            player.setPunchRepeatTicks(player.punchRepeatTicks() + kRapidPunchTicks);
        }
    }

    // Stepping onto stairs takes them (Player.tick's onStairDelay): arriving on the other end doesn't send the
    // player straight back, since they have to step off the stairs first.
    const Vec2 c = player.center();
    const int tx = collision::tileIndex(c.x);
    const int ty = collision::tileIndex(c.y);
    const Tile under = here.map.inBounds(tx, ty) ? here.map.tileAt(tx, ty) : Tile::Rock;
    const bool stairs = under == Tile::StairsDown || under == Tile::StairsUp;
    if (stairs && !player.onStairs()) {
        const int next = player.level() + (under == Tile::StairsDown ? 1 : -1);
        if (next >= 0 && next < World::kLevelCount) {
            changeLevel(player, next, true);
            return;
        }
    }
    player.setOnStairs(stairs);
}

void Simulation::tickLevel(int index) {
    Level& level = world_.level(index);
    events_.setContext(index, -1);
    const std::vector<Player*> players = playersOn(index);
    const std::vector<Rect> obstacles = level.furniture.hitboxes();
    const std::vector<Light> lightSources = lights(index);
    const Mobs::Context context{level.map, players, events_, rng_, level.drops, level.projectiles, obstacles};
    const Mobs::SpawnRules rules{level.depth(), dayNight_.time() == DayNight::Time::Night, lightSources};
    level.mobs.tick(context, rules);

    // Level.tick: arrows and sparks, and Minicraft's random tile ticks (crops grow, grass spreads, liquids flow
    // into holes).
    level.projectiles.tick(level.map, players, level.mobs, events_, rng_);
    level.map.tickRandomTiles(World::kSize / 2, World::kSize / 2, World::kSize / 2, World::kSize * World::kSize / 50,
                              rng_);
    if (level.drops.tick(level.map, players, events_) > 0) events_.sound(Sound::Pickup);

    if (level.mobs.takeBossDefeated()) {
        world_.airWizardBeaten = true;
        events_.push({.kind = GameEvent::Kind::BossDefeated, .player = level.mobs.bossKiller()});
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Commands

void Simulation::applyCommand(Player& player, const PlayerCommand& command) {
    events_.setContext(player.level(), player.id());
    using Kind = PlayerCommand::Kind;
    switch (command.kind) {
        case Kind::Respawn:
            if (player.waitingToRespawn()) respawn(player);
            break;
        case Kind::Craft: craft(player, command.a, command.b); break;
        case Kind::StowHeld: stowHeldItem(player); break;
        case Kind::ReequipHeld: reequipHeldItem(player); break;
        case Kind::HoldSlot: holdSlot(player, command.a); break;
        case Kind::UseFurniture: useFurniture(player); break;
        case Kind::Transfer: transfer(player, {command.a, command.b}, command.c != 0, command.d); break;
        case Kind::Join:
        case Kind::Leave: break;  // handled by tick()
    }
}

void Simulation::giveItems(Player& player, ItemType type, int count) {
    if (const int leftover = player.inventory().add(type, count); leftover > 0) {
        const Vec2 middle = player.center();
        levelOf(player).drops.spawn(type, leftover, middle.x, middle.y, rng_);
    }
}

void Simulation::craft(Player& player, int station, int recipe) {
    // The same lists the menus show: by hand, or a station's.
    const std::vector<Recipe> recipes =
        station < 0 ? Recipe::personalRecipes() : Recipe::stationRecipes(static_cast<ItemType>(station));
    if (recipe < 0 || recipe >= static_cast<int>(recipes.size())) return;
    const int leftover = recipes[static_cast<std::size_t>(recipe)].craft(player.inventory());
    if (leftover < 0) return;  // can't afford it
    events_.sound(Sound::Craft);
    const Recipe& made = recipes[static_cast<std::size_t>(recipe)];
    events_.push({.kind = GameEvent::Kind::ItemCrafted, .value = static_cast<int>(made.product()),
                  .count = made.amount(), .player = player.id()});
    if (leftover > 0) {
        const Vec2 middle = player.center();
        levelOf(player).drops.spawn(recipes[static_cast<std::size_t>(recipe)].product(), leftover, middle.x,
                                    middle.y, rng_);
    }
}

std::optional<int> Simulation::stowHeldItem(Player& player) {
    // Minicraft's tryAddToInvOrDrop: back in the inventory, or dropped if there's no room.
    const auto held = player.heldItem();
    if (!held) return std::nullopt;
    player.setHeldItem(std::nullopt);
    Inventory& inventory = player.inventory();
    if (const int leftover = inventory.add(*held); leftover > 0) {
        const Vec2 middle = player.center();
        levelOf(player).drops.spawn(held->type, leftover, middle.x, middle.y, rng_, held->durability);
        return std::nullopt;
    }
    // Stackable items merge into the stack of their type; tools go into a new last slot.
    const auto& stacks = inventory.stacks();
    int slot = static_cast<int>(stacks.size()) - 1;
    if (isStackable(held->type)) {
        const auto it = std::find_if(stacks.begin(), stacks.end(), [&](const auto& s) { return s.type == held->type; });
        slot = static_cast<int>(it - stacks.begin());
    }
    player.stowedSlot = std::pair{slot, held->type};
    return slot;
}

void Simulation::reequipHeldItem(Player& player) {
    // Closing the inventory without choosing another item puts the item that was in hand back in hand.
    const auto stowed = std::exchange(player.stowedSlot, std::nullopt);
    if (!stowed || player.heldItem()) return;
    const auto& stacks = player.inventory().stacks();
    const auto [slot, type] = *stowed;
    if (slot < static_cast<int>(stacks.size()) && stacks[static_cast<std::size_t>(slot)].type == type) {
        holdSlot(player, slot);
    }
}

void Simulation::holdSlot(Player& player, int slot) {
    player.stowedSlot.reset();
    if (slot < 0 || slot >= static_cast<int>(player.inventory().stacks().size())) return;
    stowHeldItem(player);  // whatever was in hand goes back first
    player.stowedSlot.reset();
    if (slot >= static_cast<int>(player.inventory().stacks().size())) return;
    player.setHeldItem(player.inventory().take(slot));
}

void Simulation::useFurniture(Player& player) {
    // Furniture.use: a station or chest opens a menu (the client shows it; here the held item goes away), a bed
    // is slept in.
    const Point target = player.interactionTile();
    const Furniture::Piece* piece = levelOf(player).furniture.at(target.x, target.y);
    if (!piece) return;
    if (piece->type == ItemType::Bed) {
        sleep(player, *piece);
        return;
    }
    if (piece->isContainer() || !Recipe::stationRecipes(piece->type).empty()) stowHeldItem(player);
}

void Simulation::sleep(Player& player, const Furniture::Piece& bed) {
    // Bed.use: the bed becomes the respawn point. Alone, sleeping skips the night (only in the evening or at
    // night); with others around, the night goes on for them.
    const DayNight::Time time = dayNight_.time();
    if (singlePlayer && time != DayNight::Time::Evening && time != DayNight::Time::Night) {
        events_.notify("Can't sleep! Wait for the evening");
        return;
    }
    // Wake up just below the bed (or on it, if that tile is blocked).
    Level& here = levelOf(player);
    const int tx = collision::tileIndex(bed.x);
    const int ty = collision::tileIndex(bed.y);
    const bool below = !here.map.isSolidAt(tx, ty + 1) && !here.furniture.at(tx, ty + 1);
    const Vec2 spot{static_cast<float>(tx * TileMap::kTileSize),
                    static_cast<float>((below ? ty + 1 : ty) * TileMap::kTileSize) - 3.0f};
    player.setSpawn(player.level(), spot);
    if (!singlePlayer) {
        events_.notify("Respawn point set");
        return;
    }
    dayNight_.setTime(DayNight::Time::Morning);
    player.setPosition(spot.x, spot.y);
    here.mobs.clearEnemies();  // the night's monsters are gone by morning
    events_.push({.kind = GameEvent::Kind::Slept, .player = player.id()});
    events_.notify("You slept until morning");
}

void Simulation::transfer(Player& player, Point chestTile, bool fromChest, int index) {
    // Moves a stack between a chest and the inventory. What doesn't fit stays where it was.
    Furniture& furniture = levelOf(player).furniture;
    Furniture::Piece* chest = furniture.at(chestTile.x, chestTile.y);
    if (!chest || !chest->isContainer()) return;
    Inventory& from = fromChest ? chest->contents : player.inventory();
    Inventory& to = fromChest ? player.inventory() : chest->contents;
    if (index < 0 || index >= static_cast<int>(from.stacks().size())) return;
    const Inventory::Stack stack = from.take(index);
    if (const int leftover = to.add(stack); leftover > 0) {
        Inventory::Stack back = stack;
        back.count = leftover;
        from.add(back);
    }
    events_.sound(Sound::Pickup);
    // An emptied death chest disappears (DeathChest).
    if (chest->deathChest && chest->contents.empty()) furniture.remove(chest);
}

void Simulation::die(Player& player) {
    // Player.die: everything the player carried goes into a death chest where they fell.
    events_.setContext(player.level(), player.id());
    Inventory carried = player.inventory();
    if (const auto& held = player.heldItem()) carried.add(*held);
    if (const auto& armor = player.armor()) carried.add(*armor, 1);
    if (!carried.empty()) {
        const Vec2 c = player.center();
        levelOf(player).furniture.addDeathChest(c.x, c.y, std::move(carried));
    }
    player.inventory().clear();
    player.setHeldItem(std::nullopt);
    player.removeArmor();
    player.setWaitingToRespawn(true);
    events_.sound(Sound::Death);
    events_.push({.kind = GameEvent::Kind::PlayerDied, .value = secondsPlayed(), .player = player.id()});
    events_.push({.kind = GameEvent::Kind::PlayerKilled,
                  .value = (tick_ - player.lifeStartTick()) / kTicksPerSecond,
                  .killer = player.lastDamage(),
                  .player = player.id()});
}

void Simulation::respawn(Player& player) {
    // Back at the bed the player last slept in, or the surface spawn point, with full stats.
    int index = World::kSurfaceIndex;
    Vec2 spawn = surfaceSpawn();
    if (player.spawnLevel() >= 0) {
        index = player.spawnLevel();
        spawn = player.spawnPoint();
    }
    player.setWaitingToRespawn(false);
    changeLevel(player, index, false);
    player.setPosition(spawn.x, spawn.y);
    player.startLife(tick_);
    player.refillStats();
    player.setOnStairs(false);
}

void Simulation::changeLevel(Player& player, int index, bool viaStairs) {
    if (index == player.level() || index < 0 || index >= World::kLevelCount) return;
    player.setLevel(index);
    player.setOnStairs(viaStairs);
    if (world_.level(index).isSky()) world_.spawnBoss(rng_);
    events_.setContext(index, player.id());
    events_.push({.kind = GameEvent::Kind::LevelChanged, .value = index, .flag = viaStairs, .player = player.id()});
    if (player.reachLevel(index)) {
        events_.push({.kind = GameEvent::Kind::LevelReached, .value = index, .player = player.id()});
    }
    events_.notify(world_.level(index).name());
}

// ---------------------------------------------------------------------------------------------------------------
// Light and the state hash

std::vector<Light> Simulation::lights(int level) const {
    std::vector<Light> lights = world_.level(level).furniture.lights();
    const TileMap& map = world_.level(level).map;
    for (const Player* player : playersOn(level)) {
        const Vec2 p = player->center();
        lights.push_back({p.x - 1.0f, p.y - 4.0f, player->lightRadius()});
        const int px = collision::tileIndex(p.x);
        const int py = collision::tileIndex(p.y);
        for (int ty = py - kLightScanTiles; ty <= py + kLightScanTiles; ++ty) {
            for (int tx = px - kLightScanTiles; tx <= px + kLightScanTiles; ++tx) {
                if (!map.inBounds(tx, ty)) continue;
                const Tile tile = map.tileAt(tx, ty);
                // TorchTile: 5, LavaTile: 6 (x 8 px), centred on the tile.
                const int radius = tile == Tile::Torch ? 5 : tile == Tile::Lava ? 6 : 0;
                if (radius == 0) continue;
                lights.push_back({static_cast<float>(tx * TileMap::kTileSize + 8),
                                  static_cast<float>(ty * TileMap::kTileSize + 8), static_cast<float>(radius * 8)});
            }
        }
    }
    return lights;
}

std::uint64_t Simulation::stateHash() const {
    Hasher h;
    h.value(tick_);
    h.value(dayNight_.tick());
    for (const auto& player : players_) {
        const Rect p = player->bounds();
        h.value(player->id());
        h.value(player->level());
        h.value(p.x);
        h.value(p.y);
        h.value(player->health());
        h.value(player->energy());
        h.value(player->hunger());
        h.value(player->armorPoints());
        for (const auto& stack : player->inventory().stacks()) {
            h.value(static_cast<int>(stack.type));
            h.value(stack.count);
            h.value(stack.durability);
        }
    }
    for (int i = 0; i < World::kLevelCount; ++i) {
        const Level& level = world_.level(i);
        const auto& tiles = level.map.tiles();
        h.bytes(tiles.data(), tiles.size());
        const auto& data = level.map.data();
        h.bytes(data.data(), data.size());
        for (const auto& mob : level.mobs.all()) {
            h.value(static_cast<int>(mob->kind()));
            h.value(mob->position().x);
            h.value(mob->position().y);
            h.value(mob->health());
        }
        for (const auto& item : level.drops.items()) {
            h.value(static_cast<int>(item.type));
            h.value(item.motion.x);
            h.value(item.motion.y);
        }
        for (const auto& piece : level.furniture.all()) {
            h.value(static_cast<int>(piece.type));
            h.value(piece.x);
            h.value(piece.y);
        }
    }
    return h.result();
}
