#pragma once

#include "bounce.h"
#include "texture.h"

#include <SDL3/SDL.h>

#include <string>
#include <vector>

class Camera;
class Font;

// Short-lived visual effects drawn over the world: the "smash" X shown when a punch hits a tile, and the red
// damage number that pops out of it (Minicraft's SmashParticle and TextParticle).
class Effects {
public:
    bool load(SDL_Renderer* renderer, const std::string& smashPath);

    // Shows the smash X over a tile for a moment.
    void addSmash(int tx, int ty);
    // A red number that pops up from a world point, bounces, and fades out after a second.
    void addDamageNumber(int damage, float x, float y);

    void update(float dt);
    void draw(SDL_Renderer* renderer, const Camera& camera, const Font& font) const;
    void clear();

private:
    struct Smash {
        float x;
        float y;
        float timeLeft;
    };
    struct DamageNumber {
        std::string text;
        Bounce motion;
        int age;  // ticks
    };

    TexturePtr smashTexture_;
    std::vector<Smash> smashes_;
    std::vector<DamageNumber> numbers_;
    float tickAccumulator_ = 0.0f;
};
