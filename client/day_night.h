#pragma once

#include "texture.h"

#include <SDL3/SDL.h>

#include <vector>

class Camera;

// Minicraft's day cycle (Updater): one day is 64800 ticks (18 minutes at 60 Hz), split into four equal parts.
// The surface darkens through the evening, stays dark at night and brightens again in the morning.
class DayNight {
public:
    enum class Time { Morning, Day, Evening, Night };
    static constexpr int kDayLength = 64800;

    void update(float dt);

    Time time() const;
    int tick() const { return tick_; }
    // Jumps to the start of a part of the day (Minicraft's F3+T shortcuts).
    void setTime(Time time);
    bool pastDay1() const { return pastDay1_; }
    // Restores a saved time of day.
    void restore(int tick, bool pastDay1);

    // How dark the surface is, 0 (day) to 0.8 (night): Minicraft's darkFactor / 160.
    float darkness() const;

private:
    int tick_ = 0;
    bool pastDay1_ = false;  // the very first morning is bright, like Minicraft
    float tickAccumulator_ = 0.0f;
};

const char* timeName(DayNight::Time time);

// The night overlay: darkness over the whole view except dithered circles of light (Minicraft's LightOverlay).
class Lighting {
public:
    struct Light {
        float x;       // world pixels
        float y;
        float radius;  // world pixels
    };

    // Draws in world-pixel scale (call with the world render scale active).
    void draw(SDL_Renderer* renderer, const Camera& camera, float darkness, const std::vector<Light>& lights);

private:
    TexturePtr texture_;
    int width_ = 0;
    int height_ = 0;
    std::vector<Uint8> pixels_;
};
