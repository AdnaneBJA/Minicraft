#pragma once

#include "mob.h"
#include "texture.h"

#include <SDL3/SDL.h>

#include <array>
#include <memory>
#include <span>
#include <string>
#include <vector>

class Camera;
class DroppedItems;
class Effects;
class Player;
class TileMap;

// Every mob in the world: zombies and animals. Spawns them around the player, despawns them far away, and updates,
// draws and punches them.
class Mobs {
public:
    static constexpr int kMaxZombies = 8;
    static constexpr int kMaxAnimals = 10;

    // Loads <dir>zombie.png, cow.png, pig.png and sheep.png.
    bool load(SDL_Renderer* renderer, const std::string& spriteDirectory);

    // Zombies only spawn at night (Minicraft spawns surface enemies only at night) and the ones out of view despawn
    // during the day; animals spawn on grass at any time. Mobs that die drop their loot into `drops`. `obstacles`
    // (furniture) block mobs like other mobs do.
    void update(float dt, const TileMap& map, Player& player, Effects& effects, DroppedItems& drops, bool night,
                std::span<const SDL_FRect> obstacles);
    // Draws the mobs standing behind (`behind` = true: higher on screen than `playerY`) or in front of the player.
    void draw(SDL_Renderer* renderer, const Camera& camera, float playerY, bool behind) const;

    // A punch: hurts every mob whose hitbox overlaps `attackBox`. Returns true if one was hit.
    bool punch(const SDL_FRect& attackBox, int damage, SDL_Point direction, Effects& effects);

    // Places a mob of `kind` on suitable ground near (x, y) (world pixels), `minTiles`..`maxTiles` away, clear of
    // other mobs and `obstacles`. Returns false if no spot was found.
    bool spawnNear(MobKind kind, const TileMap& map, std::span<const SDL_FRect> obstacles, float x, float y,
                   int minTiles, int maxTiles);

    std::vector<SDL_FRect> hitboxes() const;
    int count(MobKind kind) const;
    int animalCount() const;
    const std::vector<std::unique_ptr<Mob>>& all() const { return mobs_; }
    void clear() { mobs_.clear(); }

    bool zombieSpawning = true;
    bool animalSpawning = true;

private:
    void tick(const TileMap& map, Player& player, Effects& effects, DroppedItems& drops, bool night,
              std::span<const SDL_FRect> obstacles);
    bool occupied(const SDL_FRect& box, const Mob* self, std::span<const SDL_FRect> obstacles) const;

    std::array<TexturePtr, kMobKinds> sprites_;
    std::array<TexturePtr, kMobKinds> flashes_;
    std::vector<std::unique_ptr<Mob>> mobs_;
    float tickAccumulator_ = 0.0f;
    int zombieSpawnTimer_ = 0;
    int animalSpawnTimer_ = 0;
};
