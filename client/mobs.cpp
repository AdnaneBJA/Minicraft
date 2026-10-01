#include "mobs.h"

#include "audio.h"
#include "collision.h"
#include "dropped_items.h"
#include "effects.h"
#include "player.h"
#include "projectiles.h"
#include "tile_map.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

constexpr float kTick = 1.0f / 60.0f;
constexpr int kEnemySpawnTicks = 60;    // try to spawn an enemy every second (at night, or anytime underground)
constexpr int kAnimalSpawnTicks = 120;  // and an animal every two seconds on the surface
constexpr int kSpawnMinTiles = 10;      // off screen on the surface
constexpr int kSpawnMaxTiles = 20;
constexpr int kCaveSpawnMinTiles = 5;   // underground, anywhere out of the light
constexpr int kCaveSpawnMaxTiles = 16;
constexpr float kDespawnDistance = 48.0f * static_cast<float>(TileMap::kTileSize);
constexpr float kDaytimeDespawnDistance = 12.0f * static_cast<float>(TileMap::kTileSize);  // surface, out of view

const char* spriteFile(MobKind kind) {
    switch (kind) {
        case MobKind::Zombie: return "zombie.png";
        case MobKind::Cow: return "cow.png";
        case MobKind::Pig: return "pig.png";
        case MobKind::Sheep: return "sheep.png";
        case MobKind::Skeleton: return "skeleton.png";
        case MobKind::Slime: return "slime.png";
        case MobKind::Creeper: return "creeper.png";
        case MobKind::Snake: return "snake.png";
        case MobKind::AirWizard: return "air_wizard.png";
    }
    return "";
}

// Enemies spawn on any open ground except stairs, crops and doors (EnemyMob.checkStartPos); animals only on grass
// (PassiveMob.checkStartPos: grass or flowers).
bool canSpawnOn(MobKind kind, Tile tile) {
    if (isEnemy(kind)) {
        return !blocksMobs(tile) && tile != Tile::StairsDown && tile != Tile::StairsUp && tile != Tile::Wheat &&
               tile != Tile::Farmland && tile != Tile::WoodDoor && tile != Tile::StoneDoor;
    }
    return tile == Tile::Grass || tile == Tile::Flower;
}

std::unique_ptr<Mob> makeMob(MobKind kind, int level, float x, float y) {
    switch (kind) {
        case MobKind::Zombie: return std::make_unique<Zombie>(x, y, level);
        case MobKind::Skeleton: return std::make_unique<Skeleton>(x, y, level);
        case MobKind::Slime: return std::make_unique<Slime>(x, y, level);
        case MobKind::Creeper: return std::make_unique<Creeper>(x, y, level);
        case MobKind::Snake: return std::make_unique<Snake>(x, y, level);
        case MobKind::AirWizard: return std::make_unique<AirWizard>(x, y);
        default: return std::make_unique<Animal>(kind, x, y);
    }
}

// Level.isLight: inside the radius of any light source.
bool isLit(float x, float y, std::span<const Lighting::Light> lights) {
    return std::any_of(lights.begin(), lights.end(), [&](const Lighting::Light& light) {
        return std::hypot(light.x - x, light.y - y) < light.radius;
    });
}

}  // namespace

bool MobSprites::load(SDL_Renderer* renderer, const std::string& spriteDirectory) {
    for (int i = 0; i < kMobKinds; ++i) {
        const std::string path = spriteDirectory + spriteFile(static_cast<MobKind>(i));
        sprites_[static_cast<std::size_t>(i)] = loadTexture(renderer, path);
        flashes_[static_cast<std::size_t>(i)] = loadTexture(renderer, path, true);
        if (!sprites_[static_cast<std::size_t>(i)] || !flashes_[static_cast<std::size_t>(i)]) return false;
    }
    return true;
}

void Mobs::update(float dt, const Context& context, const SpawnRules& rules) {
    tickAccumulator_ += dt;
    while (tickAccumulator_ >= kTick) {
        tickAccumulator_ -= kTick;
        tick(context, rules);
    }
}

bool Mobs::occupied(const SDL_FRect& box, const Mob* self, std::span<const SDL_FRect> obstacles) const {
    for (const auto& other : mobs_) {
        if (other.get() == self) continue;
        const SDL_FRect otherBox = other->hitbox();
        if (SDL_HasRectIntersectionFloat(&box, &otherBox)) return true;
    }
    return std::any_of(obstacles.begin(), obstacles.end(),
                       [&](const SDL_FRect& obstacle) { return SDL_HasRectIntersectionFloat(&box, &obstacle); });
}

void Mobs::tick(const Context& context, const SpawnRules& rules) {
    const Mob::Blocked blocked = [&](const SDL_FRect& box, const Mob* self) {
        return occupied(box, self, context.obstacles);
    };
    const Mob::World world{context.map, context.player, context.effects, context.projectiles, context.audio, blocked};
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
    const SDL_FPoint p = context.player.center();
    std::erase_if(mobs_, [&](const std::unique_ptr<Mob>& mob) {
        if (mob->isDead()) {
            mob->dropLoot(context.drops);
            if (mob->kind() == MobKind::AirWizard) {
                bossDefeated_ = true;
                context.audio.play(Sound::BossDeath);
            }
            return true;
        }
        if (mob->kind() == MobKind::AirWizard) return false;  // the boss never leaves
        const SDL_FPoint c = mob->center();
        const float distance = std::hypot(c.x - p.x, c.y - p.y);
        if (distance > kDespawnDistance) return true;
        return rules.depth == 0 && isEnemy(mob->kind()) && !rules.night && distance > kDaytimeDespawnDistance;
    });

    trySpawn(context, rules);
}

