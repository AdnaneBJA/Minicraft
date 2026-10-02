#pragma once

#include "bounce.h"
#include "events.h"
#include "random.h"
#include "texture.h"

#include <SDL3/SDL.h>

#include <string>
#include <vector>

class Camera;
class Font;

// Short-lived visual effects drawn over the world: the "smash" X shown when a punch hits a tile, and the damage
// numbers that pop out of hit tiles or the hurt player (Minicraft's SmashParticle and TextParticle).
class Effects {
public:
    bool load(SDL_Renderer* renderer, const std::string& smashPath);

    // Shows the smash X over a tile for a moment.
    void addSmash(int tx, int ty);
    // A number that pops up from a world point, bounces, and disappears after a second: red for damage dealt,
    // magenta for health the player lost, grey for what their armour soaked up (Minicraft's colours).
    void addDamageNumber(int damage, float x, float y, NumberStyle style = NumberStyle::Damage);

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
        SDL_Color color;
    };

    TexturePtr smashTexture_;
    std::vector<Smash> smashes_;
    std::vector<DamageNumber> numbers_;
    float tickAccumulator_ = 0.0f;
    Random random_{0x5EED};  // the bounce of a number is cosmetic: the client's own random numbers
};
