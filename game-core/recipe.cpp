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

std::vector<Recipe> Recipe::personalRecipes() {
    using enum ItemType;
    return {
        Recipe(Workbench, 1, {{Wood, 10}}),
        Recipe(Torch, 2, {{Wood, 1}, {Coal, 1}}),
        Recipe(Plank, 2, {{Wood, 1}}),
        Recipe(PlankWall, 1, {{Plank, 3}}),
        Recipe(WoodDoor, 1, {{Plank, 5}}),
    };
}

std::vector<Recipe> Recipe::stationRecipes(ItemType station) {
    using enum ItemType;
    switch (station) {
        case Workbench:
            return {
                Recipe(Workbench, 1, {{Wood, 10}}),
                Recipe(Torch, 2, {{Wood, 1}, {Coal, 1}}),
                Recipe(Plank, 2, {{Wood, 1}}),
                Recipe(PlankWall, 1, {{Plank, 3}}),
                Recipe(WoodDoor, 1, {{Plank, 5}}),
                Recipe(Lantern, 1, {{Wood, 8}, {Slime, 4}, {Glass, 3}}),
                Recipe(StoneBrick, 1, {{Stone, 2}}),
                Recipe(StoneWall, 1, {{StoneBrick, 3}}),
                Recipe(StoneDoor, 1, {{StoneBrick, 5}}),
                Recipe(Oven, 1, {{Stone, 15}}),
                Recipe(Furnace, 1, {{Stone, 20}}),
                Recipe(Chest, 1, {{Wood, 20}}),
                Recipe(Anvil, 1, {{Iron, 5}}),
                Recipe(Loom, 1, {{Wood, 10}, {WhiteWool, 5}}),
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
                Recipe(LeatherArmor, 1, {{Leather, 10}}),
                Recipe(SnakeArmor, 1, {{Scale, 15}}),
            };
        case Furnace:
            return {
                Recipe(Iron, 1, {{IronOre, 3}, {Coal, 1}}),
                Recipe(Gold, 1, {{GoldOre, 3}, {Coal, 1}}),
                Recipe(Glass, 1, {{Sand, 4}, {Coal, 1}}),
            };
        case Oven:
            return {
                Recipe(CookedPork, 1, {{RawPork, 1}, {Coal, 1}}),
                Recipe(Steak, 1, {{RawBeef, 1}, {Coal, 1}}),
                Recipe(Bread, 1, {{Wheat, 4}}),
                Recipe(BakedPotato, 1, {{Potato, 1}}),
            };
        case Anvil: {
            std::vector<Recipe> recipes{
                Recipe(IronArmor, 1, {{Iron, 10}}),
                Recipe(GoldArmor, 1, {{Gold, 10}}),
                Recipe(GemArmor, 1, {{Gem, 65}}),
                Recipe(GoldenApple, 1, {{Apple, 1}, {Gold, 8}}),
            };
            // Iron, gold and gem tools: 5 wood plus 5 iron, 5 gold or 50 gems (bows also take 2 string).
            const struct {
                ItemType firstTool;
                ItemType material;
                int amount;
            } levels[] = {{IronSword, Iron, 5}, {GoldSword, Gold, 5}, {GemSword, Gem, 50}};
            for (const auto& level : levels) {
                for (int i = 0; i < 6; ++i) {
                    const auto tool = static_cast<ItemType>(static_cast<int>(level.firstTool) + i);
                    std::vector<Cost> costs{{Wood, 5}, {level.material, level.amount}};
                    if (i == 5) costs.push_back({String, 2});
                    recipes.emplace_back(tool, 1, std::move(costs));
                }
            }
            return recipes;
        }
        case Loom:
            return {
                Recipe(String, 2, {{WhiteWool, 1}}),
                Recipe(WhiteWool, 1, {{String, 3}}),
                Recipe(Bed, 1, {{Wood, 5}, {WhiteWool, 3}}),
                Recipe(LeatherArmor, 1, {{Leather, 10}}),
            };
        default: return {};
    }
}
