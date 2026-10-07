// Several players in one simulation: joining and leaving through commands, PvP, and the lockstep promise that two
// simulations fed the same inputs (as two clients are by the server) stay identical.
#include "simulation.h"

#include <gtest/gtest.h>

namespace {

constexpr int kAlice = 1;
constexpr int kBob = 2;
constexpr int kX = 128;
constexpr int kY = 128;

class Multiplayer : public ::testing::Test {
protected:
    void SetUp() override {
        sim.startNewWorld(99);
        sim.singlePlayer = false;
        tick({{kAlice, {}, {PlayerCommand::join("Alice")}}, {kBob, {}, {PlayerCommand::join("Bob")}}});
        Level& surface = sim.world().level(World::kSurfaceIndex);
        surface.mobs.clear();
        surface.mobs.enemySpawning = false;
        surface.mobs.animalSpawning = false;
        for (int y = kY - 6; y <= kY + 6; ++y) {
            for (int x = kX - 6; x <= kX + 6; ++x) surface.map.setTile(x, y, Tile::Grass);
        }
    }

    void tick(std::vector<PlayerTurn> turns) { sim.tick({.tick = sim.tickCount() + 1, .turns = std::move(turns)}); }
    Player& alice() { return *sim.findPlayer(kAlice); }
    Player& bob() { return *sim.findPlayer(kBob); }

    // Alice on (kX, kY) facing right at Bob, who stands on the next tile.
    void faceOff() {
        alice().setPosition(kX * 16.0f, kY * 16.0f - 3.0f);
        tick({{kAlice, {.moveX = 1}, {}}, {kBob, {}, {}}});
        alice().setPosition(kX * 16.0f, kY * 16.0f - 3.0f);
        bob().setPosition((kX + 1) * 16.0f, kY * 16.0f - 3.0f);
    }

    Simulation sim;
};

}  // namespace

TEST_F(Multiplayer, PlayersJoinAndLeaveWithCommands) {
    ASSERT_EQ(sim.players().size(), 2u);
    EXPECT_EQ(alice().name(), "Alice");
    EXPECT_EQ(bob().name(), "Bob");
    tick({{kBob, {}, {PlayerCommand::leave()}}});
    EXPECT_EQ(sim.players().size(), 1u);
    EXPECT_EQ(sim.findPlayer(kBob), nullptr);
}

TEST_F(Multiplayer, APunchHurtsAnotherPlayer) {
    faceOff();
    alice().refillStats();
    tick({{kAlice, {.attack = true, .attackPressed = true}, {}}, {kBob, {}, {}}});
    EXPECT_LT(bob().health(), Player::kMaxHealth);
    EXPECT_EQ(alice().health(), Player::kMaxHealth);
}

TEST_F(Multiplayer, AnArrowHitsAnotherPlayerButNotItsShooter) {
    faceOff();
    bob().setPosition((kX + 4) * 16.0f, kY * 16.0f - 3.0f);
    alice().inventory().add(ItemType::Arrow, 3);
    alice().setHeldItem(Inventory::Stack{ItemType::WoodBow, 1, maxDurability(ItemType::WoodBow)});
    tick({{kAlice, {.attack = true, .attackPressed = true}, {}}, {kBob, {}, {}}});
    for (int i = 0; i < 20; ++i) tick({{kAlice, {}, {}}, {kBob, {}, {}}});
    EXPECT_LT(bob().health(), Player::kMaxHealth);
    EXPECT_EQ(alice().health(), Player::kMaxHealth);
}

TEST_F(Multiplayer, PlayersCanBeOnDifferentLevels) {
    sim.changeLevel(bob(), 2, false);
    EXPECT_EQ(bob().level(), 2);
    EXPECT_EQ(alice().level(), World::kSurfaceIndex);
    // Both levels are simulated: mobs can spawn down in the cave where Bob is.
    sim.world().level(2).mobs.enemySpawning = true;
    for (int i = 0; i < 600; ++i) tick({{kAlice, {}, {}}, {kBob, {}, {}}});
    EXPECT_GT(sim.world().level(2).mobs.enemyCount(), 0);
}

