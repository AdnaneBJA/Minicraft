#pragma once

// Minicraft's day cycle (Updater): one day is 64800 ticks (18 minutes at 60 Hz), split into four equal parts.
// The surface darkens through the evening, stays dark at night and brightens again in the morning.
class DayNight {
public:
    enum class Time { Morning, Day, Evening, Night };
    static constexpr int kDayLength = 64800;

    // Advances one 60 Hz tick.
    void step();

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
};

const char* timeName(DayNight::Time time);

// A light source (Minicraft's getLightRadius): the player, lanterns, torches, lava. Enemies never spawn in the
// light, and the client cuts the darkness away around it.
struct Light {
    float x;       // world pixels
    float y;
    float radius;  // world pixels
};
