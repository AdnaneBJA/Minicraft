// Gameplay rules, driven through Simulation::tick like real play: tools, ores and smelting, farming, liquids,
// building, bows, food and armour, hunger, the power glove, creepers, skeletons and the Air Wizard.
#include "recipe.h"
#include "simulation.h"

#include <gtest/gtest.h>

#include <memory>

namespace {

constexpr int kX = 128;  // the test area in the middle of a level
constexpr int kY = 128;

class Rules : public ::testing::Test {
protected:
    void SetUp() override {
        sim.startNewWorld(1337);
        goToLevel(World::kSurfaceIndex);
    }

    // Switches level with spawning off, so only the mobs a test adds are there.
    void goToLevel(int index) {
        sim.changeLevel(index, false);
        sim.level().mobs.clear();
        sim.level().mobs.enemySpawning = false;
        sim.level().mobs.animalSpawning = false;
        sim.events().take();
    }
    TileMap& map() { return sim.level().map; }
    Tile target() { return map().tileAt(kX + 1, kY); }

    // A square of `tile` around the test spot.
    void clearArea(int radius, Tile tile = Tile::Dirt) {
        for (int y = kY - radius; y <= kY + radius; ++y) {
            for (int x = kX - radius; x <= kX + radius; ++x) map().setTile(x, y, tile);
        }
    }
    // The player on (kX, kY), facing right: the tile in front is (kX + 1, kY).
    void standFacingRight() {
        Player& player = sim.player();
        player.setPosition(kX * 16.0f, kY * 16.0f - 3.0f);
        sim.tick({.moveX = 1});
        player.setPosition(kX * 16.0f, kY * 16.0f - 3.0f);
    }
    void hold(ItemType type, int count = 1) {
        sim.player().setHeldItem(Inventory::Stack{type, count, maxDurability(type)});
    }
    // Space, with full stats so energy never runs out mid-test.
    void use() {
        sim.player().refillStats();
        sim.tick({.attack = true, .attackPressed = true});
    }
    // Lets drops settle, then walks onto them.
    void collectDrops() {
        for (int i = 0; i < 40; ++i) {
            const Vec2 c = sim.player().center();
            sim.level().drops.tick(map(), Rect{c.x - 40, c.y - 40, 80, 80}, sim.inventory());
        }
    }
    void tickIdle(int ticks) {
        for (int i = 0; i < ticks; ++i) sim.tick({});
    }
    bool notified(const std::string& text) {
        for (const auto& event : sim.events().take()) {
            if (event.kind == GameEvent::Kind::Notification && event.text == text) return true;
        }
        return false;
    }

    Simulation sim;
};

}  // namespace

TEST_F(Rules, PunchingOreDoesNothingButAPickaxeMinesIt) {
    goToLevel(2);  // cave B1
    clearArea(3);
    map().setTile(kX + 1, kY, Tile::IronOre);
    standFacingRight();
    sim.player().setHeldItem(std::nullopt);
    use();
    EXPECT_EQ(target(), Tile::IronOre);
    EXPECT_EQ(map().damageAt(kX + 1, kY), 0);

    hold(ItemType::RockPickaxe);
    for (int swing = 0; swing < 20 && target() == Tile::IronOre; ++swing) use();
    collectDrops();
    EXPECT_EQ(target(), Tile::Dirt);
    EXPECT_GE(sim.inventory().count(ItemType::IronOre), 2);
}

TEST_F(Rules, TheFurnaceSmeltsIronAndTheAnvilMakesTheBetterTools) {
    sim.inventory().add(ItemType::IronOre, 3);
    sim.inventory().add(ItemType::Coal, 1);
    const auto furnace = Recipe::stationRecipes(ItemType::Furnace);
    ASSERT_EQ(furnace[0].product(), ItemType::Iron);
    EXPECT_TRUE(sim.craft(furnace[0]));
    EXPECT_EQ(sim.inventory().count(ItemType::Iron), 1);
    EXPECT_EQ(Recipe::stationRecipes(ItemType::Anvil).size(), 22u);
    EXPECT_FALSE(sim.craft(furnace[0]));  // out of ore
}

