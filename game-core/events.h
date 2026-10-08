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

// What hurt a player last: another player, a mob, or the world itself (lava, hunger). Kept for the stats: it's
// who gets the kill when the player dies.
struct DamageSource {
    enum class Kind { None, Player, Mob, Environment };
    Kind kind = Kind::None;
    int id = -1;  // the player's id (Player) or the MobKind (Mob)
};

// Something the simulation wants a player to see or hear. The simulation never plays sounds or draws; it records
// events during a tick and each client acts on the ones that concern its own player: everything that happens on
// the level they're on, and the personal events addressed to them.
struct GameEvent {
    enum class Kind {
        Sound,         // `sound`
        Smash,         // the smash X over tile (tileX, tileY)
        Number,        // `value` popping out of (x, y), drawn in `style`
        Notification,  // `text`
        LevelChanged,  // `player` is now on World level `value` (viaStairs: `flag`)
        Slept,         // `player` slept (until morning in single-player)
        PlayerDied,    // `player` died (`value`: seconds played)
        BossDefeated,  // the Air Wizard is dead: everyone has won (`player`: who landed the last hit, or -1)
        // Stats: what players did, for the game server to report. Clients ignore them.
        TileBroken,     // `player` broke or harvested a tile (`value`: the Tile)
        ItemCollected,  // `player` picked up `count` items (`value`: the ItemType)
        ItemCrafted,    // `player` crafted `count` items (`value`: the ItemType)
        MobKilled,      // a mob died (`value`: MobKind, `count`: its level), killed by `player` (-1: nobody)
        PlayerKilled,   // `player` died after `value` seconds alive, killed by `killer`
        LevelReached,   // `player` entered World level `value` for the first time this life
    };
    Kind kind;
    Sound sound = Sound::Select;
    NumberStyle style = NumberStyle::Damage;
    int tileX = 0;
    int tileY = 0;
    float x = 0.0f;
    float y = 0.0f;
    int value = 0;
    int count = 0;
    bool flag = false;
    DamageSource killer;
    std::string text;
    int level = 0;    // the World level it happened on
    int player = -1;  // the player it's for; -1 = everyone on that level
};

// True for the events that feed the stats (and the boss defeat, which is one too).
inline bool isStatEvent(GameEvent::Kind kind) {
    switch (kind) {
        case GameEvent::Kind::TileBroken:
        case GameEvent::Kind::ItemCollected:
        case GameEvent::Kind::ItemCrafted:
        case GameEvent::Kind::MobKilled:
        case GameEvent::Kind::PlayerKilled:
        case GameEvent::Kind::LevelReached:
        case GameEvent::Kind::BossDefeated: return true;
        default: return false;
    }
}

// The events recorded since the client last took them. While the simulation works on one level (or one player's
// actions) it sets the context, and every event recorded meanwhile is stamped with it.
class Events {
public:
    void setContext(int level, int player) {
        level_ = level;
        player_ = player;
    }

    void sound(Sound sound) { push({.kind = GameEvent::Kind::Sound, .sound = sound}); }
    void smash(int tileX, int tileY) { push({.kind = GameEvent::Kind::Smash, .tileX = tileX, .tileY = tileY}); }
    void number(int value, float x, float y, NumberStyle style = NumberStyle::Damage) {
        push({.kind = GameEvent::Kind::Number, .style = style, .x = x, .y = y, .value = value});
    }
    // A note for the player in the context (or everyone on the level, if none).
    void notify(std::string text) {
        push({.kind = GameEvent::Kind::Notification, .text = std::move(text), .player = player_});
    }
    void push(GameEvent event) {
        event.level = level_;
        events_.push_back(std::move(event));
    }

    // Hands over everything recorded so far and starts a new list.
    std::vector<GameEvent> take() { return std::exchange(events_, {}); }
    const std::vector<GameEvent>& pending() const { return events_; }

private:
    std::vector<GameEvent> events_;
    int level_ = 0;
    int player_ = -1;
};
