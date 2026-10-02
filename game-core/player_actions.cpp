#include "player_actions.h"

#include "events.h"
#include "player.h"
#include "random.h"
#include "world.h"

namespace {

constexpr int kTileCenter = TileMap::kTileSize / 2;

Vec2 tileCenter(Point tile) {
    return {static_cast<float>(tile.x * TileMap::kTileSize + kTileCenter),
            static_cast<float>(tile.y * TileMap::kTileSize + kTileCenter)};
}


// Ground a torch or a floor can go on.
bool isOpenGround(Tile tile) {
    switch (tile) {
        case Tile::Grass:
        case Tile::Sand:
        case Tile::Dirt:
        case Tile::Path:
        case Tile::Farmland:
        case Tile::WoodPlanks:
        case Tile::StoneBricks:
        case Tile::Cloud: return true;
        default: return false;
    }
}

}  // namespace

void PlayerActions::useOrPunch() {
    const auto& held = player_.heldItem();
    if (!held) {
        punch();
        return;
    }
    if (isTool(held->type)) {
        swingTool();
        return;
    }
    useItem();
}

void PlayerActions::punch() {
    if (!player_.tryPunch()) return;  // out of energy
    attack(nullptr);
}

// Player.attack's "hurt" step, bare-handed (`tool` null) or with a tool: mobs in the attack box take 1-2 damage
// plus the tool's bonus, and the tile in front takes 1-3. A tool pays 1 durability for its mob bonus and 1 if
// anything was hit, like Minicraft.
void PlayerActions::attack(Inventory::Stack* tool) {
    const int mobDamage = rng_.nextInt(2) + 1;
    const ToolInfo info = tool ? toolInfo(tool->type) : ToolInfo{};
    const int bonus = tool && tool->durability > 0 ? toolMobBonus(info, rng_) : 0;
    const Rect reach = player_.attackBox();
    bool hitMob = level_.mobs.hit(reach, mobDamage + bonus, player_.facing(), events_);
    // PvP: other players in reach take the same hit, knocked back the way the attacker faces.
    for (Player* other : others_) {
        if (!intersects(reach, other->hitbox())) continue;
        if (other->takeHit(mobDamage + bonus, player_.facing().x, player_.facing().y, events_)) hitMob = true;
    }
    if (hitMob && bonus > 0) --tool->durability;
    const Point target = player_.interactionTile();
    TileMap& map = level_.map;
    if (!map.inBounds(target.x, target.y)) {
        player_.showSlash();
        return;
    }
    const Tile tile = map.tileAt(target.x, target.y);
    // A punch opens and closes doors (DoorTile.hurt).
    if (tile == Tile::WoodDoor || tile == Tile::StoneDoor) {
        map.setData(target.x, target.y, map.dataAt(target.x, target.y) ? 0 : 1);
        player_.showSlash();
        return;
    }
    // Ores and hard rock only shrug off a punch (OreTile.hurt / HardRockTile.hurt deal 0).
    if (isOre(tile) || tile == Tile::HardRock) {
        events_.smash(target.x, target.y);
        const Vec2 c = tileCenter(target);
        events_.number(0, c.x, c.y);
        events_.sound(Sound::MonsterHurt);
        // A gem pickaxe ends up here only when the player is too tired to dig (2 energy): no hint then.
        const bool gemPickaxe = info.type == ToolType::Pickaxe && info.level >= 4;
        if (tile == Tile::HardRock && !gemPickaxe) events_.notify("Gem pickaxe required!");
        return;
    }
    const int damage = rng_.nextInt(3) + 1;  // bare-hand punch: 1-3, like Minicraft
    const auto hit = map.hurtTile(target.x, target.y, damage, rng_);
    if (tool && (hitMob || hit) && tool->durability > 0) --tool->durability;
    if (!hit) {
        player_.showSlash();  // nothing to hit: just the slash
        return;
    }
    onTileHit(target, damage, *hit, false);
}