TEST_F(Rules, OnlyAGemPickaxeBreaksHardRock) {
    clearArea(3);
    map().setTile(kX + 1, kY, Tile::HardRock);
    standFacingRight();
    hold(ItemType::IronPickaxe);
    use();
    EXPECT_TRUE(notified("Gem pickaxe required!"));
    EXPECT_EQ(map().damageAt(kX + 1, kY), 0);

    hold(ItemType::GemPickaxe);
    for (int swing = 0; swing < 50 && target() == Tile::HardRock; ++swing) use();
    EXPECT_EQ(target(), Tile::Dirt);
}

TEST_F(Rules, ATiredGemPickaxeSwingShowsNoHint) {
    clearArea(3);
    map().setTile(kX + 1, kY, Tile::HardRock);
    standFacingRight();
    hold(ItemType::GemPickaxe);
    sim.player().restoreStats(10, 1, 10, std::nullopt, 0);  // enough to swing, not to dig
    sim.tick({.attack = true, .attackPressed = true});
    EXPECT_FALSE(notified("Gem pickaxe required!"));
}

TEST_F(Rules, WheatGrowsFromSeedsAndIsHarvested) {
    clearArea(3, Tile::Grass);
    standFacingRight();
    hold(ItemType::WoodHoe);
    use();
    ASSERT_EQ(target(), Tile::Farmland);
    hold(ItemType::Seeds, 3);
    use();
    ASSERT_EQ(target(), Tile::Wheat);
    EXPECT_EQ(sim.player().heldItem()->count, 2);

    for (int i = 0; i < 100000 && map().dataAt(kX + 1, kY) < TileMap::kWheatRipeAge; ++i) {
        map().tickRandomTiles(kX + 1, kY, 0, 1, sim.rng());
    }
    ASSERT_EQ(map().dataAt(kX + 1, kY), TileMap::kWheatRipeAge);
    sim.player().setHeldItem(std::nullopt);
    use();
    collectDrops();
    EXPECT_GE(sim.inventory().count(ItemType::Wheat), 2);
    EXPECT_EQ(target(), Tile::Dirt);
}

TEST_F(Rules, AnAcornGrowsIntoATree) {
    clearArea(3, Tile::Grass);
    standFacingRight();
    hold(ItemType::Acorn);
    use();
    ASSERT_EQ(target(), Tile::Sapling);
    EXPECT_FALSE(sim.player().heldItem());
    for (int i = 0; i < 200; ++i) map().tickRandomTiles(kX + 1, kY, 0, 1, sim.rng());
    EXPECT_EQ(target(), Tile::Tree);
}

TEST_F(Rules, WaterFlowsIntoHolesAndDirtFillsItBackIn) {
    clearArea(3);
    map().setTile(kX + 2, kY, Tile::Water);
    map().setTile(kX + 1, kY, Tile::Hole);
    for (int i = 0; i < 200; ++i) map().tickRandomTiles(kX + 2, kY, 0, 1, sim.rng());
    EXPECT_EQ(target(), Tile::Water);
    standFacingRight();
    hold(ItemType::Dirt);
    use();
    EXPECT_EQ(target(), Tile::Dirt);
}

TEST_F(Rules, TorchesArePlacedAndPunchedBackOut) {
    clearArea(3);
    standFacingRight();
    hold(ItemType::Torch, 2);
    use();
    ASSERT_EQ(target(), Tile::Torch);
    EXPECT_EQ(map().groundAt(kX + 1, kY), Tile::Dirt);
    sim.player().setHeldItem(std::nullopt);
    use();
    collectDrops();
    EXPECT_EQ(target(), Tile::Dirt);
    EXPECT_GE(sim.inventory().count(ItemType::Torch), 1);
}

