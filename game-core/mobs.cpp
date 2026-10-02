#include "mobs.h"

#include "collision.h"
#include "dropped_items.h"
#include "events.h"
#include "player.h"
#include "projectiles.h"
#include "random.h"
#include "tile_map.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

constexpr int kEnemySpawnTicks = 60;    // try to spawn an enemy every second (at night, or anytime underground)
constexpr int kAnimalSpawnTicks = 120;  // and an animal every two seconds on the surface
constexpr int kSpawnMinTiles = 10;      // off screen on the surface
constexpr int kSpawnMaxTiles = 20;
constexpr int kCaveSpawnMinTiles = 5;   // underground, anywhere out of the light
constexpr int kCaveSpawnMaxTiles = 16;
constexpr float kDespawnDistance = 48.0f * static_cast<float>(TileMap::kTileSize);
constexpr float kDaytimeDespawnDistance = 12.0f * static_cast<float>(TileMap::kTileSize);  // surface, out of view

// Enemies spawn on any open ground except stairs, crops and doors (EnemyMob.checkStartPos); animals only on grass
// (PassiveMob.checkStartPos: grass or flowers).
bool canSpawnOn(MobKind kind, Tile tile) {
    if (isEnemy(kind)) {
        return !blocksMobs(tile) && tile != Tile::StairsDown && tile != Tile::StairsUp && tile != Tile::Wheat &&
               tile != Tile::Farmland && tile != Tile::WoodDoor && tile != Tile::StoneDoor;
    }
    return tile == Tile::Grass || tile == Tile::Flower;
}

std::unique_ptr<Mob> makeMob(MobKind kind, int level, float x, float y, Random& rng) {
    switch (kind) {
        case MobKind::Zombie: return std::make_unique<Zombie>(x, y, level, rng);
        case MobKind::Skeleton: return std::make_unique<Skeleton>(x, y, level, rng);
        case MobKind::Slime: return std::make_unique<Slime>(x, y, level, rng);
        case MobKind::Creeper: return std::make_unique<Creeper>(x, y, level, rng);
        case MobKind::Snake: return std::make_unique<Snake>(x, y, level, rng);
        case MobKind::AirWizard: return std::make_unique<AirWizard>(x, y, rng);
        default: return std::make_unique<Animal>(kind, x, y);
    }
}

// Level.isLight: inside the radius of any light source.
bool isLit(float x, float y, std::span<const Light> lights) {
    return std::any_of(lights.begin(), lights.end(), [&](const Light& light) {
        return std::hypot(light.x - x, light.y - y) < light.radius;
    });
}

}  // namespace

bool Mobs::occupied(const Rect& box, const Mob* self, std::span<const Rect> obstacles) const {
    for (const auto& other : mobs_) {
        if (other.get() == self) continue;
        const Rect otherBox = other->hitbox();
        if (intersects(box, otherBox)) return true;
    }
    return std::any_of(obstacles.begin(), obstacles.end(),
                       [&](const Rect& obstacle) { return intersects(box, obstacle); });
}

void Mobs::tick(const Context& context, const SpawnRules& rules) {
    const Mob::Blocked blocked = [&](const Rect& box, const Mob* self) {
        return occupied(box, self, context.obstacles);
    };
    const Mob::World world{context.map, context.player, context.events, context.rng, context.projectiles, blocked};
    for (std::size_t i = 0; i < mobs_.size(); ++i) {
        mobs_[i]->tick(world);
        if (mobs_[i]->kind() == MobKind::Creeper) {
            const auto& creeper = static_cast<const Creeper&>(*mobs_[i]);
            if (creeper.exploding() && !creeper.isDead()) {
                explode(creeper, context);
                mobs_[i]->kill();
            }
        }
    }

    // Dead mobs drop their loot; mobs far from the player despawn (surface enemies sooner during the day).
    const Vec2 p = context.player.center();
    std::erase_if(mobs_, [&](const std::unique_ptr<Mob>& mob) {
        if (mob->isDead()) {
            mob->dropLoot(context.drops, context.rng);
            if (mob->kind() == MobKind::AirWizard) {
                bossDefeated_ = true;
                context.events.sound(Sound::BossDeath);
            }
            return true;
        }
        if (mob->kind() == MobKind::AirWizard) return false;  // the boss never leaves
        const Vec2 c = mob->center();
        const float distance = std::hypot(c.x - p.x, c.y - p.y);
        if (distance > kDespawnDistance) return true;
        return rules.depth == 0 && isEnemy(mob->kind()) && !rules.night && distance > kDaytimeDespawnDistance;
    });

    trySpawn(context, rules);
}

