#pragma once

#include "geometry.h"

#include <functional>
#include <span>

class DroppedItems;
class Events;
class Player;
class Projectiles;
class Random;
class TileMap;

enum class MobKind { Zombie, Cow, Pig, Sheep, Skeleton, Slime, Creeper, Snake, AirWizard };
constexpr int kMobKinds = 9;

const char* mobName(MobKind kind);
bool isEnemy(MobKind kind);

// A mob (Minicraft's Mob / MobAi): walks in 60 Hz ticks with tile and mob collision, gets hurt and knocked back,
// flashes white while hurt (the client draws it from a sprite sheet with one 16 px row per mob level and the frames
// laid // out like the player's (down, up, right 1, right 2). Subclasses decide where to walk (think) and what happens when
// they bump into the player.
class Mob {
public:
    enum class Direction { Down, Up, Left, Right };
    // Whether a box is taken by another mob (`self` is the one moving) or by furniture.
    using Blocked = std::function<bool(const Rect& box, const Mob* self)>;
    struct World {
        TileMap& map;
        std::span<Player* const> players;  // the living players on this level
        Events& events;
        Random& rng;
        Projectiles& projectiles;
        const Blocked& blocked;

        // The player closest to `from`, or null when nobody is on the level.
        Player* nearestPlayer(Vec2 from) const;
    };

    virtual ~Mob() = default;

    void tick(const World& world);
    // Hit for `damage`, pushed along (directionX, directionY). Ignored during the short hurt cooldown.
    virtual void hurt(int damage, int directionX, int directionY, Events& events);
    // Drops this mob's loot where it died.
    virtual void dropLoot(DroppedItems& drops, Random& rng) const = 0;

    MobKind kind() const { return kind_; }
    int level() const { return level_; }
    bool isDead() const { return health_ <= 0; }
    int health() const { return health_; }
    int maxHealth() const { return maxHealth_; }
    void kill() { health_ = 0; }
    // Same proportions as the player: an 8x6 box at the feet; the centre sits at (8, 11) inside the sprite.
    Rect hitbox() const { return {x_ + 4.0f, y_ + 8.0f, 8.0f, 6.0f}; }
    Vec2 center() const { return {x_ + 8.0f, y_ + 11.0f}; }
    // Unit vector of the facing direction.
    Point facing() const;

    // What the client needs to draw it: the sprite's top-left, facing, walk animation and hurt flash.
    Vec2 position() const { return {x_, y_}; }
    Direction direction() const { return direction_; }
    int walkDistance() const { return walkDistance_; }
    bool isHurt() const { return hurtTime_ > 0; }
    int ticks() const { return ticks_; }

protected:
    // `ticksPerStep`: walks 1 px every that many ticks. A random walk lasts `randomWalkTicks` and starts by chance
    // 1 in `randomWalkChance` ticks (Minicraft's rwTime / rwChance). `level` (1-4) picks the sprite row.
    Mob(MobKind kind, float x, float y, int level, int maxHealth, int ticksPerStep, int randomWalkTicks,
        int randomWalkChance);

    // Called every tick after walking: decide the walk direction (moveX_ / moveY_).
    virtual void think(const World& world) = 0;
    // Starts a random walk (MobAi.randomizeWalkDir).
    virtual void randomizeWalk(Random& rng) = 0;
    // Called when the mob tries to walk into the player.
    virtual void touchPlayer(Player& player, Events& events);
    // Whether it can float over the open sky (InfiniteFallTile.mayPass: only the Air Wizard).
    virtual bool floatsOverSky() const { return false; }
    // Hurts the player for `damage`, knocking them away from this mob; true if it landed.
    bool hitPlayer(Player& player, int damage, Events& events) const;
    // Moves towards the player within `distance` px (EnemyMob.tick); otherwise maybe starts a random walk.
    void chasePlayer(const World& world, int distance);
    void teleport(float x, float y) {
        x_ = x;
        y_ = y;
    }


    int moveX_ = 0;  // current walk direction (-1, 0, 1), Minicraft's xmov / ymov
    int moveY_ = 0;
    int randomWalkTime_ = 0;  // ticks left of a random walk
    const int randomWalkTicks_;
    const int randomWalkChance_;
    int ticks_ = 0;
    int hurtTime_ = 0;
    int walkDistance_ = 0;
    Direction direction_ = Direction::Down;
    int health_;

private:
    // Tries to move by (dx, dy), each axis separately; returns false if it couldn't move at all.
    bool move(float dx, float dy, const World& world, bool changeDirection);

    MobKind kind_;
    float x_;
    float y_;
    int level_;
    int maxHealth_;
    const int ticksPerStep_;
    int knockbackX_ = 0;
    int knockbackY_ = 0;
};

