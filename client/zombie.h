#pragma once

#include "texture.h"

#include <SDL3/SDL.h>

#include <string>
#include <vector>

class Camera;
class Effects;
class Player;
class TileMap;

// One zombie (Minicraft's Zombie / EnemyMob / MobAi, level 1 on normal difficulty). Runs in 60 Hz ticks.
class Zombie {
public:
    static constexpr int kMaxHealth = 10;

    Zombie(float x, float y);

    // One tick of AI and movement. `blocked(box)` says whether another zombie occupies a box.
    template <typename Blocked>
    void tick(const TileMap& map, Player& player, Effects& effects, Blocked blocked);

    // Punched for `damage`, pushed along (directionX, directionY). Ignored during the short hurt cooldown.
    void hurt(int damage, int directionX, int directionY, Effects& effects);

    void draw(SDL_Renderer* renderer, const Camera& camera, SDL_Texture* sprite, SDL_Texture* flash) const;

    bool isDead() const { return health_ <= 0; }
    // Same proportions as the player: an 8x6 box at the feet; the centre sits at (8, 11) inside the sprite.
    SDL_FRect hitbox() const { return {x_ + 4.0f, y_ + 8.0f, 8.0f, 6.0f}; }
    SDL_FPoint center() const { return {x_ + 8.0f, y_ + 11.0f}; }
    int health() const { return health_; }

private:
    enum class Direction { Down, Up, Left, Right };

    // Tries to move one axis by `delta` px; returns false if blocked. Touching the player while trying hurts them.
    template <typename Blocked>
    bool move(float dx, float dy, const TileMap& map, Player& player, Effects& effects, Blocked blocked,
              bool changeDirection);
    void randomizeWalk();

    float x_;
    float y_;
    int health_ = kMaxHealth;
    Direction direction_ = Direction::Down;
    int walkDistance_ = 0;
    int moveX_ = 0;  // current walk direction (-1, 0, 1), Minicraft's xmov / ymov
    int moveY_ = 0;
    int randomWalkTime_ = 0;  // ticks left of a random walk, during which the zombie ignores the player
    int ticks_ = 0;
    int hurtTime_ = 0;
    int knockbackX_ = 0;
    int knockbackY_ = 0;
};

// All zombies in the world: spawning around the player, despawning far away, updating and drawing.
class Zombies {
public:
    static constexpr int kMaxAlive = 8;

    bool load(SDL_Renderer* renderer, const std::string& spritePath);

    void update(float dt, const TileMap& map, Player& player, Effects& effects);
    // Draws the zombies standing behind (`behind` = true: higher on screen than `playerY`) or in front of the player,
    // so they overlap the player sprite in the right order.
    void draw(SDL_Renderer* renderer, const Camera& camera, float playerY, bool behind) const;

    // A punch: hurts every zombie whose hitbox overlaps `attackBox`. Returns true if one was hit.
    bool punch(const SDL_FRect& attackBox, int damage, SDL_Point direction, Effects& effects);

    // Places a zombie on open ground near (x, y) (world pixels), for testing. Returns false if no spot was found.
    bool spawnNear(const TileMap& map, float x, float y, int minTiles, int maxTiles);

    void clear() { zombies_.clear(); }
    const std::vector<Zombie>& all() const { return zombies_; }
    bool spawningEnabled = true;

private:
    void tick(const TileMap& map, Player& player, Effects& effects);

    TexturePtr sprite_;
    TexturePtr flash_;
    std::vector<Zombie> zombies_;
    float tickAccumulator_ = 0.0f;
    int spawnTimer_ = 0;
};
