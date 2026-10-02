#include "simulation.h"

#include "collision.h"
#include "player_actions.h"
#include "recipe.h"

#include <algorithm>
#include <bit>
#include <cstring>

namespace {

// Holding Space works like Minicraft: the press punches once, and only once the key has been held for a moment
// (Minicraft waits for the OS key repeat to make the key "sticky") does it unload rapid punches until energy runs
// out.
constexpr int kPunchHoldDelayTicks = 30;
constexpr int kRapidPunchTicks = 3;  // 20 punches/s
constexpr int kLightScanTiles = 24;  // torches and lava this far from the player give light

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

Vec2 Simulation::surfaceSpawn() const { return world_.level(World::kSurfaceIndex).map.findSpawnPoint(); }

void Simulation::resetSession(std::uint64_t rngSeed) {
    rng_.reseed(rngSeed);
    events_.take();
    inventory_.clear();
    player_.setHeldItem(std::nullopt);
    player_.refillStats();
    player_.removeArmor();
    dayNight_ = DayNight{};
    spawnLevel_ = -1;
    spawnPoint_ = {};
    ticksPlayed_ = 0;
    punchRepeatTicks_ = 0;
    onStairs_ = false;
    dead_ = false;
}

void Simulation::startNewWorld(std::uint32_t seed) {
    resetSession(seed);
    world_.generate(seed, rng_);
    const Vec2 spawn = surfaceSpawn();
    player_.setPosition(spawn.x, spawn.y);
    inventory_.add(ItemType::PowerGlove);  // like the original Minicraft, the player starts with the glove
}

void Simulation::regenerate(std::uint32_t seed) {
    rng_.reseed(seed);
    world_.generate(seed, rng_);
    const Vec2 spawn = surfaceSpawn();
    player_.setPosition(spawn.x, spawn.y);
    onStairs_ = false;
}

void Simulation::tick(const PlayerInput& input) {
    if (dead_) return;
    ++ticksPlayed_;
    Level& here = level();
    const std::vector<Rect> obstacles = here.furniture.hitboxes();
    player_.tick(input, here.map, obstacles, events_);

    // Space: a fresh press always acts; held, it repeats after a short delay.
    if (input.attackPressed) {
        useOrPunch();
        punchRepeatTicks_ = kPunchHoldDelayTicks;
    } else if (input.attack && --punchRepeatTicks_ <= 0) {
        useOrPunch();
        punchRepeatTicks_ += kRapidPunchTicks;
    }

    // Stepping onto stairs takes them (Player.tick's onStairDelay): arriving on the other end doesn't send the
    // player straight back, since they have to step off the stairs first.
    const Vec2 c = player_.center();
    const int tx = collision::tileIndex(c.x);
    const int ty = collision::tileIndex(c.y);
    const Tile under = here.map.inBounds(tx, ty) ? here.map.tileAt(tx, ty) : Tile::Rock;
    const bool stairs = under == Tile::StairsDown || under == Tile::StairsUp;
    if (stairs && !onStairs_) {
        const int next = world_.currentIndex() + (under == Tile::StairsDown ? 1 : -1);
        if (next >= 0 && next < World::kLevelCount) {
            changeLevel(next, true);
            return;
        }
    }
    onStairs_ = stairs;

    dayNight_.step();
    const std::vector<Light> lightSources = lights();
    const Mobs::Context context{here.map, player_, events_, rng_, here.drops, here.projectiles, obstacles};
    const Mobs::SpawnRules rules{here.depth(), dayNight_.time() == DayNight::Time::Night, lightSources};
    here.mobs.tick(context, rules);

    // Level.tick: arrows and sparks, and Minicraft's random tile ticks (crops grow, grass spreads, liquids flow
    // into holes).
    here.projectiles.tick(here.map, player_, here.mobs, events_, rng_);
    here.map.tickRandomTiles(World::kSize / 2, World::kSize / 2, World::kSize / 2, World::kSize * World::kSize / 50,
                             rng_);

    if (here.drops.tick(here.map, player_.hitbox(), inventory_) > 0) events_.sound(Sound::Pickup);

    if (here.mobs.takeBossDefeated()) {
        world_.airWizardBeaten = true;
        events_.push({.kind = GameEvent::Kind::BossDefeated});
    }
    if (player_.isDead()) die();
}

void Simulation::useOrPunch() {
    PlayerActions(level(), player_, inventory_, events_, rng_).useOrPunch();
}

void Simulation::addOrDrop(const Inventory::Stack& stack) {
    if (const int leftover = inventory_.add(stack); leftover > 0) {
        const Vec2 middle = player_.center();
        level().drops.spawn(stack.type, leftover, middle.x, middle.y, rng_, stack.durability);
    }
}

void Simulation::giveItems(ItemType type, int count) {
    if (const int leftover = inventory_.add(type, count); leftover > 0) {
        const Vec2 middle = player_.center();
        level().drops.spawn(type, leftover, middle.x, middle.y, rng_);
    }
}

bool Simulation::craft(const Recipe& recipe) {
    const int leftover = recipe.craft(inventory_);
    if (leftover < 0) return false;
    events_.sound(Sound::Craft);
    if (leftover > 0) {
        const Vec2 middle = player_.center();
        level().drops.spawn(recipe.product(), leftover, middle.x, middle.y, rng_);
    }
    return true;
}

std::optional<int> Simulation::stowHeldItem() {
    const auto held = player_.heldItem();
    if (!held) return std::nullopt;
    player_.setHeldItem(std::nullopt);
    if (const int leftover = inventory_.add(*held); leftover > 0) {
        const Vec2 middle = player_.center();
        level().drops.spawn(held->type, leftover, middle.x, middle.y, rng_, held->durability);
        return std::nullopt;
    }
    // Stackable items merge into the stack of their type; tools go into a new last slot.
    const auto& stacks = inventory_.stacks();
    if (!isStackable(held->type)) return static_cast<int>(stacks.size()) - 1;
    const auto it = std::find_if(stacks.begin(), stacks.end(), [&](const auto& s) { return s.type == held->type; });
    return static_cast<int>(it - stacks.begin());
}

void Simulation::holdSlot(int index) {
    if (index < 0 || index >= static_cast<int>(inventory_.stacks().size())) return;
    player_.setHeldItem(inventory_.take(index));
}

Simulation::FurnitureUse Simulation::useFurniture() {
    const Point target = player_.interactionTile();
    Furniture::Piece* piece = level().furniture.at(target.x, target.y);
    if (!piece) return {};
    if (piece->isContainer()) {
        stowHeldItem();
        return {.kind = FurnitureUse::Kind::Chest, .tile = target, .deathChest = piece->deathChest};
    }
    if (piece->type == ItemType::Bed) {
        const DayNight::Time time = dayNight_.time();
        if (time != DayNight::Time::Evening && time != DayNight::Time::Night) {
            events_.notify("Can't sleep! Wait for the evening");
            return {.kind = FurnitureUse::Kind::CantSleep};
        }
        sleep(*piece);
        return {.kind = FurnitureUse::Kind::Slept};
    }
    if (Recipe::stationRecipes(piece->type).empty()) return {};  // a lantern: nothing to use
    stowHeldItem();
    return {.kind = FurnitureUse::Kind::Station, .station = piece->type};
}

void Simulation::sleep(const Furniture::Piece& bed) {
    // Bed.use: sleeping skips to the morning and makes the bed the respawn point.
    spawnLevel_ = world_.currentIndex();
    // Wake up just below the bed (or on it, if that tile is blocked).
    const int tx = collision::tileIndex(bed.x);
    const int ty = collision::tileIndex(bed.y);
    const bool below = !level().map.isSolidAt(tx, ty + 1) && !level().furniture.at(tx, ty + 1);
    spawnPoint_ = {static_cast<float>(tx * TileMap::kTileSize),
                   static_cast<float>((below ? ty + 1 : ty) * TileMap::kTileSize) - 3.0f};
    dayNight_.setTime(DayNight::Time::Morning);
    player_.setPosition(spawnPoint_.x, spawnPoint_.y);
    level().mobs.clearEnemies();  // the night's monsters are gone by morning
    events_.push({.kind = GameEvent::Kind::Slept});
    events_.notify("You slept until morning");
}

bool Simulation::transfer(Point chestTile, bool fromChest, int index) {
    Furniture::Piece* chest = level().furniture.at(chestTile.x, chestTile.y);
    if (!chest || !chest->isContainer()) return false;
    Inventory& from = fromChest ? chest->contents : inventory_;
    Inventory& to = fromChest ? inventory_ : chest->contents;
    if (index < 0 || index >= static_cast<int>(from.stacks().size())) return true;
    const Inventory::Stack stack = from.take(index);
    if (const int leftover = to.add(stack); leftover > 0) {
        Inventory::Stack back = stack;
        back.count = leftover;
        from.add(back);
    }
    events_.sound(Sound::Pickup);
    // An emptied death chest disappears (DeathChest).
    if (chest->deathChest && chest->contents.empty()) {
        level().furniture.remove(chest);
        return false;
    }
    return true;
}

void Simulation::die() {
    // Player.die: everything the player carried goes into a death chest where they fell.
    Inventory carried = inventory_;
    if (const auto& held = player_.heldItem()) carried.add(*held);
    if (const auto& armor = player_.armor()) carried.add(*armor, 1);
    if (!carried.empty()) {
        const Vec2 c = player_.center();
        level().furniture.addDeathChest(c.x, c.y, std::move(carried));
    }
    inventory_.clear();
    player_.setHeldItem(std::nullopt);
    player_.removeArmor();
    dead_ = true;
    events_.sound(Sound::Death);
    events_.push({.kind = GameEvent::Kind::PlayerDied, .value = secondsPlayed()});
}

void Simulation::respawn() {
    int index = World::kSurfaceIndex;
    Vec2 spawn = surfaceSpawn();
    if (spawnLevel_ >= 0) {
        index = spawnLevel_;
        spawn = spawnPoint_;
    }
    changeLevel(index, false);
    player_.setPosition(spawn.x, spawn.y);
    player_.refillStats();
    onStairs_ = false;
    dead_ = false;
}

void Simulation::changeLevel(int index, bool viaStairs) {
    if (index == world_.currentIndex() || index < 0 || index >= World::kLevelCount) return;
    world_.setCurrent(index);
    if (level().isSky()) world_.spawnBoss(rng_);
    onStairs_ = viaStairs;
    events_.push({.kind = GameEvent::Kind::LevelChanged, .value = index, .flag = viaStairs});
    events_.notify(level().name());
}

std::vector<Light> Simulation::lights() const {
    std::vector<Light> lights = level().furniture.lights();
    const Vec2 p = player_.center();
    lights.push_back({p.x - 1.0f, p.y - 4.0f, player_.lightRadius()});
    const TileMap& map = level().map;
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
    return lights;
}

std::uint64_t Simulation::stateHash() const {
    Hasher h;
    const Rect p = player_.bounds();
    h.value(p.x);
    h.value(p.y);
    h.value(player_.health());
    h.value(player_.energy());
    h.value(player_.hunger());
    h.value(player_.armorPoints());
    for (const auto& stack : inventory_.stacks()) {
        h.value(static_cast<int>(stack.type));
        h.value(stack.count);
        h.value(stack.durability);
    }
    h.value(dayNight_.tick());
    h.value(world_.currentIndex());
    h.value(ticksPlayed_);
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
