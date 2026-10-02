#pragma once

#include "day_night.h"
#include "mob.h"
#include "texture.h"

#include <SDL3/SDL.h>

#include <array>
#include <memory>
#include <span>
#include <string>
#include <vector>

class Audio;
class Camera;
class DroppedItems;
class Effects;
class Player;
class Projectiles;
class TileMap;

// The mob sprite sheets (and their white hurt-flash copies), loaded once and shared by every level.
class MobSprites {
public:
    // Loads <dir>zombie.png, cow.png, pig.png, sheep.png, skeleton.png, slime.png, creeper.png, snake.png and
    // air_wizard.png.
    bool load(SDL_Renderer* renderer, const std::string& spriteDirectory);
    SDL_Texture* sprite(MobKind kind) const { return sprites_[static_cast<std::size_t>(kind)].get(); }
    SDL_Texture* flash(MobKind kind) const { return flashes_[static_cast<std::size_t>(kind)].get(); }

private:
    std::array<TexturePtr, kMobKinds> sprites_;
    std::array<TexturePtr, kMobKinds> flashes_;
};

// Every mob on one level. Spawns them around the player (enemies in the dark, animals on surface grass), despawns
// them far away, and updates, draws and hits them.
class Mobs {
public:
    static constexpr int kMaxEnemies = 10;
    static constexpr int kMaxAnimals = 10;

    // What the mobs act on during an update.
    struct Context {
        TileMap& map;
        Player& player;
        Effects& effects;
        DroppedItems& drops;
        Projectiles& projectiles;
        Audio& audio;
        std::span<const SDL_FRect> obstacles;  // furniture
    };
    // Where and what may spawn (Level.trySpawn).
    struct SpawnRules {
        int depth = 0;       // 1 = sky, 0 = surface, -1..-3 = caves
        bool night = false;  // surface enemies only spawn at night
        std::span<const Lighting::Light> lights;  // no enemy spawns in the light
    };

    void update(float dt, const Context& context, const SpawnRules& rules);
    // Draws the mobs standing behind (`behind` = true: higher on screen than `playerY`) or in front of the player.
    void draw(SDL_Renderer* renderer, const Camera& camera, const MobSprites& sprites, float playerY,
              bool behind) const;

    // An attack: hurts every mob whose hitbox overlaps `box`. Returns true if one was hit.
    bool hit(const SDL_FRect& box, int damage, SDL_Point direction, Effects& effects, Audio& audio);

    // Places a level-`level` mob of `kind` on suitable ground near (x, y) (world pixels), `minTiles`..`maxTiles`
    // away, clear of other mobs and `obstacles`. Returns false if no spot was found.
    bool spawnNear(MobKind kind, int level, const TileMap& map, std::span<const SDL_FRect> obstacles, float x,
                   float y, int minTiles, int maxTiles);
    void add(std::unique_ptr<Mob> mob) { mobs_.push_back(std::move(mob)); }

    std::vector<SDL_FRect> hitboxes() const;
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
    void tick(const Context& context, const SpawnRules& rules);
    void trySpawn(const Context& context, const SpawnRules& rules);
    // A creeper's blast (Creeper.tick): hurts everything near it and turns the tiles within `radius` into holes.
    void explode(const Creeper& creeper, const Context& context);
    bool occupied(const SDL_FRect& box, const Mob* self, std::span<const SDL_FRect> obstacles) const;
    // Spawns a mob on tile (tx, ty) if it can stand there and nothing is in the way.
    bool spawnAt(MobKind kind, int level, const TileMap& map, std::span<const SDL_FRect> obstacles, int tx, int ty);

    std::vector<std::unique_ptr<Mob>> mobs_;
    float tickAccumulator_ = 0.0f;
    int enemySpawnTimer_ = 0;
    int animalSpawnTimer_ = 0;
    bool bossDefeated_ = false;
};