// What a hit tile shows and drops: the smash X and damage number, an apple now and then from trees, and the
// tile's loot when it breaks. Flowers, saplings, torches and crops are simply picked. `withPickaxe`: a rock mined
// with a pickaxe drops more stone and coal (RockTile.hurt with dropCoal).
void PlayerActions::onTileHit(Point target, int damage, const TileMap::TileHit& hit, bool withPickaxe) {
    const Vec2 c = tileCenter(target);
    DroppedItems& drops = level_.drops;
    switch (hit.tile) {
        case Tile::Flower:
            // FlowerTile.hurt: the flower is picked (dropped as an item) and grass is left; no smash or number.
            drops.spawn(flowerItem(level_.map.flowerVariant(target.x, target.y)), 1, c.x, c.y, rng_);
            player_.showSlash();
            return;
        case Tile::Sapling:
        case Tile::CactusSapling:
            events_.sound(Sound::MonsterHurt);  // SaplingTile.hurt: trampled back to grass or sand
            player_.showSlash();
            return;
        case Tile::Torch:
            drops.spawn(ItemType::Torch, 1, c.x, c.y, rng_);
            player_.showSlash();
            return;
        case Tile::Wheat: {
            // The original WheatTile.harvest: 0-1 seeds, plus 2-4 wheat when ripe (1-2 when nearly ripe).
            events_.sound(Sound::MonsterHurt);
            drops.spawn(ItemType::Seeds, rng_.nextInt(2), c.x, c.y, rng_);
            const int age = hit.data;
            if (age >= 50) drops.spawn(ItemType::Wheat, rng_.nextInt(4 - 2 + 1) + 2, c.x, c.y, rng_);
            else if (age >= 40) drops.spawn(ItemType::Wheat, rng_.nextInt(2 - 1 + 1) + 1, c.x, c.y, rng_);
            player_.showSlash();
            return;
        }
        default: break;
    }
    events_.smash(target.x, target.y);
    events_.number(damage, c.x, c.y);
    events_.sound(Sound::MonsterHurt);  // every tile's hurt() plays the hit sound
    // Minicraft's TreeTile.hurt: every hit on a tree has a 1 in 100 chance to shake an apple loose.
    if (hit.tile == Tile::Tree && rng_.nextInt(100) == 0) drops.spawn(ItemType::Apple, 1, c.x, c.y, rng_);
    // OreTile.hurt: every hit that does damage knocks 0-1 ore loose, and 2 more when it breaks.
    const auto oreDrop = [&](Tile tile) {
        switch (tile) {
            case Tile::IronOre: return ItemType::IronOre;
            case Tile::GoldOre: return ItemType::GoldOre;
            case Tile::GemOre: return ItemType::Gem;
            default: return ItemType::CloudOre;
        }
    };
    if (isOre(hit.tile) && damage > 0) {
        drops.spawn(oreDrop(hit.tile), rng_.nextInt(2) + (hit.broken ? 2 : 0), c.x, c.y, rng_);
    }
    if (!hit.broken) return;
    switch (hit.tile) {
        case Tile::Tree:
            // A tree gives 1-3 wood and 0-2 acorns.
            drops.spawn(ItemType::Wood, rng_.nextInt(3 - 1 + 1) + 1, c.x, c.y, rng_);
            drops.spawn(ItemType::Acorn, rng_.nextInt(3), c.x, c.y, rng_);
            break;
        case Tile::Rock:
            // 1 stone by hand, or 2-4 stone and 2 coal with a pickaxe.
            drops.spawn(ItemType::Stone, withPickaxe ? rng_.nextInt(4 - 2 + 1) + 2 : 1, c.x, c.y, rng_);
            if (withPickaxe) drops.spawn(ItemType::Coal, 2, c.x, c.y, rng_);
            break;
        case Tile::HardRock:
            drops.spawn(ItemType::Stone, rng_.nextInt(3 - 1 + 1) + 1, c.x, c.y, rng_);
            drops.spawn(ItemType::Coal, rng_.nextInt(2), c.x, c.y, rng_);
            break;
        case Tile::Cactus: drops.spawn(ItemType::Cactus, rng_.nextInt(4 - 2 + 1) + 2, c.x, c.y, rng_); break;
        case Tile::WoodWall: drops.spawn(ItemType::PlankWall, 1, c.x, c.y, rng_); break;
        case Tile::StoneWall: drops.spawn(ItemType::StoneWall, 1, c.x, c.y, rng_); break;
        default: break;
    }
}

// Swinging a tool: 1 energy like any swing; then, if the tool has a use on the tile in front, it does that
// (paying extra energy and durability); otherwise it attacks with the tool. A tool at 0 durability breaks.
void PlayerActions::swingTool() {
    Inventory::Stack tool = *player_.heldItem();
    if (toolInfo(tool.type).type == ToolType::Bow) {
        if (!shootBow(tool)) return;
    } else {
        if (!player_.tryPunch()) return;  // out of energy
        if (!useToolOnTile(tool)) attack(&tool);
        player_.showSlash();
    }
    if (tool.durability <= 0) {
        player_.setHeldItem(std::nullopt);  // worn out
    } else {
        player_.setHeldItem(tool);
    }
}

