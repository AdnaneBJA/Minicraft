#include "items.h"

#include <algorithm>

const char* itemName(ItemType type) {
    switch (type) {
        case ItemType::Wood: return "Wood";
        case ItemType::Stone: return "Stone";
    }
    return "?";
}

bool Inventory::canAdd(ItemType type) const {
    return this->count(type) > 0 || static_cast<int>(stacks_.size()) < kMaxSlots;
}

void Inventory::add(ItemType type, int count) {
    const auto it = std::find_if(stacks_.begin(), stacks_.end(), [&](const Stack& s) { return s.type == type; });
    if (it != stacks_.end()) {
        it->count += count;
    } else {
        stacks_.push_back({type, count});
    }
}

int Inventory::count(ItemType type) const {
    const auto it = std::find_if(stacks_.begin(), stacks_.end(), [&](const Stack& s) { return s.type == type; });
    return it != stacks_.end() ? it->count : 0;
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