void Mobs::trySpawn(const Context& context, const SpawnRules& rules) {
    const SDL_FPoint p = context.player.center();
    // Level.trySpawn: enemies at night on the surface, and anytime in the caves and the sky.
    const bool enemiesNow = rules.depth != 0 || rules.night;
    if (enemySpawning && enemiesNow && ++enemySpawnTimer_ >= kEnemySpawnTicks) {
        enemySpawnTimer_ = 0;
        if (enemyCount() - count(MobKind::AirWizard) < kMaxEnemies) {
            // Mob level: 1 on the surface, the depth in the caves; the sky's are the toughest (the original's 4).
            const int level = rules.depth > 0 ? 4 : std::clamp(-rules.depth, 1, 4);
            const int roll = static_cast<int>(SDL_rand(100));
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
                const int dx = static_cast<int>(SDL_rand(2 * maxTiles + 1)) - maxTiles;
                const int dy = static_cast<int>(SDL_rand(2 * maxTiles + 1)) - maxTiles;
                if (std::max(std::abs(dx), std::abs(dy)) < minTiles) continue;
                const float cx = static_cast<float>((px + dx) * TileMap::kTileSize + TileMap::kTileSize / 2);
                const float cy = static_cast<float>((py + dy) * TileMap::kTileSize + TileMap::kTileSize / 2);
                if (isLit(cx, cy, rules.lights)) continue;
                if (spawnAt(kind, level, context.map, context.obstacles, px + dx, py + dy)) break;
            }
        }
    }
    if (animalSpawning && rules.depth == 0 && ++animalSpawnTimer_ >= kAnimalSpawnTicks) {
        animalSpawnTimer_ = 0;
        if (animalCount() < kMaxAnimals) {
            const auto kind = static_cast<MobKind>(1 + static_cast<int>(SDL_rand(3)));  // cow, pig or sheep
            spawnNear(kind, 1, context.map, context.obstacles, p.x, p.y, kSpawnMinTiles, kSpawnMaxTiles);
        }
    }
}

void Mobs::explode(const Creeper& creeper, const Context& context) {
    context.audio.play(Sound::Explode);
    const SDL_FPoint c = creeper.center();
    const int damage = creeper.blastDamage();
    // Damage falls off with distance: blast / (distance + 1), plus 1 (normal difficulty).
    const auto blastAt = [&](SDL_FPoint target) {
        const float distance = std::hypot(target.x - c.x, target.y - c.y);
        return static_cast<int>(static_cast<float>(damage) / (distance + 1.0f)) + 1;
    };
    const int radius = creeper.level();
    const float reach = static_cast<float>((radius + 1) * TileMap::kTileSize);
    const SDL_FPoint p = context.player.center();
    if (std::hypot(p.x - c.x, p.y - c.y) < reach) {
        context.player.takeHit(blastAt(p), p.x < c.x ? -1 : 1, 0, context.effects, context.audio);
    }
    for (auto& mob : mobs_) {
        if (mob.get() == &creeper) continue;
        const SDL_FPoint m = mob->center();
        if (std::hypot(m.x - c.x, m.y - c.y) < reach) {
            mob->hurt(blastAt(m), m.x < c.x ? -1 : 1, 0, context.effects, context.audio);
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
            context.effects.addSmash(x, y);
        }
    }
}

bool Mobs::spawnNear(MobKind kind, int level, const TileMap& map, std::span<const SDL_FRect> obstacles, float x,
                     float y, int minTiles, int maxTiles) {
    const int centerX = collision::tileIndex(x);
    const int centerY = collision::tileIndex(y);
    for (int attempt = 0; attempt < 20; ++attempt) {
        const int tx = centerX + static_cast<int>(SDL_rand(2 * maxTiles + 1)) - maxTiles;
        const int ty = centerY + static_cast<int>(SDL_rand(2 * maxTiles + 1)) - maxTiles;
        const int distance = std::max(std::abs(tx - centerX), std::abs(ty - centerY));
        if (distance >= minTiles && spawnAt(kind, level, map, obstacles, tx, ty)) return true;
    }
    return false;
}

bool Mobs::spawnAt(MobKind kind, int level, const TileMap& map, std::span<const SDL_FRect> obstacles, int tx,
                   int ty) {
    if (!map.inBounds(tx, ty) || !canSpawnOn(kind, map.tileAt(tx, ty))) return false;
    auto mob = makeMob(kind, level, static_cast<float>(tx * TileMap::kTileSize),
                       static_cast<float>(ty * TileMap::kTileSize) - 3.0f);
    if (occupied(mob->hitbox(), nullptr, obstacles)) return false;
    mobs_.push_back(std::move(mob));
    return true;
}

std::vector<SDL_FRect> Mobs::hitboxes() const {
    std::vector<SDL_FRect> boxes;
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

bool Mobs::hit(const SDL_FRect& box, int damage, SDL_Point direction, Effects& effects, Audio& audio) {
    bool hit = false;
    for (auto& mob : mobs_) {
        const SDL_FRect mobBox = mob->hitbox();
        if (SDL_HasRectIntersectionFloat(&mobBox, &box)) {
            mob->hurt(damage, direction.x, direction.y, effects, audio);
            hit = true;
        }
    }
    return hit;
}

void Mobs::draw(SDL_Renderer* renderer, const Camera& camera, const MobSprites& sprites, float playerY,
                bool behind) const {
    for (const auto& mob : mobs_) {
        if ((mob->center().y < playerY) == behind) {
            mob->draw(renderer, camera, sprites.sprite(mob->kind()), sprites.flash(mob->kind()));
        }
    }
}
