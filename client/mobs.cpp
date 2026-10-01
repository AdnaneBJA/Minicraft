#include "mobs.h"

#include "collision.h"
#include "dropped_items.h"
#include "effects.h"
#include "player.h"
#include "tile_map.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kTick = 1.0f / 60.0f;
constexpr int kZombieSpawnTicks = 60;   // try to spawn a zombie every second at night
constexpr int kAnimalSpawnTicks = 120;  // and an animal every two seconds
constexpr int kSpawnMinTiles = 10;      // off screen
constexpr int kSpawnMaxTiles = 20;
constexpr float kDespawnDistance = 48.0f * static_cast<float>(TileMap::kTileSize);
constexpr float kDaytimeDespawnDistance = 12.0f * static_cast<float>(TileMap::kTileSize);  // zombies out of view

const char* spriteFile(MobKind kind) {
    switch (kind) {
        case MobKind::Zombie: return "zombie.png";
        case MobKind::Cow: return "cow.png";
        case MobKind::Pig: return "pig.png";
        case MobKind::Sheep: return "sheep.png";
    }
    return "";
}

// Zombies spawn on any open ground; animals only on grass (PassiveMob.checkStartPos: grass or flowers).
bool canSpawnOn(MobKind kind, Tile tile) {
    if (kind == MobKind::Zombie) return !blocksMobs(tile);
    return tile == Tile::Grass || tile == Tile::Flower;
}

std::unique_ptr<Mob> makeMob(MobKind kind, float x, float y) {
    if (kind == MobKind::Zombie) return std::make_unique<Zombie>(x, y);
    return std::make_unique<Animal>(kind, x, y);
}

}  // namespace

bool Mobs::load(SDL_Renderer* renderer, const std::string& spriteDirectory) {
    for (int i = 0; i < kMobKinds; ++i) {
        const std::string path = spriteDirectory + spriteFile(static_cast<MobKind>(i));
        sprites_[static_cast<std::size_t>(i)] = loadTexture(renderer, path);
        flashes_[static_cast<std::size_t>(i)] = loadTexture(renderer, path, true);
        if (!sprites_[static_cast<std::size_t>(i)] || !flashes_[static_cast<std::size_t>(i)]) return false;
    }
    return true;
}

void Mobs::update(float dt, const TileMap& map, Player& player, Effects& effects, DroppedItems& drops, bool night,
                  std::span<const SDL_FRect> obstacles) {
    tickAccumulator_ += dt;
    while (tickAccumulator_ >= kTick) {
        tickAccumulator_ -= kTick;
        tick(map, player, effects, drops, night, obstacles);
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

void Mobs::tick(const TileMap& map, Player& player, Effects& effects, DroppedItems& drops, bool night,
                std::span<const SDL_FRect> obstacles) {
    const Mob::Blocked blocked = [&](const SDL_FRect& box, const Mob* self) { return occupied(box, self, obstacles); };
    const Mob::World world{map, player, effects, blocked};
    for (auto& mob : mobs_) mob->tick(world);

    // Dead mobs drop their loot; mobs far from the player despawn (zombies sooner during the day).
    const SDL_FPoint p = player.center();
    std::erase_if(mobs_, [&](const std::unique_ptr<Mob>& mob) {
        if (mob->isDead()) {
            mob->dropLoot(drops);
            return true;
        }
        const SDL_FPoint c = mob->center();
        const float distance = std::hypot(c.x - p.x, c.y - p.y);
        if (distance > kDespawnDistance) return true;
        return mob->kind() == MobKind::Zombie && !night && distance > kDaytimeDespawnDistance;
    });

    if (zombieSpawning && night && ++zombieSpawnTimer_ >= kZombieSpawnTicks) {
        zombieSpawnTimer_ = 0;
        if (count(MobKind::Zombie) < kMaxZombies) {
            spawnNear(MobKind::Zombie, map, obstacles, p.x, p.y, kSpawnMinTiles, kSpawnMaxTiles);
        }
    }
    if (animalSpawning && ++animalSpawnTimer_ >= kAnimalSpawnTicks) {
        animalSpawnTimer_ = 0;
        if (animalCount() < kMaxAnimals) {
            const auto kind = static_cast<MobKind>(1 + static_cast<int>(SDL_rand(3)));  // cow, pig or sheep
            spawnNear(kind, map, obstacles, p.x, p.y, kSpawnMinTiles, kSpawnMaxTiles);
        }
    }
}

bool Mobs::spawnNear(MobKind kind, const TileMap& map, std::span<const SDL_FRect> obstacles, float x, float y,
                     int minTiles, int maxTiles) {
    const int centerX = collision::tileIndex(x);
    const int centerY = collision::tileIndex(y);
    for (int attempt = 0; attempt < 20; ++attempt) {
        const int tx = centerX + static_cast<int>(SDL_rand(2 * maxTiles + 1)) - maxTiles;
        const int ty = centerY + static_cast<int>(SDL_rand(2 * maxTiles + 1)) - maxTiles;
        const int distance = std::max(std::abs(tx - centerX), std::abs(ty - centerY));
        if (distance < minTiles || !map.inBounds(tx, ty) || !canSpawnOn(kind, map.tileAt(tx, ty))) continue;
        auto mob = makeMob(kind, static_cast<float>(tx * TileMap::kTileSize),
                           static_cast<float>(ty * TileMap::kTileSize) - 3.0f);
        if (occupied(mob->hitbox(), nullptr, obstacles)) continue;
        mobs_.push_back(std::move(mob));
        return true;
    }
    return false;
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

int Mobs::animalCount() const { return static_cast<int>(mobs_.size()) - count(MobKind::Zombie); }

bool Mobs::punch(const SDL_FRect& attackBox, int damage, SDL_Point direction, Effects& effects) {
    bool hit = false;
    for (auto& mob : mobs_) {
        const SDL_FRect box = mob->hitbox();
        if (SDL_HasRectIntersectionFloat(&box, &attackBox)) {
            mob->hurt(damage, direction.x, direction.y, effects);
            hit = true;
        }
    }
    return hit;
}

void Mobs::draw(SDL_Renderer* renderer, const Camera& camera, float playerY, bool behind) const {
    for (const auto& mob : mobs_) {
        if ((mob->center().y < playerY) == behind) {
            const auto kind = static_cast<std::size_t>(mob->kind());
            mob->draw(renderer, camera, sprites_[kind].get(), flashes_[kind].get());
        }
    }
}
