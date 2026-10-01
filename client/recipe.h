#pragma once

#include "items.h"

#include <vector>

// A crafting recipe (Minicraft's Recipe): `amount` of `product` for a list of item costs.
class Recipe {
public:
    struct Cost {
        ItemType type;
        int count;
    };

    Recipe(ItemType product, int amount, std::vector<Cost> costs);

    ItemType product() const { return product_; }
    int amount() const { return amount_; }
    const std::vector<Cost>& costs() const { return costs_; }

    bool canCraft(const Inventory& inventory) const;
    // Takes the costs and adds the product. Returns how many products didn't fit (the caller drops them on the
    // ground, like Minicraft), or -1 if the inventory can't pay.
    int craft(Inventory& inventory) const;

    // What the player can craft by hand (Z), without a workbench (Minicraft's Recipes.craftRecipes).
    static std::vector<Recipe> personalRecipes();
    // What a placed workbench offers (Minicraft's Recipes.workbenchRecipes: the wood and rock tools and arrows).
    static std::vector<Recipe> workbenchRecipes();

private:
    ItemType product_;
    int amount_;
    std::vector<Cost> costs_;
};
