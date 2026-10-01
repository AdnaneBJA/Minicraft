#pragma once

#include <SDL3/SDL.h>

#include <functional>

class Camera;
class DroppedItems;
class Effects;
class Player;
class TileMap;

enum class MobKind { Zombie, Cow, Pig, Sheep };
constexpr int kMobKinds = 4;

const char* mobName(MobKind kind);

// A mob (Minicraft's Mob / MobAi): walks in 60 Hz ticks with tile and mob collision, gets hurt and knocked back,
// flashes white while hurt, and is drawn from a sprite strip laid out like the player's (down, up, right 1, right 2).
// Subclasses decide where to walk (think) and what happens when they bump into the player.
class Mob {
public:
    // Whether a box is taken by another mob (`self` is the one moving) or by furniture.
    using Blocked = std::function<bool(const SDL_FRect& box, const Mob* self)>;
    struct World {
        const TileMap& map;
        Player& player;
        Effects& effects;
        const Blocked& blocked;
    };

    virtual ~Mob() = default;

    void tick(const World& world);
    // Punched for `damage`, pushed along (directionX, directionY). Ignored during the short hurt cooldown.
    void hurt(int damage, int directionX, int directionY, Effects& effects);
    void draw(SDL_Renderer* renderer, const Camera& camera, SDL_Texture* sprite, SDL_Texture* flash) const;
    // Drops this mob's loot where it died.
    virtual void dropLoot(DroppedItems& drops) const = 0;

    MobKind kind() const { return kind_; }
    bool isDead() const { return health_ <= 0; }
    int health() const { return health_; }
    // Same proportions as the player: an 8x6 box at the feet; the centre sits at (8, 11) inside the sprite.
    SDL_FRect hitbox() const { return {x_ + 4.0f, y_ + 8.0f, 8.0f, 6.0f}; }
    SDL_FPoint center() const { return {x_ + 8.0f, y_ + 11.0f}; }

protected:
    // `ticksPerStep`: walks 1 px every that many ticks. A random walk lasts `randomWalkTicks` and starts by chance
    // 1 in `randomWalkChance` ticks (Minicraft's rwTime / rwChance).
    Mob(MobKind kind, float x, float y, int maxHealth, int ticksPerStep, int randomWalkTicks, int randomWalkChance);

    // Called every tick after walking: decide the walk direction (moveX_ / moveY_).
    virtual void think(const World& world) = 0;
    // Starts a random walk (MobAi.randomizeWalkDir).
    virtual void randomizeWalk() = 0;
    // Called when the mob tries to walk into the player.
    virtual void touchPlayer(Player& player, Effects& effects);

    int moveX_ = 0;  // current walk direction (-1, 0, 1), Minicraft's xmov / ymov
    int moveY_ = 0;
    int randomWalkTime_ = 0;  // ticks left of a random walk
    const int randomWalkTicks_;
    const int randomWalkChance_;

private:
    enum class Direction { Down, Up, Left, Right };

    // Tries to move by (dx, dy), each axis separately; returns false if it couldn't move at all.
    bool move(float dx, float dy, const World& world, bool changeDirection);

    MobKind kind_;
    float x_;
    float y_;
    int health_;
    const int ticksPerStep_;
    Direction direction_ = Direction::Down;
    int walkDistance_ = 0;
    int ticks_ = 0;
    int hurtTime_ = 0;
    int knockbackX_ = 0;
    int knockbackY_ = 0;
};

// Minicraft's Zombie (EnemyMob, level 1 on normal difficulty): chases the player within 100 px, otherwise wanders,
// and hurts the player by bumping into them.
class Zombie : public Mob {
public:
    Zombie(float x, float y);
    void dropLoot(DroppedItems& drops) const override;

protected:
    void think(const World& world) override;
    void randomizeWalk() override;
    void touchPlayer(Player& player, Effects& effects) override;
};

// Minicraft's cow, pig and sheep (PassiveMob): wander about at random, often stopping.
class Animal : public Mob {
public:
    Animal(MobKind kind, float x, float y);
    void dropLoot(DroppedItems& drops) const override;

protected:
    void think(const World& world) override;
    void randomizeWalk() override;
};
