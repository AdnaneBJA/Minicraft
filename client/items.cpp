#include "items.h"

#include <algorithm>
#include <iterator>
#include <string>

const char* itemName(ItemType type) {
    switch (type) {
        case ItemType::Wood: return "Wood";
        case ItemType::Stone: return "Stone";
        case ItemType::Workbench: return "Workbench";
        case ItemType::Cloth: return "Cloth";
        case ItemType::Iron: return "Iron";
        case ItemType::Potato: return "Potato";
        case ItemType::Acorn: return "Acorn";
        case ItemType::Apple: return "Apple";
        case ItemType::WoodSword: return "Wood Sword";
        case ItemType::WoodAxe: return "Wood Axe";
        case ItemType::WoodHoe: return "Wood Hoe";
        case ItemType::WoodPickaxe: return "Wood Pickaxe";
        case ItemType::WoodShovel: return "Wood Shovel";
        case ItemType::WoodBow: return "Wood Bow";
        case ItemType::RockSword: return "Rock Sword";
        case ItemType::RockAxe: return "Rock Axe";
        case ItemType::RockHoe: return "Rock Hoe";
        case ItemType::RockPickaxe: return "Rock Pickaxe";
        case ItemType::RockShovel: return "Rock Shovel";
        case ItemType::RockBow: return "Rock Bow";
        case ItemType::Arrow: return "Arrow";
        case ItemType::String: return "String";
        case ItemType::Leather: return "Leather";
        case ItemType::RawBeef: return "Raw Beef";
        case ItemType::RawPork: return "Raw Pork";
        case ItemType::WhiteWool: return "White Wool";
        case ItemType::Dandelion: return "Dandelion";
        case ItemType::Poppy: return "Poppy";
        case ItemType::OxeyeDaisy: return "Oxeye Daisy";
        case ItemType::Cornflower: return "Cornflower";
        case ItemType::Allium: return "Allium";
        case ItemType::BlueOrchid: return "Blue Orchid";
        case ItemType::Rose: return "Rose";
        case ItemType::Iris: return "Iris";
        case ItemType::Sand: return "Sand";
        case ItemType::Dirt: return "Dirt";
        case ItemType::Coal: return "Coal";
    }
    return "?";
}

bool isFurniture(ItemType type) { return type == ItemType::Workbench; }

bool isTool(ItemType type) {
    const int value = static_cast<int>(type);
    return value >= static_cast<int>(ItemType::WoodSword) && value <= static_cast<int>(ItemType::RockBow);
}

bool isStackable(ItemType type) { return !isFurniture(type) && !isTool(type); }

ItemType flowerItem(int variant) { return static_cast<ItemType>(static_cast<int>(ItemType::Dandelion) + variant); }

ToolInfo toolInfo(ItemType type) {
    switch (type) {
        case ItemType::WoodSword: return {ToolType::Sword, 0};
        case ItemType::WoodAxe: return {ToolType::Axe, 0};
        case ItemType::WoodHoe: return {ToolType::Hoe, 0};
        case ItemType::WoodPickaxe: return {ToolType::Pickaxe, 0};
        case ItemType::WoodShovel: return {ToolType::Shovel, 0};
        case ItemType::WoodBow: return {ToolType::Bow, 0};
        case ItemType::RockSword: return {ToolType::Sword, 1};
        case ItemType::RockAxe: return {ToolType::Axe, 1};
        case ItemType::RockHoe: return {ToolType::Hoe, 1};
        case ItemType::RockPickaxe: return {ToolType::Pickaxe, 1};
        case ItemType::RockShovel: return {ToolType::Shovel, 1};
        case ItemType::RockBow: return {ToolType::Bow, 1};
        default: return {};
    }
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