// Minicraft's EnemyMob base for the hostile mobs: health scales with the level squared (normal difficulty doubles
// it), they chase the player within `detectDistance` and hurt them by bumping into them for `level` damage.
class Enemy : public Mob {
public:
    void dropLoot(DroppedItems& drops, Random& rng) const override;

protected:
    // `scaleHealth`: health = baseHealth * level^2 * 2 (EnemyMob's isFactor); otherwise just baseHealth.
    Enemy(MobKind kind, float x, float y, int level, int baseHealth, int detectDistance, int ticksPerStep,
          int randomWalkTicks, int randomWalkChance, Random& rng, bool scaleHealth = true);
    void think(const World& world) override;
    void randomizeWalk(Random& rng) override;
    void touchPlayer(Player& player, Events& events) override;
    // The kind's own drops; called by dropLoot.
    virtual void dropItems(DroppedItems& drops, Random& rng) const = 0;

    int detectDistance_;
};

// Zombie: chases the player, drops cloth (and rarely iron or a potato).
class Zombie : public Enemy {
public:
    Zombie(float x, float y, int level, Random& rng);

protected:
    void dropItems(DroppedItems& drops, Random& rng) const override;
};

// Skeleton: chases the player and shoots an arrow every 500 / (level + 5) ticks when within 100 px.
class Skeleton : public Enemy {
public:
    Skeleton(float x, float y, int level, Random& rng);

protected:
    void think(const World& world) override;
    void dropItems(DroppedItems& drops, Random& rng) const override;

private:
    int arrowDelay_;
    int arrowTimer_;
};

// Slime: hops about (a jump every so often, standing between jumps) and drops slime.
class Slime : public Enemy {
public:
    Slime(float x, float y, int level, Random& rng);
    // In the air (drawn with the jumping frame, 4 px higher).
    bool isJumping() const { return jumpTime_ > 0; }

protected:
    void think(const World& world) override;
    void randomizeWalk(Random& rng) override;
    void dropItems(DroppedItems& drops, Random& rng) const override;

private:
    int jumpTime_ = 0;  // > 0 while in the air; counts on below 0 as the rest between jumps
};

// Creeper: touching the player lights a 1 s fuse; if the player is still within 64 px when it burns down, it
// explodes, hurting everything around it and blasting the tiles within `level` tiles into holes.
class Creeper : public Enemy {
public:
    Creeper(float x, float y, int level, Random& rng);
    // The fuse burning: it flashes white every 6 ticks.
    bool isFlashing() const { return fuseLit_ && (fuseTime_ / 6) % 2 == 0; }
    // Set once the fuse burned down with the player in range: the Mobs manager runs the explosion.
    bool exploding() const { return exploding_; }
    int blastDamage() const { return 50 * level(); }

protected:
    void think(const World& world) override;
    void touchPlayer(Player& player, Events& events) override;
    void dropItems(DroppedItems& drops, Random& rng) const override;

private:
    int fuseTime_ = 0;
    bool fuseLit_ = false;
    bool exploding_ = false;
};

// Snake (deepest caves only): bites harder (level + 1) and drops scales for snake armour.
class Snake : public Enemy {
public:
    Snake(float x, float y, int level, Random& rng);

protected:
    void touchPlayer(Player& player, Events& events) override;
    void dropItems(DroppedItems& drops, Random& rng) const override;
};

// The Air Wizard, the boss of the sky (2000 health). It keeps its distance, teleports back when the player gets
// far away, and casts spirals of sparks; the spirals get faster as it weakens. Beating it wins the game.
class AirWizard : public Enemy {
public:
    static constexpr int kMaxHealth = 2000;

    AirWizard(float x, float y, Random& rng);
    // Winding up or casting a spiral (drawn in its angry colours).
    bool isCasting() const { return attackDelay_ > 0 || attackTime_ > 0; }
    void hurt(int damage, int directionX, int directionY, Events& events) override;

protected:
    void think(const World& world) override;
    void touchPlayer(Player& player, Events& events) override;
    bool floatsOverSky() const override { return true; }
    void dropItems(DroppedItems& drops, Random& rng) const override;

private:
    int attackDelay_ = 0;  // wind-up before a spiral (it spins on the spot)
    int attackTime_ = 0;   // ticks of sparks left in the current spiral
    int attackType_ = 0;   // 0-2: faster sparks at half and a tenth of its health
};

// Minicraft's cow, pig and sheep (PassiveMob): wander about at random, often stopping.
class Animal : public Mob {
public:
    Animal(MobKind kind, float x, float y);
    void dropLoot(DroppedItems& drops, Random& rng) const override;

protected:
    void think(const World& world) override;
    void randomizeWalk(Random& rng) override;
};
