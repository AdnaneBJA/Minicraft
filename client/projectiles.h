#pragma once

#include <SDL3/SDL.h>

#include <vector>

class Audio;
class Camera;
class Effects;
class Mobs;
class Player;
class TileMap;

// Things flying through a level: arrows (Minicraft+'s Arrow, shot by skeletons and by the player's bows) and the
// Air Wizard's sparks (Spark). Drawn from projectiles.png: four 8x8 arrow frames (right, left, up, down) and the
// spark.
class Projectiles {
public:
    // An arrow flying along `direction` from (x, y). `damage` is the bow's or skeleton's level; `fromPlayer` arrows
    // hit mobs, the others hit the player.
    void shootArrow(float x, float y, SDL_Point direction, int damage, bool fromPlayer);
    // A spark drifting at (vx, vy) px per tick for about 6 seconds.
    void addSpark(float x, float y, float vx, float vy);

    // One 60 Hz tick: move everything, hurt what it hits, and drop arrows that hit a wall or left the map.
    void tick(const TileMap& map, Player& player, Mobs& mobs, Effects& effects, Audio& audio);
    void draw(SDL_Renderer* renderer, const Camera& camera, SDL_Texture* sheet) const;
    void clear();
    std::size_t size() const { return arrows_.size() + sparks_.size(); }

private:
    struct Arrow {
        float x;
        float y;
        SDL_Point direction;
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

    std::vector<Arrow> arrows_;
    std::vector<Spark> sparks_;
};
