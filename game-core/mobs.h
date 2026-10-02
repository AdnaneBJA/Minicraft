#pragma once

#include "day_night.h"
#include "geometry.h"
#include "mob.h"

#include <memory>
#include <span>
#include <vector>

class DroppedItems;
class Events;
class Player;
class Projectiles;
class Random;
class TileMap;

// Every mob on one level. Spawns them around the player (enemies in the dark, animals on surface grass), despawns
// them far away, and ticks and hits them.
class Mobs {
public:
    static constexpr int kMaxEnemies = 10;
    static constexpr int kMaxAnimals = 10;

    // What the mobs act on during an update.
    struct Context {
        TileMap& map;
        Player& player;
        Events& events;
        Random& rng;
        DroppedItems& drops;
        Projectiles& projectiles;
        std::span<const Rect> obstacles;  // furniture
    };
    // Where and what may spawn (Level.trySpawn).
    struct SpawnRules {
        int depth = 0;       // 1 = sky, 0 = surface, -1..-3 = caves
        bool night = false;  // surface enemies only spawn at night
        std::span<const Light> lights;  // no enemy spawns in the light
    };

    // One 60 Hz tick: every mob moves, then the dead drop their loot, far ones despawn and new ones may spawn.
    void tick(const Context& context, const SpawnRules& rules);

    // An attack: hurts every mob whose hitbox overlaps `box`. Returns true if one was hit.
    bool hit(const Rect& box, int damage, Point direction, Events& events);

    // Places a level-`level` mob of `kind` on suitable ground near (x, y) (world pixels), `minTiles`..`maxTiles`
    // away, clear of other mobs and `obstacles`. Returns false if no spot was found.
    bool spawnNear(MobKind kind, int level, const TileMap& map, std::span<const Rect> obstacles, float x,
                   float y, int minTiles, int maxTiles, Random& rng);
    void add(std::unique_ptr<Mob> mob) { mobs_.push_back(std::move(mob)); }

    std::vector<Rect> hitboxes() const;
    int count(MobKind kind) const;
    int enemyCount() const;
    int animalCount() const;
    // The Air Wizard, if it's on this level.
    const Mob* boss() const;
    // True once after the Air Wizard died (the game shows the win screen).
    bool takeBossDefeated();
    const std::vector<std::unique_ptr<Mob>>& all() const { return mobs_; }
    void clear() { mobs_.clear(); }
    // Removes every hostile mob (the night's monsters, gone after sleeping), keeping animals and the boss.
    void clearEnemies();

    bool enemySpawning = true;
    bool animalSpawning = true;

private:
    void trySpawn(const Context& context, const SpawnRules& rules);
    // A creeper's blast (Creeper.tick): hurts everything near it and turns the tiles within `radius` into holes.
    void explode(const Creeper& creeper, const Context& context);
    bool occupied(const Rect& box, const Mob* self, std::span<const Rect> obstacles) const;
    // Spawns a mob on tile (tx, ty) if it can stand there and nothing is in the way.
    bool spawnAt(MobKind kind, int level, const TileMap& map, std::span<const Rect> obstacles, int tx, int ty,
                 Random& rng);

    std::vector<std::unique_ptr<Mob>> mobs_;
    int enemySpawnTimer_ = 0;
    int animalSpawnTimer_ = 0;
    bool bossDefeated_ = false;
};
