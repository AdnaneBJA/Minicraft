#include "recipe.h"

#include <algorithm>
#include <utility>

Recipe::Recipe(ItemType product, int amount, std::vector<Cost> costs)
    : product_(product), amount_(amount), costs_(std::move(costs)) {}

bool Recipe::canCraft(const Inventory& inventory) const {
    return std::all_of(costs_.begin(), costs_.end(),
                       [&](const Cost& cost) { return inventory.count(cost.type) >= cost.count; });
}

int Recipe::craft(Inventory& inventory) const {
    if (!canCraft(inventory)) return -1;
    for (const Cost& cost : costs_) inventory.remove(cost.type, cost.count);
    return inventory.add(product_, amount_);
}

std::vector<Recipe> Recipe::workbenchRecipes() {
    using enum ItemType;
    return {
        Recipe(WoodSword, 1, {{Wood, 5}}),
        Recipe(WoodAxe, 1, {{Wood, 5}}),
        Recipe(WoodHoe, 1, {{Wood, 5}}),
        Recipe(WoodPickaxe, 1, {{Wood, 5}}),
        Recipe(WoodShovel, 1, {{Wood, 5}}),
        Recipe(WoodBow, 1, {{Wood, 5}, {String, 2}}),
        Recipe(RockSword, 1, {{Wood, 5}, {Stone, 5}}),
        Recipe(RockAxe, 1, {{Wood, 5}, {Stone, 5}}),
        Recipe(RockHoe, 1, {{Wood, 5}, {Stone, 5}}),
        Recipe(RockPickaxe, 1, {{Wood, 5}, {Stone, 5}}),
        Recipe(RockShovel, 1, {{Wood, 5}, {Stone, 5}}),
        Recipe(RockBow, 1, {{Wood, 5}, {Stone, 5}, {String, 2}}),
        Recipe(Arrow, 3, {{Wood, 2}, {Stone, 2}}),
    };
}

std::vector<Recipe> Recipe::personalRecipes() {
    return {
        Recipe(ItemType::Workbench, 1, {{ItemType::Wood, 10}}),
    };
}