void Mobs::trySpawn(const Context& context, const SpawnRules& rules) {
    const Vec2 p = context.player.center();
    // Level.trySpawn: enemies at night on the surface, and anytime in the caves and the sky.
    const bool enemiesNow = rules.depth != 0 || rules.night;
    if (enemySpawning && enemiesNow && ++enemySpawnTimer_ >= kEnemySpawnTicks) {
        enemySpawnTimer_ = 0;
        if (enemyCount() - count(MobKind::AirWizard) < kMaxEnemies) {
            // Mob level: 1 on the surface, the depth in the caves; the sky's are the toughest (the original's 4).
            const int level = rules.depth > 0 ? 4 : std::clamp(-rules.depth, 1, 4);
            const int roll = context.rng.nextInt(100);
            MobKind kind = MobKind::Creeper;
            if (roll <= 40) kind = MobKind::Slime;
            else if (roll <= 75) kind = MobKind::Zombie;
            else if (roll >= 85) kind = MobKind::Skeleton;
            if (rules.depth <= -3 && roll > 60 && roll <= 75) kind = MobKind::Snake;  // snakes in the deepest caves
            if (rules.depth > 0 && kind != MobKind::Slime) kind = MobKind::Zombie;    // the sky: slimes and zombies
            const bool cave = rules.depth < 0;
            const int minTiles = cave ? kCaveSpawnMinTiles : kSpawnMinTiles;
            const int maxTiles = cave ? kCaveSpawnMaxTiles : kSpawnMaxTiles;
            // Never in the light (a torch or lantern keeps an area safe).
            const int px = collision::tileIndex(p.x);
            const int py = collision::tileIndex(p.y);
            for (int attempt = 0; attempt < 10; ++attempt) {
                const int dx = context.rng.nextInt(2 * maxTiles + 1) - maxTiles;
                const int dy = context.rng.nextInt(2 * maxTiles + 1) - maxTiles;
                if (std::max(std::abs(dx), std::abs(dy)) < minTiles) continue;
                const float cx = static_cast<float>((px + dx) * TileMap::kTileSize + TileMap::kTileSize / 2);
                const float cy = static_cast<float>((py + dy) * TileMap::kTileSize + TileMap::kTileSize / 2);
                if (isLit(cx, cy, rules.lights)) continue;
                if (spawnAt(kind, level, context.map, context.obstacles, px + dx, py + dy, context.rng)) break;
            }
        }
    }
    if (animalSpawning && rules.depth == 0 && ++animalSpawnTimer_ >= kAnimalSpawnTicks) {
        animalSpawnTimer_ = 0;
        if (animalCount() < kMaxAnimals) {
            const auto kind = static_cast<MobKind>(1 + context.rng.nextInt(3));  // cow, pig or sheep
            spawnNear(kind, 1, context.map, context.obstacles, p.x, p.y, kSpawnMinTiles, kSpawnMaxTiles, context.rng);
        }
    }
}