bool PlayerActions::shootBow(Inventory::Stack& bow) {
    // Player.attack with a bow: an arrow from the inventory, 1 energy and 1 durability.
    if (inventory_.count(ItemType::Arrow) == 0) {
        events_.notify("No arrows!");
        return false;
    }
    if (!player_.tryPunch()) return false;
    inventory_.remove(ItemType::Arrow, 1);
    const Vec2 c = player_.center();
    level_.projectiles.shootArrow(c.x, c.y - 2.0f, player_.facing(), toolInfo(bow.type).level, player_.id());
    --bow.durability;
    return true;
}

// The tile interactions of Minicraft's tools (Tile.interact). Each use pays energy and 1 durability; returns false
// if the tool has no use here or the player is out of energy.
bool PlayerActions::useToolOnTile(Inventory::Stack& tool) {
    const ToolInfo info = toolInfo(tool.type);
    const Point target = player_.interactionTile();
    TileMap& map = level_.map;
    if (!map.inBounds(target.x, target.y)) return false;
    const Tile tile = map.tileAt(target.x, target.y);
    const auto pay = [&](int cost) {
        if (!player_.payEnergy(cost) || tool.durability <= 0) return false;
        --tool.durability;
        return true;
    };
    const Vec2 c = tileCenter(target);
    // Takes a floor or door up again (FloorTile / DoorTile.interact with the matching tool).
    const auto pickUp = [&](ItemType item, Tile left) {
        if (!pay(4 - info.level)) return false;
        events_.sound(Sound::MonsterHurt);
        map.setTile(target.x, target.y, left);
        level_.drops.spawn(item, 1, c.x, c.y, rng_);
        return true;
    };
    // Grass dug or tilled sometimes gives up seeds (GrassTile.interact).
    const auto maybeSeeds = [&]() {
        if (rng_.nextInt(5) == 0) level_.drops.spawn(ItemType::Seeds, 1, c.x, c.y, rng_);
    };
    const int cost = 4 - info.level;
    int damage = toolDamage(info, rng_);
    switch (info.type) {
        case ToolType::Axe:
            if (tile == Tile::WoodPlanks) return pickUp(ItemType::Plank, Tile::Dirt);
            if (tile == Tile::WoodDoor) return pickUp(ItemType::WoodDoor, Tile::WoodPlanks);
            if ((tile != Tile::Tree && tile != Tile::WoodWall) || !pay(cost)) return false;
            break;
        case ToolType::Pickaxe:
            if (tile == Tile::StoneBricks) return pickUp(ItemType::StoneBrick, Tile::Dirt);
            if (tile == Tile::StoneDoor) return pickUp(ItemType::StoneDoor, Tile::StoneBricks);
            if (tile == Tile::Rock) {
                if (!pay(5 - info.level)) return false;
                break;
            }
            if (isOre(tile)) {
                if (!pay(6 - info.level)) return false;  // OreTile.interact
                break;
            }
            if (tile == Tile::HardRock) {
                // HardRockTile.interact: only a gem pickaxe gets through, for 2 energy a swing.
                if (info.level < 4) {
                    events_.notify("Gem pickaxe required!");
                    return false;
                }
                if (!pay(2)) return false;
                break;
            }
            if (tile == Tile::StoneWall) {
                if (!pay(cost)) return false;
                break;
            }
            if (tile != Tile::Grass || !pay(cost)) return false;
            map.setTile(target.x, target.y, Tile::Path);  // GrassTile: pickaxe makes a path
            return true;
        case ToolType::Shovel:
            if (tile == Tile::Grass) {
                if (!pay(cost)) return false;
                map.setTile(target.x, target.y, Tile::Dirt);
                maybeSeeds();
                return true;
            }
            if (tile == Tile::Cloud) {
                // CloudTile.interact: scoop the cloud up (5 energy), leaving the endless fall.
                if (!pay(5)) return false;
                map.setTile(target.x, target.y, Tile::InfiniteFall);
                level_.drops.spawn(ItemType::Cloud, rng_.nextInt(3 - 1 + 1) + 1, c.x, c.y, rng_);
                return true;
            }
            if (tile != Tile::Sand && tile != Tile::Dirt) return false;
            if (!pay(cost)) return false;
            map.setTile(target.x, target.y, Tile::Hole);  // dig a hole and keep what was dug out
            level_.drops.spawn(tile == Tile::Sand ? ItemType::Sand : ItemType::Dirt, 1, c.x, c.y, rng_);
            return true;
        case ToolType::Hoe:
            if ((tile != Tile::Grass && tile != Tile::Dirt) || !pay(cost)) return false;
            map.setTile(target.x, target.y, Tile::Farmland);
            if (tile == Tile::Grass) maybeSeeds();
            return true;
        default: return false;
    }
    // Axe on wood, pickaxe on stone and ore: much more damage than a punch.
    if (const auto hit = map.hurtTile(target.x, target.y, damage, rng_)) {
        onTileHit(target, damage, *hit, info.type == ToolType::Pickaxe);
    }
    return true;
}

