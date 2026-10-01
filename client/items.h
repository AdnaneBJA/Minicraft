#pragma once

#include "texture.h"

#include <SDL3/SDL.h>

#include <string>
#include <vector>

enum class ItemType { Wood, Stone };

const char* itemName(ItemType type);

// Stacks of items the player carries, in the order they were first picked up.
class Inventory {
public:
    struct Stack {
        ItemType type;
        int count;
    };

    void add(ItemType type, int count = 1);
    int count(ItemType type) const;
    const std::vector<Stack>& stacks() const { return stacks_; }
    void clear() { stacks_.clear(); }

private:
    std::vector<Stack> stacks_;
};

// The 8x8 item icons (items.png: wood, stone).
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
