#pragma once

#include <string>
#include <utility>
#include <vector>

// Minicraft's sound effects, by ID (the client maps them to assets/audio/*.wav).
enum class Sound { BossDeath, Confirm, Craft, Death, Explode, Fuse, MonsterHurt, Pickup, PlayerHurt, Select };
constexpr int kSoundCount = static_cast<int>(Sound::Select) + 1;

// What a damage number shows: damage dealt (red), health the player lost (magenta), or what their armour soaked up
// (grey).
enum class NumberStyle { Damage, PlayerDamage, ArmorDamage };

// Something the simulation wants the player to see or hear. The simulation never plays sounds or draws; it records
// events during a tick and the client (or, later, the server sending them to clients) acts on them.
struct GameEvent {
    enum class Kind {
        Sound,         // `sound`
        Smash,         // the smash X over tile (tileX, tileY)
        Number,        // `value` popping out of (x, y), drawn in `style`
        Notification,  // `text`
        LevelChanged,  // the player is now on World level `value` (viaStairs: `flag`)
        Slept,         // slept until morning
        PlayerDied,
        BossDefeated,
    };
    Kind kind;
    Sound sound = Sound::Select;
    NumberStyle style = NumberStyle::Damage;
    int tileX = 0;
    int tileY = 0;
    float x = 0.0f;
    float y = 0.0f;
    int value = 0;
    bool flag = false;
    std::string text;
};

// The events recorded since the client last took them.
class Events {
public:
    void sound(Sound sound) { push({.kind = GameEvent::Kind::Sound, .sound = sound}); }
    void smash(int tileX, int tileY) { push({.kind = GameEvent::Kind::Smash, .tileX = tileX, .tileY = tileY}); }
    void number(int value, float x, float y, NumberStyle style = NumberStyle::Damage) {
        push({.kind = GameEvent::Kind::Number, .style = style, .x = x, .y = y, .value = value});
    }
    void notify(std::string text) { push({.kind = GameEvent::Kind::Notification, .text = std::move(text)}); }
    void push(GameEvent event) { events_.push_back(std::move(event)); }

    // Hands over everything recorded so far and starts a new list.
    std::vector<GameEvent> take() { return std::exchange(events_, {}); }
    const std::vector<GameEvent>& pending() const { return events_; }

private:
    std::vector<GameEvent> events_;
};
