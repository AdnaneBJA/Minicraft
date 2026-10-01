#pragma once

#include "texture.h"

#include <SDL3/SDL.h>

#include <string>
#include <vector>

// The order matches the columns of items.png.
enum class ItemType { Wood, Stone, Workbench, Cloth, Iron, Potato };

const char* itemName(ItemType type);
// Resources stack; furniture (like Minicraft's FurnitureItem) takes one slot per item.
bool isStackable(ItemType type);

// Stacks of items the player carries, in the order they were first picked up. A non-stackable item is a stack of 1.
class Inventory {
public:
    static constexpr int kMaxSlots = 27;  // Minicraft's inventory size; each stack takes one slot

    struct Stack {
        ItemType type;
        int count;
    };

    // True if the item stacks onto an existing stack or a free slot is left.
    bool canAdd(ItemType type) const;
    // Adds as many as fit and returns how many didn't (only non-stackable items can run out of slots here).
    int add(ItemType type, int count = 1);
    // Removes up to `count` items, emptying stacks from the last one; returns how many were removed.
    int remove(ItemType type, int count);
    int count(ItemType type) const;
    const std::vector<Stack>& stacks() const { return stacks_; }
    void clear() { stacks_.clear(); }

private:
    std::vector<Stack> stacks_;
};

// The 8x8 item icons (items.png: wood, stone, workbench, cloth, iron, potato).
class ItemIcons {
public:
    static constexpr float kSize = 8.0f;

    bool load(SDL_Renderer* renderer, const std::string& path);
    void draw(SDL_Renderer* renderer, ItemType type, float x, float y) const;
    // Dark silhouette of the icon, used as a drop shadow under items lying on the ground.
    void drawShadow(SDL_Renderer* renderer, ItemType type, float x, float y) const;

private:
    TexturePtr texture_;
};
