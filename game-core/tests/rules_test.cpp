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
        sim.addPlayer(0, "Paul");
        goToLevel(World::kSurfaceIndex);
    }

    Player& me() { return *sim.findPlayer(0); }
    Level& level() { return sim.levelOf(me()); }
    // One tick with this player's input (and commands).
    void tick(PlayerInput input = {}, std::vector<PlayerCommand> commands = {}) {
        sim.tick({.tick = sim.tickCount() + 1, .turns = {{0, input, std::move(commands)}}});
    }

    // Switches level with spawning off, so only the mobs a test adds are there.
    void goToLevel(int index) {
        sim.changeLevel(me(), index, false);
        level().mobs.clear();
        level().mobs.enemySpawning = false;
        level().mobs.animalSpawning = false;
        sim.events().take();
    }
    TileMap& map() { return level().map; }
    Tile target() { return map().tileAt(kX + 1, kY); }

    // A square of `tile` around the test spot.
    void clearArea(int radius, Tile tile = Tile::Dirt) {
        for (int y = kY - radius; y <= kY + radius; ++y) {
            for (int x = kX - radius; x <= kX + radius; ++x) map().setTile(x, y, tile);
        }
    }
    // The player on (kX, kY), facing right: the tile in front is (kX + 1, kY).
    void standFacingRight() {
        me().setPosition(kX * 16.0f, kY * 16.0f - 3.0f);
        tick({.moveX = 1});
        me().setPosition(kX * 16.0f, kY * 16.0f - 3.0f);
    }
    void hold(ItemType type, int count = 1) {
        me().setHeldItem(Inventory::Stack{type, count, maxDurability(type)});
    }
    // Space, with full stats so energy never runs out mid-test.
    void use() {
        me().refillStats();
        tick({.attack = true, .attackPressed = true});
    }
    // Lets the drops settle, then walks all over the tile in front, where they fell, to pick them up.
    void collectDrops() {
        const Rect start = me().bounds();
        Player* players[] = {&me()};
        Events events;  // pickups record stats events; not what these tests look at
        for (int i = 0; i < 40; ++i) level().drops.tick(map(), players, events);  // too fresh to pick up yet
        for (int dy = -8; dy <= 8; dy += 2) {
            for (int dx = -8; dx <= 8; dx += 2) {
                me().setPosition((kX + 1) * 16.0f + static_cast<float>(dx), kY * 16.0f - 3.0f + static_cast<float>(dy));
                level().drops.tick(map(), players, events);
            }
        }
        me().setPosition(start.x, start.y);
    }
    void tickIdle(int ticks) {
        for (int i = 0; i < ticks; ++i) tick();
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
    me().setHeldItem(std::nullopt);
    use();
    EXPECT_EQ(target(), Tile::IronOre);
    EXPECT_EQ(map().damageAt(kX + 1, kY), 0);

    hold(ItemType::RockPickaxe);
    for (int swing = 0; swing < 20 && target() == Tile::IronOre; ++swing) use();
    collectDrops();
    EXPECT_EQ(target(), Tile::Dirt);
    EXPECT_GE(me().inventory().count(ItemType::IronOre), 2);
}

TEST_F(Rules, TheFurnaceSmeltsIronAndTheAnvilMakesTheBetterTools) {
    me().inventory().add(ItemType::IronOre, 3);
    me().inventory().add(ItemType::Coal, 1);
    const auto furnace = Recipe::stationRecipes(ItemType::Furnace);
    ASSERT_EQ(furnace[0].product(), ItemType::Iron);
    tick({}, {PlayerCommand::craft(static_cast<int>(ItemType::Furnace), 0)});
    EXPECT_EQ(me().inventory().count(ItemType::Iron), 1);
    EXPECT_EQ(Recipe::stationRecipes(ItemType::Anvil).size(), 22u);
    tick({}, {PlayerCommand::craft(static_cast<int>(ItemType::Furnace), 0)});  // out of ore: nothing happens
    EXPECT_EQ(me().inventory().count(ItemType::Iron), 1);
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
    me().restoreStats(10, 1, 10, std::nullopt, 0);  // enough to swing, not to dig
    tick({.attack = true, .attackPressed = true});
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
    EXPECT_EQ(me().heldItem()->count, 2);

    for (int i = 0; i < 100000 && map().dataAt(kX + 1, kY) < TileMap::kWheatRipeAge; ++i) {
        map().tickRandomTiles(kX + 1, kY, 0, 1, sim.rng());
    }
    ASSERT_EQ(map().dataAt(kX + 1, kY), TileMap::kWheatRipeAge);
    me().setHeldItem(std::nullopt);
    use();
    collectDrops();
    EXPECT_GE(me().inventory().count(ItemType::Wheat), 2);
    EXPECT_EQ(target(), Tile::Dirt);
}

TEST_F(Rules, AnAcornGrowsIntoATree) {
    clearArea(3, Tile::Grass);
    standFacingRight();
    hold(ItemType::Acorn);
    use();
    ASSERT_EQ(target(), Tile::Sapling);
    EXPECT_FALSE(me().heldItem());
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
    me().setHeldItem(std::nullopt);
    use();
    collectDrops();
    EXPECT_EQ(target(), Tile::Dirt);
    EXPECT_GE(me().inventory().count(ItemType::Torch), 1);
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
    me().setHeldItem(std::nullopt);
    use();
    EXPECT_FALSE(map().isSolidAt(kX + 1, kY));
}

