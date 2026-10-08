#include "stats_observer.h"

#include "mob.h"

StatsObserver::StatsObserver(std::uint32_t seed) : seed_(seed) {
    sim_.startNewWorld(seed);
    sim_.singlePlayer = false;
}

std::string StatsObserver::nameOf(int playerId) const {
    const auto it = names_.find(playerId);
    return it == names_.end() ? std::string() : it->second;
}

std::vector<StatEvent> StatsObserver::apply(const TickInput& tick, std::int64_t nowMs) {
    sim_.tick(tick);
    std::vector<StatEvent> stats;
    for (const GameEvent& event : sim_.events().take()) {
        if (!isStatEvent(event.kind)) continue;
        StatEvent stat = describe(event);
        stat.id = std::to_string(seed_) + "-" + std::to_string(tick.tick) + "-" + std::to_string(stats.size());
        stat.at = nowMs;
        stats.push_back(std::move(stat));
    }
    return stats;
}

StatEvent StatsObserver::describe(const GameEvent& event) const {
    using Kind = GameEvent::Kind;
    StatEvent stat;
    stat.player = event.player >= 0 ? nameOf(event.player) : std::string();
    switch (event.kind) {
        case Kind::TileBroken:
            stat.type = "TileBroken";
            stat.subject = tileName(static_cast<Tile>(event.value));
            stat.count = 1;
            break;
        case Kind::ItemCollected:
        case Kind::ItemCrafted:
            stat.type = event.kind == Kind::ItemCollected ? "ItemCollected" : "ItemCrafted";
            stat.subject = itemName(static_cast<ItemType>(event.value));
            stat.icon = event.value;
            stat.count = event.count;
            break;
        case Kind::MobKilled:
            stat.type = "MobKilled";
            stat.subject = mobName(static_cast<MobKind>(event.value));
            stat.count = event.count;
            break;
        case Kind::PlayerKilled:
            stat.type = "PlayerKilled";
            stat.count = event.value;
            switch (event.killer.kind) {
                case DamageSource::Kind::Player:
                    stat.killerKind = "player";
                    stat.subject = nameOf(event.killer.id);
                    break;
                case DamageSource::Kind::Mob:
                    stat.killerKind = "mob";
                    stat.subject = mobName(static_cast<MobKind>(event.killer.id));
                    break;
                case DamageSource::Kind::Environment:
                case DamageSource::Kind::None: stat.killerKind = "environment"; break;
            }
            break;
        case Kind::LevelReached:
            stat.type = "LevelReached";
            stat.subject = sim_.world().level(event.value).name();
            stat.count = event.value;
            break;
        case Kind::BossDefeated:
            stat.type = "BossDefeated";
            stat.subject = "Air Wizard";
            stat.count = 1;
            break;
        default: break;
    }
    return stat;
}
