#pragma once

#include "geometry.h"

#include <vector>

class Events;
class Mobs;
class Player;
class Random;
class TileMap;

// Things flying through a level: arrows (Minicraft+'s Arrow, shot by skeletons and by the player's bows) and the
// Air Wizard's sparks (Spark).
class Projectiles {
public:
    struct Arrow {
        float x;
        float y;
        Point direction;
        int damage;
        int speed;  // px per tick
        bool fromPlayer;
    };
    struct Spark {
        float x;
        float y;
        float vx;
        float vy;
        int age;
        int lifetime;
    };

    // An arrow flying along `direction` from (x, y). `damage` is the bow's or skeleton's level; `fromPlayer` arrows
    // hit mobs, the others hit the player.
    void shootArrow(float x, float y, Point direction, int damage, bool fromPlayer);
    // A spark drifting at (vx, vy) px per tick for about 6 seconds.
    void addSpark(float x, float y, float vx, float vy, Random& rng);

    // One 60 Hz tick: move everything, hurt what it hits, and drop arrows that hit a wall or left the map.
    void tick(const TileMap& map, Player& player, Mobs& mobs, Events& events, Random& rng);
    void clear();
    std::size_t size() const { return arrows_.size() + sparks_.size(); }
    const std::vector<Arrow>& arrows() const { return arrows_; }
    const std::vector<Spark>& sparks() const { return sparks_; }

private:
    std::vector<Arrow> arrows_;
    std::vector<Spark> sparks_;
};
