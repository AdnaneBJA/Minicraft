#include "items.h"

#include <algorithm>
#include <iterator>
#include <string>

namespace {

// Names in ItemType order.
constexpr const char* kNames[] = {
    "Wood", "Stone", "Workbench", "Cloth", "Iron", "Potato", "Acorn", "Apple",
    "Wood Sword", "Wood Axe", "Wood Hoe", "Wood Pickaxe", "Wood Shovel", "Wood Bow",
    "Rock Sword", "Rock Axe", "Rock Hoe", "Rock Pickaxe", "Rock Shovel", "Rock Bow",
    "Arrow", "String", "Leather", "Raw Beef", "Raw Pork", "White Wool",
    "Dandelion", "Poppy", "Oxeye Daisy", "Cornflower", "Allium", "Blue Orchid", "Rose", "Iris",
    "Sand", "Dirt", "Coal",
    "Iron Ore", "Gold Ore", "Gem", "Gold", "Glass", "Slime", "Bone", "Gunpowder", "Wheat", "Seeds",
    "Bread", "Cooked Pork", "Steak", "Baked Potato", "Gold Apple", "Cactus", "Cloud", "Cloud Ore",
    "Torch", "Plank", "Stone Brick", "Plank Wall", "Stone Wall", "Wood Door", "Stone Door", "Scale",
    "Furnace", "Oven", "Anvil", "Chest", "Lantern", "Loom", "Bed", "Power Glove",
    "Iron Sword", "Iron Axe", "Iron Hoe", "Iron Pickaxe", "Iron Shovel", "Iron Bow",
    "Gold Sword", "Gold Axe", "Gold Hoe", "Gold Pickaxe", "Gold Shovel", "Gold Bow",
    "Gem Sword", "Gem Axe", "Gem Hoe", "Gem Pickaxe", "Gem Shovel", "Gem Bow",
    "Leather Armor", "Snake Armor", "Iron Armor", "Gold Armor", "Gem Armor",
};
static_assert(std::size(kNames) == kItemTypeCount, "one name per ItemType");

// First item of each block of six tools (sword, axe, hoe, pickaxe, shovel, bow), by level.
constexpr ItemType kToolBlocks[] = {ItemType::WoodSword, ItemType::RockSword, ItemType::IronSword, ItemType::GoldSword,
                                    ItemType::GemSword};

}  // namespace

const char* itemName(ItemType type) {
    const int index = static_cast<int>(type);
    return index >= 0 && index < kItemTypeCount ? kNames[index] : "?";
}

bool isFurniture(ItemType type) {
    switch (type) {
        case ItemType::Workbench:
        case ItemType::Furnace:
        case ItemType::Oven:
        case ItemType::Anvil:
        case ItemType::Chest:
        case ItemType::Lantern:
        case ItemType::Loom:
        case ItemType::Bed: return true;
        default: return false;
    }
}

bool isTool(ItemType type) { return toolInfo(type).type != ToolType::None; }

bool isStackable(ItemType type) { return !isFurniture(type) && !isTool(type) && type != ItemType::PowerGlove; }

ItemType flowerItem(int variant) { return static_cast<ItemType>(static_cast<int>(ItemType::Dandelion) + variant); }

int foodValue(ItemType type) {
    switch (type) {
        case ItemType::BakedPotato:
        case ItemType::Apple:
        case ItemType::RawPork:
        case ItemType::RawBeef: return 1;
        case ItemType::Bread: return 2;
        case ItemType::CookedPork:
        case ItemType::Steak: return 3;
        case ItemType::GoldenApple: return 10;
        default: return 0;
    }
}

int armorLevel(ItemType type) {
    switch (type) {
        case ItemType::LeatherArmor: return 1;
        case ItemType::SnakeArmor: return 2;
        case ItemType::IronArmor: return 3;
        case ItemType::GoldArmor: return 4;
        case ItemType::GemArmor: return 5;
        default: return 0;
    }
}

int armorPoints(ItemType type) {
    switch (type) {
        case ItemType::LeatherArmor: return 30;
        case ItemType::SnakeArmor: return 40;
        case ItemType::IronArmor: return 50;
        case ItemType::GoldArmor: return 70;
        case ItemType::GemArmor: return 100;
        default: return 0;
    }
}

int lightRadius(ItemType type) {
    if (type == ItemType::Lantern) return 9;  // Lantern.Type.NORM
    if (type == ItemType::Torch) return 5;    // TorchTile.getLightRadius
    return 0;
}