TEST_F(Rules, ABowShootsAnArrowFromTheInventory) {
    clearArea(6);
    level().mobs.add(std::make_unique<Zombie>((kX + 4) * 16.0f, kY * 16.0f - 3.0f, 2, sim.rng()));
    const int health = level().mobs.all()[0]->health();
    standFacingRight();
    me().inventory().add(ItemType::Arrow, 5);
    const int arrows = me().inventory().count(ItemType::Arrow);
    hold(ItemType::IronBow);
    use();
    EXPECT_EQ(me().inventory().count(ItemType::Arrow), arrows - 1);
    tickIdle(20);
    EXPECT_LT(level().mobs.all()[0]->health(), health);
}

TEST_F(Rules, FoodFillsHungerAndArmourSoaksUpHits) {
    Player& player = me();
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
    me().restoreStats(10, 10, 10, std::nullopt, 0);
    for (int i = 0; i < 60 * 180; ++i) {  // 3 minutes of walking back and forth
        if (i % 120 == 0) me().setPosition(kX * 16.0f - 32, kY * 16.0f - 3);
        tick({.moveX = 1});
    }
    EXPECT_LT(me().hunger(), 10);
    EXPECT_GE(me().hunger(), 6);
}

TEST_F(Rules, ThePowerGloveOnlyLiftsEmptyChests) {
    clearArea(3);
    standFacingRight();
    ASSERT_TRUE(level().furniture.place(ItemType::Chest, kX + 1, kY, map(), {}));
    level().furniture.at(kX + 1, kY)->contents.add(ItemType::Gem, 3);
    hold(ItemType::PowerGlove);
    use();
    EXPECT_TRUE(notified("Empty the chest first!"));
    ASSERT_NE(level().furniture.at(kX + 1, kY), nullptr);

    level().furniture.at(kX + 1, kY)->contents.clear();
    use();
    EXPECT_EQ(level().furniture.at(kX + 1, kY), nullptr);
    EXPECT_EQ(me().heldItem()->type, ItemType::Chest);
    use();
    EXPECT_NE(level().furniture.at(kX + 1, kY), nullptr);
}

TEST_F(Rules, ACreeperBlowsUpNextToThePlayer) {
    goToLevel(3);  // cave B2
    // A corridor, so the creeper can only come at the player.
    clearArea(6, Tile::Rock);
    for (int x = kX - 5; x <= kX + 5; ++x) map().setTile(x, kY, Tile::Dirt);
    standFacingRight();
    me().restoreStats(10, 10, 10, std::nullopt, 0);
    level().mobs.add(std::make_unique<Creeper>((kX + 2) * 16.0f, kY * 16.0f - 3.0f, 1, sim.rng()));
    for (int i = 0; i < 600 && level().mobs.count(MobKind::Creeper) > 0; ++i) {
        me().setPosition(kX * 16.0f, kY * 16.0f - 3.0f);
        tick();
    }
    EXPECT_EQ(level().mobs.count(MobKind::Creeper), 0);
    EXPECT_LT(me().health(), 10);
    int holes = 0;
    for (int x = kX - 2; x <= kX + 5; ++x) holes += map().tileAt(x, kY) == Tile::Hole;
    EXPECT_GT(holes, 0);
}

TEST_F(Rules, ASkeletonShootsAtThePlayer) {
    goToLevel(3);
    clearArea(8);
    standFacingRight();
    level().mobs.add(std::make_unique<Skeleton>((kX + 5) * 16.0f, kY * 16.0f - 3.0f, 2, sim.rng()));
    int shots = 0;
    for (int i = 0; i < 600; ++i) {
        const std::size_t before = level().projectiles.arrows().size();
        me().refillStats();
        me().setPosition(kX * 16.0f, kY * 16.0f - 3.0f);
        tick();
        if (level().projectiles.arrows().size() > before) ++shots;
    }
    EXPECT_GT(shots, 0);
}

TEST_F(Rules, BeatingTheAirWizardWinsTheGame) {
    goToLevel(World::kSkyIndex);
    sim.world().spawnBoss(sim.rng());
    const Mob* boss = level().mobs.boss();
    ASSERT_NE(boss, nullptr);
    me().setPosition(boss->center().x - 30, boss->center().y - 11);
    std::size_t sparks = 0;
    for (int i = 0; i < 600; ++i) {
        me().refillStats();
        tick();
        sparks = std::max(sparks, level().projectiles.sparks().size());
    }
    EXPECT_GT(sparks, 10u);  // it cast its spirals

    bool won = false;
    for (int i = 0; i < 2000 && !won; ++i) {
        if (const Mob* wizard = level().mobs.boss()) {
            level().mobs.hit(wizard->hitbox(), 30, {1, 0}, sim.events());
        }
        me().refillStats();
        tick();
        for (const auto& event : sim.events().take()) won = won || event.kind == GameEvent::Kind::BossDefeated;
    }
    EXPECT_TRUE(won);
    EXPECT_TRUE(sim.world().airWizardBeaten);
}

TEST_F(Rules, DyingLeavesADeathChestAndRespawnBringsThePlayerBack) {
    clearArea(3);
    standFacingRight();
    me().inventory().add(ItemType::Gem, 5);
    me().restoreStats(1, 10, 10, std::nullopt, 0);
    me().takeHit(5, 0, 0, sim.events());
    tick();
    bool died = false;
    for (const auto& event : sim.events().take()) died = died || event.kind == GameEvent::Kind::PlayerDied;
    ASSERT_TRUE(died);
    EXPECT_TRUE(me().inventory().empty());
    int deathChests = 0;
    for (const auto& piece : level().furniture.all()) deathChests += piece.deathChest;
    EXPECT_EQ(deathChests, 1);

    const Vec2 before = me().center();
    tick({.moveX = 1});  // nothing happens while dead
    EXPECT_EQ(me().center().x, before.x);
    tick({}, {PlayerCommand::respawn()});
    EXPECT_EQ(me().health(), Player::kMaxHealth);
}