TEST_F(Rules, DoorsGoOnFloorsAndOpenWhenPunched) {
    clearArea(3);
    standFacingRight();
    hold(ItemType::Plank, 2);
    use();
    ASSERT_EQ(target(), Tile::WoodPlanks);
    hold(ItemType::WoodDoor);
    use();
    ASSERT_EQ(target(), Tile::WoodDoor);
    EXPECT_TRUE(map().isSolidAt(kX + 1, kY));
    sim.player().setHeldItem(std::nullopt);
    use();
    EXPECT_FALSE(map().isSolidAt(kX + 1, kY));
}

TEST_F(Rules, ABowShootsAnArrowFromTheInventory) {
    clearArea(6);
    sim.level().mobs.add(std::make_unique<Zombie>((kX + 4) * 16.0f, kY * 16.0f - 3.0f, 2, sim.rng()));
    const int health = sim.level().mobs.all()[0]->health();
    standFacingRight();
    sim.inventory().add(ItemType::Arrow, 5);
    const int arrows = sim.inventory().count(ItemType::Arrow);
    hold(ItemType::IronBow);
    use();
    EXPECT_EQ(sim.inventory().count(ItemType::Arrow), arrows - 1);
    tickIdle(20);
    EXPECT_LT(sim.level().mobs.all()[0]->health(), health);
}

TEST_F(Rules, FoodFillsHungerAndArmourSoaksUpHits) {
    Player& player = sim.player();
    player.restoreStats(10, 10, 4, std::nullopt, 0);
    EXPECT_TRUE(player.eat(foodValue(ItemType::Steak)));
    EXPECT_EQ(player.hunger(), 7);
    ASSERT_TRUE(player.wearArmor(ItemType::IronArmor));
    EXPECT_EQ(player.armorPoints(), 50);
    player.takeHit(8, 0, 1, sim.events());
    EXPECT_EQ(player.armorPoints(), 42);
    EXPECT_EQ(player.health(), 8);  // iron armour (level 3): every 4 points absorbed cost a heart
}

TEST_F(Rules, WalkingWearsDownHunger) {
    clearArea(10, Tile::Grass);
    sim.player().restoreStats(10, 10, 10, std::nullopt, 0);
    for (int i = 0; i < 60 * 180; ++i) {  // 3 minutes of walking back and forth
        if (i % 120 == 0) sim.player().setPosition(kX * 16.0f - 32, kY * 16.0f - 3);
        sim.tick({.moveX = 1});
    }
    EXPECT_LT(sim.player().hunger(), 10);
    EXPECT_GE(sim.player().hunger(), 6);
}

TEST_F(Rules, ThePowerGloveOnlyLiftsEmptyChests) {
    clearArea(3);
    standFacingRight();
    ASSERT_TRUE(sim.level().furniture.place(ItemType::Chest, kX + 1, kY, map(), {}));
    sim.level().furniture.at(kX + 1, kY)->contents.add(ItemType::Gem, 3);
    hold(ItemType::PowerGlove);
    use();
    EXPECT_TRUE(notified("Empty the chest first!"));
    ASSERT_NE(sim.level().furniture.at(kX + 1, kY), nullptr);

    sim.level().furniture.at(kX + 1, kY)->contents.clear();
    use();
    EXPECT_EQ(sim.level().furniture.at(kX + 1, kY), nullptr);
    EXPECT_EQ(sim.player().heldItem()->type, ItemType::Chest);
    use();
    EXPECT_NE(sim.level().furniture.at(kX + 1, kY), nullptr);
}