std::optional<Tile> PlayerActions::placedTile(ItemType item, Tile target) const {
    const bool fillable = target == Tile::Hole || target == Tile::Water || target == Tile::Lava;
    switch (item) {
        case ItemType::Dirt: return fillable ? std::optional(Tile::Dirt) : std::nullopt;
        case ItemType::Sand: return fillable ? std::optional(Tile::Sand) : std::nullopt;
        case ItemType::Cloud:
            return target == Tile::InfiniteFall ? std::optional(Tile::Cloud) : std::nullopt;
        case ItemType::Seeds: return target == Tile::Farmland ? std::optional(Tile::Wheat) : std::nullopt;
        case ItemType::Acorn: return target == Tile::Grass ? std::optional(Tile::Sapling) : std::nullopt;
        case ItemType::Cactus: return target == Tile::Sand ? std::optional(Tile::CactusSapling) : std::nullopt;
        case ItemType::Torch: return isOpenGround(target) ? std::optional(Tile::Torch) : std::nullopt;
        case ItemType::Plank:
            return fillable || (isOpenGround(target) && target != Tile::WoodPlanks) ? std::optional(Tile::WoodPlanks)
                                                                                    : std::nullopt;
        case ItemType::StoneBrick:
            return fillable || (isOpenGround(target) && target != Tile::StoneBricks)
                       ? std::optional(Tile::StoneBricks)
                       : std::nullopt;
        case ItemType::PlankWall: return target == Tile::WoodPlanks ? std::optional(Tile::WoodWall) : std::nullopt;
        case ItemType::StoneWall: return target == Tile::StoneBricks ? std::optional(Tile::StoneWall) : std::nullopt;
        case ItemType::WoodDoor: return target == Tile::WoodPlanks ? std::optional(Tile::WoodDoor) : std::nullopt;
        case ItemType::StoneDoor: return target == Tile::StoneBricks ? std::optional(Tile::StoneDoor) : std::nullopt;
        default: return std::nullopt;
    }
}

void PlayerActions::consumeHeld() {
    Inventory::Stack held = *player_.heldItem();
    if (--held.count <= 0) {
        player_.setHeldItem(std::nullopt);
    } else {
        player_.setHeldItem(held);
    }
}

void PlayerActions::useItem() {
    const Inventory::Stack held = *player_.heldItem();
    const Point target = player_.interactionTile();
    TileMap& map = level_.map;

    if (held.type == ItemType::PowerGlove) {
        // PowerGloveItem: lift the furniture in front into the hand; the glove goes back into the inventory.
        const Furniture::Piece* piece = level_.furniture.at(target.x, target.y);
        if (!piece) return;
        if (piece->isContainer() && !piece->contents.empty()) {
            events_.notify("Empty the chest first!");
            return;
        }
        const ItemType type = piece->type;
        const bool deathChest = piece->deathChest;
        level_.furniture.remove(piece);
        events_.sound(Sound::MonsterHurt);
        if (deathChest) return;  // an emptied death chest just goes away
        inventory_.add(held);
        player_.setHeldItem(Inventory::Stack{type, 1});
        return;
    }
    if (isFurniture(held.type)) {
        if (level_.furniture.place(held.type, target.x, target.y, map, level_.mobs.hitboxes())) {
            player_.setHeldItem(std::nullopt);
        }
        return;
    }
    if (const int food = foodValue(held.type); food > 0) {
        if (player_.eat(food)) consumeHeld();
        return;
    }
    if (armorLevel(held.type) > 0) {
        if (player_.wearArmor(held.type)) consumeHeld();
        else if (player_.armor()) events_.notify("Already wearing armor!");
        return;
    }
    if (!map.inBounds(target.x, target.y)) return;
    const Tile tile = map.tileAt(target.x, target.y);
    if (const auto placed = placedTile(held.type, tile)) {
        // Nothing may stand where a solid tile goes.
        if (isSolid(*placed) && level_.furniture.at(target.x, target.y)) return;
        const std::uint8_t data = *placed == Tile::Torch ? static_cast<std::uint8_t>(tile) : 0;
        map.setTile(target.x, target.y, *placed, data);
        consumeHeld();
    }
}
