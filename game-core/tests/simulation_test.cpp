// The simulation is deterministic: the same seed and the same inputs give the same game, tick for tick. The
// server, bots and Python environment all depend on it (and client prediction will compare against it).
#include "simulation.h"

#include <gtest/gtest.h>

#include <vector>

namespace {

// A scripted session: walk around, punch, open nothing; the same script every time.
std::vector<PlayerInput> script(int ticks) {
    std::vector<PlayerInput> inputs;
    for (int t = 0; t < ticks; ++t) {
        PlayerInput input;
        const int phase = (t / 90) % 4;  // a slow square walk
        input.moveX = phase == 0 ? 1 : phase == 2 ? -1 : 0;
        input.moveY = phase == 1 ? 1 : phase == 3 ? -1 : 0;
        input.attack = (t / 45) % 3 == 0;
        input.attackPressed = t % 45 == 0;
        inputs.push_back(input);
    }
    return inputs;
}

std::uint64_t play(std::uint32_t seed, const std::vector<PlayerInput>& inputs, int night = 0) {
    Simulation sim;
    sim.startNewWorld(seed);
    sim.addPlayer(0, "Paul");
    if (night) sim.dayNight().setTime(DayNight::Time::Night);  // zombies, skeletons, creepers...
    for (const PlayerInput& input : inputs) sim.tick({.tick = sim.tickCount() + 1, .turns = {{0, input, {}}}});
    return sim.stateHash();
}

}  // namespace

TEST(Simulation, SameSeedAndInputsGiveTheSameGame) {
    const auto inputs = script(3000);  // 50 seconds of play
    EXPECT_EQ(play(1337, inputs), play(1337, inputs));
}

TEST(Simulation, SameSeedAndInputsGiveTheSameGameAtNight) {
    const auto inputs = script(3000);
    EXPECT_EQ(play(42, inputs, 1), play(42, inputs, 1));
}

TEST(Simulation, DifferentSeedsGiveDifferentGames) {
    const auto inputs = script(600);
    EXPECT_NE(play(1, inputs), play(2, inputs));
}

TEST(Simulation, DifferentInputsGiveDifferentGames) {
    auto inputs = script(301);
    const std::uint64_t original = play(7, inputs);
    inputs[300] = {.attackPressed = !inputs[300].attackPressed};  // one different keypress on the last tick
    EXPECT_NE(original, play(7, inputs));
}

TEST(Simulation, TicksAdvanceTheClockAndTimePlayed) {
    Simulation sim;
    sim.startNewWorld(3);
    const int start = sim.dayNight().tick();
    for (int i = 0; i < Simulation::kTicksPerSecond * 2; ++i) sim.tick({});  // nobody playing: time still passes
    EXPECT_EQ(sim.dayNight().tick(), start + 120);
    EXPECT_EQ(sim.secondsPlayed(), 2);
}

TEST(Simulation, PlayersStartOnTheSurfaceWithThePowerGlove) {
    Simulation sim;
    sim.startNewWorld(5);
    sim.tick({.tick = 1, .turns = {{7, {}, {PlayerCommand::join("Alice")}}}});
    const Player* alice = sim.findPlayer(7);
    ASSERT_NE(alice, nullptr);
    EXPECT_EQ(alice->name(), "Alice");
    EXPECT_EQ(alice->level(), World::kSurfaceIndex);
    EXPECT_EQ(alice->inventory().count(ItemType::PowerGlove), 1);
    EXPECT_EQ(alice->health(), Player::kMaxHealth);
}
