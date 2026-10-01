#include "player_actions.h"

#include "dropped_items.h"
#include "effects.h"
#include "mobs.h"
#include "player.h"

void PlayerActions::punch() {
    if (!player_.tryPunch()) return;  // out of energy
    attack(nullptr);
}

// Player.attack's "hurt" step, bare-handed (`tool` null) or with a tool: mobs in the attack box take 1-2 damage
// plus the tool's bonus, and the tile in front takes 1-3. A tool pays 1 durability for its mob bonus and 1 if
// anything was hit, like Minicraft.
void PlayerActions::attack(Inventory::Stack* tool) {
    int mobDamage = static_cast<int>(SDL_rand(2)) + 1;
    const ToolInfo info = tool ? toolInfo(tool->type) : ToolInfo{};
    const int bonus = tool && tool->durability > 0 ? toolMobBonus(info) : 0;
    const bool hitMob = mobs_.punch(player_.attackBox(), mobDamage + bonus, player_.facing(), effects_);
    if (hitMob && bonus > 0) --tool->durability;
    const SDL_Point target = player_.interactionTile();
    const int damage = static_cast<int>(SDL_rand(3)) + 1;  // bare-hand punch: 1-3, like Minicraft
    const auto hit = map_.hurtTile(target.x, target.y, damage);
    if (tool && (hitMob || hit) && tool->durability > 0) --tool->durability;
    if (!hit) {
        player_.showSlash();  // nothing to hit: just the slash
        return;
    }
    onTileHit(target, damage, *hit, false);
}

// What a hit tile shows and drops: the smash X and damage number, an apple now and then from trees, and the
// tile's loot when it breaks. A flower is simply picked. `withPickaxe`: a rock mined with a pickaxe drops more
// stone and coal (RockTile.hurt with dropCoal).
void PlayerActions::onTileHit(SDL_Point target, int damage, const TileMap::TileHit& hit, bool withPickaxe) {
    const float centerX = static_cast<float>(target.x * TileMap::kTileSize + TileMap::kTileSize / 2);
    const float centerY = static_cast<float>(target.y * TileMap::kTileSize + TileMap::kTileSize / 2);
    if (hit.tile == Tile::Flower) {
        // FlowerTile.hurt: the flower is picked (dropped as an item) and grass is left; no smash or number.
        drops_.spawn(flowerItem(map_.flowerVariant(target.x, target.y)), 1, centerX, centerY);
        player_.showSlash();
        return;
    }
    effects_.addSmash(target.x, target.y);
    effects_.addDamageNumber(damage, centerX, centerY);
    // Minicraft's TreeTile.hurt: every hit on a tree has a 1 in 100 chance to shake an apple loose.
    if (hit.tile == Tile::Tree && SDL_rand(100) == 0) drops_.spawn(ItemType::Apple, 1, centerX, centerY);
    if (!hit.broken) return;
    // Minicraft drops: a tree gives 1-3 wood and 0-2 acorns; a rock gives 1 stone by hand, or 2-4 stone and
    // 2 coal with a pickaxe.
    if (hit.tile == Tile::Tree) {
        drops_.spawn(ItemType::Wood, 1 + static_cast<int>(SDL_rand(3)), centerX, centerY);
        drops_.spawn(ItemType::Acorn, static_cast<int>(SDL_rand(3)), centerX, centerY);
    }
    if (hit.tile == Tile::Rock) {
        drops_.spawn(ItemType::Stone, withPickaxe ? 2 + static_cast<int>(SDL_rand(3)) : 1, centerX, centerY);
        if (withPickaxe) drops_.spawn(ItemType::Coal, 2, centerX, centerY);
    }
}

// Swinging a tool: 1 energy like any swing; then, if the tool has a use on the tile in front, it does that
// (paying extra energy and durability); otherwise it attacks with the tool. A tool at 0 durability breaks.
void PlayerActions::swingTool() {
    if (!player_.tryPunch()) return;  // out of energy
    Inventory::Stack tool = *player_.heldItem();
    if (!useToolOnTile(tool)) attack(&tool);
    player_.showSlash();
    if (tool.durability <= 0) {
        player_.setHeldItem(std::nullopt);  // worn out
    } else {
        player_.setHeldItem(tool);
    }
}

// The tile interactions of Minicraft's tools (Tile.interact). Each use pays energy (4 - level; a pickaxe on rock
// 5 - level) and 1 durability; returns false if the tool has no use here or the player is out of energy.
bool PlayerActions::useToolOnTile(Inventory::Stack& tool) {
    const ToolInfo info = toolInfo(tool.type);
    const SDL_Point target = player_.interactionTile();
    if (!map_.inBounds(target.x, target.y)) return false;
    const Tile tile = map_.tileAt(target.x, target.y);
    const auto pay = [&](int cost) {
        if (!player_.payEnergy(cost) || tool.durability <= 0) return false;
        --tool.durability;
        return true;
    };
    const float centerX = static_cast<float>(target.x * TileMap::kTileSize + TileMap::kTileSize / 2);
    const float centerY = static_cast<float>(target.y * TileMap::kTileSize + TileMap::kTileSize / 2);
    const int cost = 4 - info.level;
    switch (info.type) {
        case ToolType::Axe:
            if (tile != Tile::Tree || !pay(cost)) return false;
            break;
        case ToolType::Pickaxe:
            if (tile == Tile::Rock) {
                if (!pay(5 - info.level)) return false;
                break;
            }
            if (tile != Tile::Grass || !pay(cost)) return false;
            map_.setTile(target.x, target.y, Tile::Path);  // GrassTile: pickaxe makes a path
            return true;
        case ToolType::Shovel:
            if (tile == Tile::Grass) {
                if (!pay(cost)) return false;
                map_.setTile(target.x, target.y, Tile::Dirt);
                return true;
            }
            if (tile != Tile::Sand && tile != Tile::Dirt) return false;
            if (!pay(cost)) return false;
            map_.setTile(target.x, target.y, Tile::Hole);  // dig a hole and keep what was dug out
            drops_.spawn(tile == Tile::Sand ? ItemType::Sand : ItemType::Dirt, 1, centerX, centerY);
            return true;
        case ToolType::Hoe:
            if ((tile != Tile::Grass && tile != Tile::Dirt) || !pay(cost)) return false;
            map_.setTile(target.x, target.y, Tile::Farmland);
            return true;
        default: return false;
    }
    // Axe on a tree or pickaxe on rock: much more damage than a punch.
    const int damage = toolDamage(info);
    if (const auto hit = map_.hurtTile(target.x, target.y, damage)) {
        onTileHit(target, damage, *hit, info.type == ToolType::Pickaxe);
    }
    return true;
}
