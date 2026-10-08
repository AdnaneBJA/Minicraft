// The stat events: what players did (tiles broken, items collected and crafted, kills, deaths, levels reached),
// credited to the right player. The game server replays the world and reports them to the stats dashboard.
#include "mob.h"
#include "recipe.h"
#include "simulation.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <functional>
#include <memory>

namespace {

constexpr int kAlice = 1;
constexpr int kBob = 2;
constexpr int kX = 128;
constexpr int kY = 128;

class Stats : public ::testing::Test {
protected:
    void SetUp() override {
        sim.startNewWorld(99);
        sim.singlePlayer = false;
        tick({{kAlice, {}, {PlayerCommand::join("Alice")}}});
        Level& surface = sim.world().level(World::kSurfaceIndex);
        surface.mobs.clear();
        surface.mobs.enemySpawning = false;
        surface.mobs.animalSpawning = false;
        for (int y = kY - 8; y <= kY + 8; ++y) {
            for (int x = kX - 8; x <= kX + 8; ++x) surface.map.setTile(x, y, Tile::Grass);
        }
        // Alice on (kX, kY), facing right.
        alice().setPosition(kX * 16.0f, kY * 16.0f - 3.0f);
        tick({{kAlice, {.moveX = 1}, {}}});
        alice().setPosition(kX * 16.0f, kY * 16.0f - 3.0f);
        sim.events().take();
    }

    // One tick; returns the stat events it recorded (and keeps all of them in `seen`).
    std::vector<GameEvent> tick(std::vector<PlayerTurn> turns) {
        sim.tick({.tick = sim.tickCount() + 1, .turns = std::move(turns)});
        std::vector<GameEvent> stats;
        for (GameEvent& event : sim.events().take()) {
            if (isStatEvent(event.kind)) stats.push_back(event);
        }
        seen.insert(seen.end(), stats.begin(), stats.end());
        return stats;
    }
    // Alice presses Space every few ticks, for up to `ticks` ticks or until `done`.
    void attackUntil(int ticks, const std::function<bool()>& done, int who = kAlice) {
        for (int t = 0; t < ticks && !done(); ++t) {
            const bool press = t % 8 == 0;
            tick({{who, {.attack = press, .attackPressed = press}, {}}});
        }
    }
    bool saw(GameEvent::Kind kind) const {
        return std::any_of(seen.begin(), seen.end(), [&](const GameEvent& e) { return e.kind == kind; });
    }
    const GameEvent* first(GameEvent::Kind kind) const {
        const auto it = std::find_if(seen.begin(), seen.end(), [&](const GameEvent& e) { return e.kind == kind; });
        return it == seen.end() ? nullptr : &*it;
    }
    Level& surface() { return sim.world().level(World::kSurfaceIndex); }
    Player& alice() { return *sim.findPlayer(kAlice); }