TEST_F(Rules, ACreeperBlowsUpNextToThePlayer) {
    goToLevel(3);  // cave B2
    // A corridor, so the creeper can only come at the player.
    clearArea(6, Tile::Rock);
    for (int x = kX - 5; x <= kX + 5; ++x) map().setTile(x, kY, Tile::Dirt);
    standFacingRight();
    sim.player().restoreStats(10, 10, 10, std::nullopt, 0);
    sim.level().mobs.add(std::make_unique<Creeper>((kX + 2) * 16.0f, kY * 16.0f - 3.0f, 1, sim.rng()));
    for (int i = 0; i < 600 && sim.level().mobs.count(MobKind::Creeper) > 0; ++i) {
        sim.player().setPosition(kX * 16.0f, kY * 16.0f - 3.0f);
        sim.tick({});
    }
    EXPECT_EQ(sim.level().mobs.count(MobKind::Creeper), 0);
    EXPECT_LT(sim.player().health(), 10);
    int holes = 0;
    for (int x = kX - 2; x <= kX + 5; ++x) holes += map().tileAt(x, kY) == Tile::Hole;
    EXPECT_GT(holes, 0);
}

TEST_F(Rules, ASkeletonShootsAtThePlayer) {
    goToLevel(3);
    clearArea(8);
    standFacingRight();
    sim.level().mobs.add(std::make_unique<Skeleton>((kX + 5) * 16.0f, kY * 16.0f - 3.0f, 2, sim.rng()));
    int shots = 0;
    for (int i = 0; i < 600; ++i) {
        const std::size_t before = sim.level().projectiles.arrows().size();
        sim.player().refillStats();
        sim.player().setPosition(kX * 16.0f, kY * 16.0f - 3.0f);
        sim.tick({});
        if (sim.level().projectiles.arrows().size() > before) ++shots;
    }
    EXPECT_GT(shots, 0);
}

TEST_F(Rules, BeatingTheAirWizardWinsTheGame) {
    goToLevel(World::kSkyIndex);
    sim.world().spawnBoss(sim.rng());
    const Mob* boss = sim.level().mobs.boss();
    ASSERT_NE(boss, nullptr);
    sim.player().setPosition(boss->center().x - 30, boss->center().y - 11);
    std::size_t sparks = 0;
    for (int i = 0; i < 600; ++i) {
        sim.player().refillStats();
        sim.tick({});
        sparks = std::max(sparks, sim.level().projectiles.sparks().size());
    }
    EXPECT_GT(sparks, 10u);  // it cast its spirals

    bool won = false;
    for (int i = 0; i < 2000 && !won; ++i) {
        if (const Mob* wizard = sim.level().mobs.boss()) {
            sim.level().mobs.hit(wizard->hitbox(), 30, {1, 0}, sim.events());
        }
        sim.player().refillStats();
        sim.tick({});
        for (const auto& event : sim.events().take()) won = won || event.kind == GameEvent::Kind::BossDefeated;
    }
    EXPECT_TRUE(won);
    EXPECT_TRUE(sim.world().airWizardBeaten);
}

TEST_F(Rules, DyingLeavesADeathChestAndRespawnBringsThePlayerBack) {
    clearArea(3);
    standFacingRight();
    sim.inventory().add(ItemType::Gem, 5);
    sim.player().restoreStats(1, 10, 10, std::nullopt, 0);
    sim.player().takeHit(5, 0, 0, sim.events());
    sim.tick({});
    bool died = false;
    for (const auto& event : sim.events().take()) died = died || event.kind == GameEvent::Kind::PlayerDied;
    ASSERT_TRUE(died);
    EXPECT_TRUE(sim.inventory().empty());
    int deathChests = 0;
    for (const auto& piece : sim.level().furniture.all()) deathChests += piece.deathChest;
    EXPECT_EQ(deathChests, 1);

    const Vec2 before = sim.player().center();
    sim.tick({.moveX = 1});  // nothing happens while dead
    EXPECT_EQ(sim.player().center().x, before.x);
    sim.respawn();
    EXPECT_EQ(sim.player().health(), Player::kMaxHealth);
}
