#pragma once

class Random;

#include <string>
#include <vector>

// The order matches the columns of items.png, and saves store items by number: new items go at the end.
enum class ItemType {
    Wood, Stone, Workbench, Cloth, Iron, Potato, Acorn, Apple,
    // Tools (Minicraft's ToolItem): wood and rock levels.
    WoodSword, WoodAxe, WoodHoe, WoodPickaxe, WoodShovel, WoodBow,
    RockSword, RockAxe, RockHoe, RockPickaxe, RockShovel, RockBow,
    Arrow, String, Leather, RawBeef, RawPork, WhiteWool,
    // Flowers (Minicraft+'s FlowerTile variants), in the same order as the flower sprites in tiles.png.
    Dandelion, Poppy, OxeyeDaisy, Cornflower, Allium, BlueOrchid, Rose, Iris,
    Sand, Dirt, Coal,
    IronOre, GoldOre, Gem, Gold, Glass, Slime, Bone, Gunpowder, Wheat, Seeds,
    Bread, CookedPork, Steak, BakedPotato, GoldenApple, Cactus, Cloud, CloudOre,
    Torch, Plank, StoneBrick, PlankWall, StoneWall, WoodDoor, StoneDoor, Scale,
    // Furniture (the workbench is above).
    Furnace, Oven, Anvil, Chest, Lantern, Loom, Bed, PowerGlove,
    // Tools: iron, gold and gem levels.
    IronSword, IronAxe, IronHoe, IronPickaxe, IronShovel, IronBow,
    GoldSword, GoldAxe, GoldHoe, GoldPickaxe, GoldShovel, GoldBow,
    GemSword, GemAxe, GemHoe, GemPickaxe, GemShovel, GemBow,
    LeatherArmor, SnakeArmor, IronArmor, GoldArmor, GemArmor,
};
constexpr int kFlowerVariants = 8;
constexpr int kItemTypeCount = static_cast<int>(ItemType::GemArmor) + 1;  // keep in sync with the last ItemType

const char* itemName(ItemType type);
// Furniture (Minicraft's FurnitureItem) can be placed in the world, doesn't stack and takes one slot per item.
bool isFurniture(ItemType type);
// Tools (swords, axes, hoes, pickaxes, shovels, bows) don't stack either, like Minicraft's ToolItem.
bool isTool(ItemType type);
bool isStackable(ItemType type);
// The flower item for flower variant 0..kFlowerVariants-1.
ItemType flowerItem(int variant);
// Minicraft's FoodItem: how much hunger eating one restores (0 = not food).
int foodValue(ItemType type);
// Minicraft+'s ArmorItem: level 1 (leather) to 5 (gem), 0 = not armour.
int armorLevel(ItemType type);
// The armour points a fresh piece gives: Minicraft+'s armor fraction * maxArmor (100).
int armorPoints(ItemType type);
// Light radius (in tiles) of a lantern or torch, held or placed; 0 for everything else.
int lightRadius(ItemType type);

// Minicraft's ToolItem / ToolType.
enum class ToolType { None, Sword, Axe, Hoe, Pickaxe, Shovel, Bow };
struct ToolInfo {
    ToolType type = ToolType::None;
    int level = 0;  // 0 = wood, 1 = rock, 2 = iron, 3 = gold, 4 = gem
};
ToolInfo toolInfo(ItemType type);
// A new tool's durability: ToolType.durability * (level + 1). 0 for items that aren't tools.
int maxDurability(ItemType type);
// ToolItem.getDamage(): what the right tool does to its tile (axe on trees, pickaxe on rock): level * 5 + 10 + 0-4.
int toolDamage(const ToolInfo& tool, Random& rng);
// ToolItem.getAttackDamageBonus(): extra damage against mobs (sword and axe most, pickaxe a little, others 1).
int toolMobBonus(const ToolInfo& tool, Random& rng);

// Stacks of items the player carries, in the order they were first picked up. A non-stackable item is a stack of 1.
class Inventory {
public:
    static constexpr int kMaxSlots = 27;  // Minicraft's inventory size; each stack takes one slot

    struct Stack {
        ItemType type;
        int count;
        int durability = 0;  // uses left, for tools (each tool is its own stack of 1)
    };

    // True if the item stacks onto an existing stack or a free slot is left.
    bool canAdd(ItemType type) const;
    // Adds as many as fit and returns how many didn't (only non-stackable items can run out of slots here).
    // New tools start at full durability.
    int add(ItemType type, int count = 1);
    // Adds an existing stack, keeping its durability (a tool going back into the inventory). Returns how many
    // didn't fit.
    int add(const Stack& stack);
    // Removes up to `count` items, emptying stacks from the last one; returns how many were removed.
    int remove(ItemType type, int count);
    int count(ItemType type) const;
    // Takes the whole stack at `index` out of the inventory (Minicraft's Inventory.remove(int)).
    Stack take(int index);
    const std::vector<Stack>& stacks() const { return stacks_; }
    bool empty() const { return stacks_.empty(); }
    void clear() { stacks_.clear(); }

private:
    std::vector<Stack> stacks_;
};

// Minicraft's getDisplayName(): " <count> <name>" for stackable items, " <name>" for the rest.
std::string displayName(const Inventory::Stack& stack);