void Mobs::explode(const Creeper& creeper, const Context& context) {
    context.events.sound(Sound::Explode);
    const Vec2 c = creeper.center();
    const int damage = creeper.blastDamage();
    // Damage falls off with distance: blast / (distance + 1), plus 1 (normal difficulty).
    const auto blastAt = [&](Vec2 target) {
        const float distance = std::hypot(target.x - c.x, target.y - c.y);
        return static_cast<int>(static_cast<float>(damage) / (distance + 1.0f)) + 1;
    };
    const int radius = creeper.level();
    const float reach = static_cast<float>((radius + 1) * TileMap::kTileSize);
    const Vec2 p = context.player.center();
    if (std::hypot(p.x - c.x, p.y - c.y) < reach) {
        context.player.takeHit(blastAt(p), p.x < c.x ? -1 : 1, 0, context.events);
    }
    for (auto& mob : mobs_) {
        if (mob.get() == &creeper) continue;
        const Vec2 m = mob->center();
        if (std::hypot(m.x - c.x, m.y - c.y) < reach) {
            mob->hurt(blastAt(m), m.x < c.x ? -1 : 1, 0, context.events);
        }
    }
    // The blast digs out everything breakable around it; stairs, hard rock and liquids survive.
    const int tx = collision::tileIndex(c.x);
    const int ty = collision::tileIndex(c.y - 2.0f);
    for (int y = ty - radius; y <= ty + radius; ++y) {
        for (int x = tx - radius; x <= tx + radius; ++x) {
            if (!context.map.inBounds(x, y)) continue;
            const Tile tile = context.map.tileAt(x, y);
            if (tile == Tile::StairsDown || tile == Tile::StairsUp || tile == Tile::HardRock || tile == Tile::Water ||
                tile == Tile::Lava || tile == Tile::InfiniteFall || tile == Tile::Cloud) {
                continue;
            }
            context.map.setTile(x, y, Tile::Hole);
            context.events.smash(x, y);
        }
    }
}

bool Mobs::spawnNear(MobKind kind, int level, const TileMap& map, std::span<const Rect> obstacles, float x,
                     float y, int minTiles, int maxTiles, Random& rng) {
    const int centerX = collision::tileIndex(x);
    const int centerY = collision::tileIndex(y);
    for (int attempt = 0; attempt < 20; ++attempt) {
        const int tx = centerX + rng.nextInt(2 * maxTiles + 1) - maxTiles;
        const int ty = centerY + rng.nextInt(2 * maxTiles + 1) - maxTiles;
        const int distance = std::max(std::abs(tx - centerX), std::abs(ty - centerY));
        if (distance >= minTiles && spawnAt(kind, level, map, obstacles, tx, ty, rng)) return true;
    }
    return false;
}

bool Mobs::spawnAt(MobKind kind, int level, const TileMap& map, std::span<const Rect> obstacles, int tx,
                   int ty, Random& rng) {
    if (!map.inBounds(tx, ty) || !canSpawnOn(kind, map.tileAt(tx, ty))) return false;
    auto mob = makeMob(kind, level, static_cast<float>(tx * TileMap::kTileSize),
                       static_cast<float>(ty * TileMap::kTileSize) - 3.0f, rng);
    if (occupied(mob->hitbox(), nullptr, obstacles)) return false;
    mobs_.push_back(std::move(mob));
    return true;
}

std::vector<Rect> Mobs::hitboxes() const {
    std::vector<Rect> boxes;
    boxes.reserve(mobs_.size());
    for (const auto& mob : mobs_) boxes.push_back(mob->hitbox());
    return boxes;
}

int Mobs::count(MobKind kind) const {
    return static_cast<int>(
        std::count_if(mobs_.begin(), mobs_.end(), [&](const std::unique_ptr<Mob>& mob) { return mob->kind() == kind; }));
}

int Mobs::enemyCount() const {
    return static_cast<int>(std::count_if(mobs_.begin(), mobs_.end(),
                                          [](const std::unique_ptr<Mob>& mob) { return isEnemy(mob->kind()); }));
}

int Mobs::animalCount() const { return static_cast<int>(mobs_.size()) - enemyCount(); }

const Mob* Mobs::boss() const {
    for (const auto& mob : mobs_) {
        if (mob->kind() == MobKind::AirWizard) return mob.get();
    }
    return nullptr;
}

void Mobs::clearEnemies() {
    std::erase_if(mobs_, [](const std::unique_ptr<Mob>& mob) {
        return isEnemy(mob->kind()) && mob->kind() != MobKind::AirWizard;
    });
}

bool Mobs::takeBossDefeated() { return std::exchange(bossDefeated_, false); }

bool Mobs::hit(const Rect& box, int damage, Point direction, Events& events) {
    bool hit = false;
    for (auto& mob : mobs_) {
        const Rect mobBox = mob->hitbox();
        if (intersects(mobBox, box)) {
            mob->hurt(damage, direction.x, direction.y, events);
            hit = true;
        }
    }
    return hit;
}