TEST_F(Multiplayer, ABedOnlySetsTheRespawnPoint) {
    faceOff();
    ASSERT_TRUE(sim.world().level(World::kSurfaceIndex).furniture.place(ItemType::Bed, kX + 1, kY + 2,
                                                                       sim.world().level(World::kSurfaceIndex).map, {}));
    bob().setPosition(kX * 16.0f, kY * 16.0f - 3.0f);  // out of the way
    alice().setPosition((kX + 1) * 16.0f, (kY + 1) * 16.0f - 3.0f);
    tick({{kAlice, {.moveY = 1}, {}}, {kBob, {}, {}}});  // face down, at the bed
    alice().setPosition((kX + 1) * 16.0f, (kY + 1) * 16.0f - 3.0f);
    const int timeBefore = sim.dayNight().tick();
    tick({{kAlice, {}, {PlayerCommand::useFurniture()}}, {kBob, {}, {}}});
    EXPECT_EQ(alice().spawnLevel(), World::kSurfaceIndex);
    EXPECT_EQ(sim.dayNight().tick(), timeBefore + 1);  // the night didn't skip for everyone
}

TEST(Lockstep, TwoClientsGivenTheSameInputsStayIdentical) {
    // What the server does: the same TickInputs, in the same order, to two clients.
    Simulation clientA;
    Simulation clientB;
    clientA.startNewWorld(2024);
    clientB.startNewWorld(2024);
    clientA.singlePlayer = clientB.singlePlayer = false;
    std::vector<TickInput> inputs;
    inputs.push_back({1, {{1, {}, {PlayerCommand::join("Alice")}}, {2, {}, {PlayerCommand::join("Bob")}}}});
    for (int t = 2; t < 2400; ++t) {
        PlayerInput a{.moveX = (t / 60) % 2 == 0 ? 1 : -1, .attack = t % 50 < 10, .attackPressed = t % 50 == 0};
        PlayerInput b{.moveY = (t / 45) % 2 == 0 ? 1 : -1, .attack = t % 70 < 5, .attackPressed = t % 70 == 0};
        inputs.push_back({t, {{1, a, {}}, {2, b, {}}}});
    }
    for (const TickInput& input : inputs) {
        clientA.tick(input);
        clientB.tick(input);
    }
    EXPECT_EQ(clientA.stateHash(), clientB.stateHash());

    // A third client joining late replays the same history and catches up exactly.
    Simulation latecomer;
    latecomer.startNewWorld(2024);
    latecomer.singlePlayer = false;
    for (const TickInput& input : inputs) latecomer.tick(input);
    EXPECT_EQ(latecomer.stateHash(), clientA.stateHash());
}

TEST(Lockstep, AReusedSimulationMatchesAFreshOne) {
    // A player who leaves the world and joins again keeps their Simulation: starting the new world must leave
    // nothing behind from the old one, or they'd go out of sync with everyone else.
    std::vector<TickInput> inputs;
    inputs.push_back({1, {{1, {}, {PlayerCommand::join("Alice")}}}});
    for (int t = 2; t < 1200; ++t) {
        PlayerInput a{.moveX = (t / 60) % 2 == 0 ? 1 : -1, .attack = t % 50 < 10, .attackPressed = t % 50 == 0};
        inputs.push_back({t, {{1, a, {}}}});
    }

    Simulation reused;
    reused.startNewWorld(7);
    reused.singlePlayer = false;
    for (const TickInput& input : inputs) reused.tick(input);  // the first visit

    reused.startNewWorld(2024);  // ... and the world joined the second time
    Simulation fresh;
    fresh.startNewWorld(2024);
    fresh.singlePlayer = false;
    for (const TickInput& input : inputs) {
        reused.tick(input);
        fresh.tick(input);
    }
    EXPECT_EQ(reused.stateHash(), fresh.stateHash());
}
