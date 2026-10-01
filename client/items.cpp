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
    }
    return "?";
}

bool isFurniture(ItemType type) { return type == ItemType::Workbench; }

bool isStackable(ItemType type) { return !isFurniture(type); }

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
    for (; count > 0 && canAdd(type); --count) stacks_.push_back({type, 1});
    return count;
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