ToolInfo toolInfo(ItemType type) {
    const int value = static_cast<int>(type);
    for (int level = 0; level < static_cast<int>(std::size(kToolBlocks)); ++level) {
        const int first = static_cast<int>(kToolBlocks[level]);
        if (value >= first && value < first + 6) return {static_cast<ToolType>(1 + value - first), level};
    }
    return {};
}

int toolDamage(const ToolInfo& tool) { return static_cast<int>(SDL_rand(5)) + tool.level * 5 + 10; }

int toolMobBonus(const ToolInfo& tool) {
    const int level = tool.level;
    switch (tool.type) {
        case ToolType::Axe: return (level + 1) * 2 + static_cast<int>(SDL_rand(4));               // wood 2-5
        case ToolType::Sword: return (level + 1) * 3 + static_cast<int>(SDL_rand(2 + level * level));  // wood 3-4
        case ToolType::Pickaxe: return (level + 1) + static_cast<int>(SDL_rand(2));                // wood 1-2
        case ToolType::None: return 0;
        default: return 1;
    }
}

int maxDurability(ItemType type) {
    const ToolInfo tool = toolInfo(type);
    int base = 0;  // ToolType durabilities
    switch (tool.type) {
        case ToolType::Shovel: base = 34; break;
        case ToolType::Hoe: base = 30; break;
        case ToolType::Sword: base = 52; break;
        case ToolType::Pickaxe: base = 38; break;
        case ToolType::Axe: base = 34; break;
        case ToolType::Bow: base = 30; break;
        case ToolType::None: return 0;
    }
    return base * (tool.level + 1);
}

std::string displayName(const Inventory::Stack& stack) {
    if (!isStackable(stack.type)) return std::string(" ") + itemName(stack.type);
    return " " + std::to_string(std::min(stack.count, 999)) + " " + itemName(stack.type);
}

bool Inventory::canAdd(ItemType type) const {
    return (isStackable(type) && this->count(type) > 0) || static_cast<int>(stacks_.size()) < kMaxSlots;
}

int Inventory::add(ItemType type, int count) {
    if (isStackable(type)) {
        const auto it = std::find_if(stacks_.begin(), stacks_.end(), [&](const Stack& s) { return s.type == type; });
        if (it != stacks_.end()) {
            it->count += count;
            return 0;
        }
        if (!canAdd(type)) return count;
        stacks_.push_back({type, count});
        return 0;
    }
    for (; count > 0 && canAdd(type); --count) stacks_.push_back({type, 1, maxDurability(type)});
    return count;
}

int Inventory::add(const Stack& stack) {
    if (isStackable(stack.type)) return add(stack.type, stack.count);
    if (!canAdd(stack.type)) return stack.count;
    stacks_.push_back({stack.type, 1, stack.durability});
    return stack.count - 1;
}

int Inventory::remove(ItemType type, int count) {
    int removed = 0;
    for (auto it = stacks_.rbegin(); it != stacks_.rend() && removed < count;) {
        if (it->type != type) {
            ++it;
            continue;
        }
        const int taken = std::min(it->count, count - removed);
        it->count -= taken;
        removed += taken;
        if (it->count == 0) {
            it = std::make_reverse_iterator(stacks_.erase(std::next(it).base()));
        } else {
            ++it;
        }
    }
    return removed;
}

Inventory::Stack Inventory::take(int index) {
    const Stack stack = stacks_.at(static_cast<std::size_t>(index));
    stacks_.erase(stacks_.begin() + index);
    return stack;
}

int Inventory::count(ItemType type) const {
    int total = 0;
    for (const auto& stack : stacks_) {
        if (stack.type == type) total += stack.count;
    }
    return total;
}

bool ItemIcons::load(SDL_Renderer* renderer, const std::string& path) {
    texture_ = loadTexture(renderer, path);
    return texture_ != nullptr;
}

void ItemIcons::draw(SDL_Renderer* renderer, ItemType type, float x, float y) const {
    const SDL_FRect source{static_cast<float>(static_cast<int>(type)) * kSize, 0.0f, kSize, kSize};
    const SDL_FRect destination{x, y, kSize, kSize};
    SDL_RenderTexture(renderer, texture_.get(), &source, &destination);
}

void ItemIcons::drawShadow(SDL_Renderer* renderer, ItemType type, float x, float y) const {
    SDL_SetTextureColorMod(texture_.get(), 0, 0, 0);
    SDL_SetTextureAlphaMod(texture_.get(), 140);
    draw(renderer, type, x, y);
    SDL_SetTextureColorMod(texture_.get(), 255, 255, 255);
    SDL_SetTextureAlphaMod(texture_.get(), 255);
}