    Simulation sim;
    std::vector<GameEvent> seen;
};

TEST_F(Stats, ChoppingATreeRecordsTileBrokenAndCollection) {
    surface().map.setTile(kX + 1, kY, Tile::Tree);
    alice().setHeldItem(Inventory::Stack{ItemType::GemAxe, 1, 500});
    attackUntil(1200, [&] { return saw(GameEvent::Kind::TileBroken); });
    const GameEvent* broken = first(GameEvent::Kind::TileBroken);
    ASSERT_NE(broken, nullptr);
    EXPECT_EQ(broken->value, static_cast<int>(Tile::Tree));
    EXPECT_EQ(broken->player, kAlice);

    // Walk onto the wood and wait for it to be picked up.
    for (int t = 0; t < 120; ++t) tick({{kAlice, {.moveX = t < 20 ? 1 : 0}, {}}});
    int wood = 0;
    for (const GameEvent& e : seen) {
        if (e.kind == GameEvent::Kind::ItemCollected && e.value == static_cast<int>(ItemType::Wood)) {
            EXPECT_EQ(e.player, kAlice);
            wood += e.count;
        }
    }
    EXPECT_GE(wood, 1);
}

TEST_F(Stats, SwordKillCreditsThePlayer) {
    surface().mobs.add(std::make_unique<Zombie>((kX + 1) * 16.0f, kY * 16.0f, 1, sim.rng()));
    alice().setHeldItem(Inventory::Stack{ItemType::GemSword, 1, 500});
    attackUntil(1200, [&] { return saw(GameEvent::Kind::MobKilled); });
    const GameEvent* killed = first(GameEvent::Kind::MobKilled);
    ASSERT_NE(killed, nullptr);
    EXPECT_EQ(killed->value, static_cast<int>(MobKind::Zombie));
    EXPECT_EQ(killed->count, 1);  // the zombie's level
    EXPECT_EQ(killed->player, kAlice);
}

TEST_F(Stats, ArrowKillCreditsTheShooter) {
    surface().mobs.add(std::make_unique<Zombie>((kX + 4) * 16.0f, kY * 16.0f, 1, sim.rng()));
    alice().setHeldItem(Inventory::Stack{ItemType::GemBow, 1, 500});
    alice().inventory().add(ItemType::Arrow, 200);
    attackUntil(1200, [&] { return saw(GameEvent::Kind::MobKilled); });
    const GameEvent* killed = first(GameEvent::Kind::MobKilled);
    ASSERT_NE(killed, nullptr);
    EXPECT_EQ(killed->player, kAlice);
}

TEST_F(Stats, LavaDeathIsEnvironmental) {
    const int joinedAt = 1;  // Alice joined on tick 1
    const Vec2 c = alice().center();
    surface().map.setTile(static_cast<int>(c.x) / 16, static_cast<int>(c.y) / 16, Tile::Lava);
    for (int t = 0; t < 3000 && !saw(GameEvent::Kind::PlayerKilled); ++t) tick({{kAlice, {}, {}}});
    const GameEvent* died = first(GameEvent::Kind::PlayerKilled);
    ASSERT_NE(died, nullptr);
    EXPECT_EQ(died->player, kAlice);
    EXPECT_EQ(died->killer.kind, DamageSource::Kind::Environment);
    EXPECT_EQ(died->value, (sim.tickCount() - joinedAt) / Simulation::kTicksPerSecond);
}

TEST_F(Stats, PvpKillCreditsTheKiller) {
    tick({{kAlice, {}, {}}, {kBob, {}, {PlayerCommand::join("Bob")}}});
    Player& bob = *sim.findPlayer(kBob);
    bob.setPosition((kX + 1) * 16.0f, kY * 16.0f - 3.0f);
    alice().setHeldItem(Inventory::Stack{ItemType::GemSword, 1, 500});
    for (int t = 0; t < 1200 && !saw(GameEvent::Kind::PlayerKilled); ++t) {
        const bool press = t % 8 == 0;
        bob.setPosition((kX + 1) * 16.0f, kY * 16.0f - 3.0f);  // Bob stays in reach
        tick({{kAlice, {.attack = press, .attackPressed = press}, {}}, {kBob, {}, {}}});
    }
    const GameEvent* died = first(GameEvent::Kind::PlayerKilled);
    ASSERT_NE(died, nullptr);
    EXPECT_EQ(died->player, kBob);
    EXPECT_EQ(died->killer.kind, DamageSource::Kind::Player);
    EXPECT_EQ(died->killer.id, kAlice);
}

TEST_F(Stats, CraftingRecordsTheProduct) {
    alice().inventory().add(ItemType::Wood, 20);
    const auto recipes = Recipe::personalRecipes();
    const auto it = std::find_if(recipes.begin(), recipes.end(),
                                 [](const Recipe& r) { return r.product() == ItemType::Workbench; });
    ASSERT_NE(it, recipes.end());
    tick({{kAlice, {}, {PlayerCommand::craft(-1, static_cast<int>(it - recipes.begin()))}}});
    const GameEvent* crafted = first(GameEvent::Kind::ItemCrafted);
    ASSERT_NE(crafted, nullptr);
    EXPECT_EQ(crafted->player, kAlice);
    EXPECT_EQ(crafted->value, static_cast<int>(ItemType::Workbench));
    EXPECT_EQ(crafted->count, it->amount());
}

TEST_F(Stats, EnteringACaveRecordsLevelReachedOncePerLife) {
    const int cave = World::kSurfaceIndex + 1;
    sim.changeLevel(alice(), cave, false);
    sim.changeLevel(alice(), World::kSurfaceIndex, false);
    sim.changeLevel(alice(), cave, false);
    tick({{kAlice, {}, {}}});
    int reached = 0;
    for (const GameEvent& e : seen) {
        if (e.kind == GameEvent::Kind::LevelReached) {
            EXPECT_EQ(e.value, cave);
            EXPECT_EQ(e.player, kAlice);
            ++reached;
        }
    }
    EXPECT_EQ(reached, 1);
}

TEST_F(Stats, BossDefeatIsForEveryoneAndCreditsTheKiller) {
    // Everyone gets the win screen (the event is for every player), and the stats learn who landed the last hit.
    sim.changeLevel(alice(), World::kSkyIndex, false);
    Level& sky = sim.world().level(World::kSkyIndex);
    sky.mobs.clear();
    auto boss = std::make_unique<AirWizard>(alice().center().x + 40.0f, alice().center().y, sim.rng());
    boss->setLastHitBy(kAlice);
    boss->kill();
    sky.mobs.add(std::move(boss));
    tick({{kAlice, {}, {}}});
    const GameEvent* defeated = first(GameEvent::Kind::BossDefeated);
    ASSERT_NE(defeated, nullptr);
    EXPECT_EQ(defeated->player, -1);  // not addressed to one player: every client shows the win screen
    EXPECT_EQ(defeated->killer.kind, DamageSource::Kind::Player);
    EXPECT_EQ(defeated->killer.id, kAlice);
}

TEST_F(Stats, CreeperBlastIsNotASelfKill) {
    // Alice hit the creeper (it's "hers"), then it blew up next to her and Bob. Bob was hurt by Alice's creeper; Alice
    // was hurt by a creeper, not by herself. And a creeper blowing itself up isn't anyone's kill.
    tick({{kAlice, {}, {}}, {kBob, {}, {PlayerCommand::join("Bob")}}});
    Player& bob = *sim.findPlayer(kBob);
    bob.setPosition(kX * 16.0f, (kY + 1) * 16.0f - 3.0f);
    auto creeper = std::make_unique<Creeper>((kX + 1) * 16.0f, kY * 16.0f, 1, sim.rng());
    creeper->setLastHitBy(kAlice);
    surface().mobs.add(std::move(creeper));
    for (int t = 0; t < 600 && surface().mobs.count(MobKind::Creeper) > 0; ++t) {
        bob.setPosition(kX * 16.0f, (kY + 1) * 16.0f - 3.0f);
        alice().setPosition(kX * 16.0f, kY * 16.0f - 3.0f);
        tick({{kAlice, {}, {}}, {kBob, {}, {}}});
    }
    ASSERT_EQ(surface().mobs.count(MobKind::Creeper), 0) << "the creeper never blew up";
    EXPECT_EQ(alice().lastDamage().kind, DamageSource::Kind::Mob);
    EXPECT_EQ(alice().lastDamage().id, static_cast<int>(MobKind::Creeper));
    EXPECT_EQ(bob.lastDamage().kind, DamageSource::Kind::Player);
    EXPECT_EQ(bob.lastDamage().id, kAlice);
    EXPECT_FALSE(saw(GameEvent::Kind::MobKilled));
}

TEST(StatEvents, AreDeterministic) {
    // Two copies of the world fed the same ticks record the same stat events: the server's replay counts exactly
    // what the players saw.
    const auto run = [] {
        Simulation sim;
        sim.startNewWorld(2024);
        sim.singlePlayer = false;
        std::vector<GameEvent> stats;
        for (int t = 1; t < 2400; ++t) {
            TickInput input{.tick = t};
            if (t == 1) {
                input.turns = {{1, {}, {PlayerCommand::join("Alice")}}, {2, {}, {PlayerCommand::join("Bob")}}};
            } else {
                PlayerInput a{.moveX = (t / 60) % 2 == 0 ? 1 : -1, .attack = t % 50 < 10, .attackPressed = t % 50 == 0};
                PlayerInput b{.moveY = (t / 45) % 2 == 0 ? 1 : -1, .attack = t % 70 < 5, .attackPressed = t % 70 == 0};
                input.turns = {{1, a, {}}, {2, b, {}}};
            }
            sim.tick(input);
            for (GameEvent& e : sim.events().take()) {
                if (isStatEvent(e.kind)) stats.push_back(e);
            }
        }
        return stats;
    };
    const auto a = run();
    const auto b = run();
    ASSERT_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        EXPECT_EQ(a[i].kind, b[i].kind);
        EXPECT_EQ(a[i].player, b[i].player);
        EXPECT_EQ(a[i].value, b[i].value);
        EXPECT_EQ(a[i].count, b[i].count);
    }
}

}  // namespace
