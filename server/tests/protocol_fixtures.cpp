// Byte fixtures of the game protocol, shared with the Go load-testing bots (loadtest/protocol/testdata): one file
// per message type, encoded here from fixed examples. The Go tests decode and re-encode the same files, so a format
// change on either side fails a test.
//
// To regenerate after an intended format change: run server_tests with WRITE_PROTOCOL_FIXTURES=1.
#include "protocol.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace {

std::vector<std::pair<std::string, std::vector<std::uint8_t>>> examples() {
    using namespace protocol;
    const TickInput tick{.tick = 42,
                         .turns = {{3, {.moveX = -1, .attack = true}, {PlayerCommand::craft(-1, 2)}}, {7, {}, {}}}};
    const Joined joined{.seed = 99,
                        .history = {{.tick = 1, .turns = {{7, {}, {PlayerCommand::join("Alice")}}}},
                                    {.tick = 2, .turns = {{7, {.moveX = 1}, {}}}}}};
    return {
        {"hello", encode(Hello{"Alice"})},
        {"input", encode(InputMessage{{.moveX = 1, .moveY = -1, .attack = true, .attackPressed = true}})},
        {"chat", encode(ChatMessage{"hi there"})},
        {"welcome", encode(Welcome{7})},
        {"joined", encode(joined)},
        {"tick", encode(TickMessage{tick})},
        {"chatline", encode(ChatLine{"Alice", "hello"})},
        {"error", encode(ErrorMessage{"Name already in use"})},
        {"observe", encode(Observe{"secret"})},
        {"probeping", encode(ProbePing{5})},
        {"probepong", encode(ProbePong{5})},
    };
}

std::filesystem::path fixtureDir() { return std::filesystem::path(MINICRAFT_SOURCE_DIR) / "loadtest/protocol/testdata"; }

TEST(ProtocolFixtures, MatchCommittedFiles) {
    const char* write = std::getenv("WRITE_PROTOCOL_FIXTURES");
    for (const auto& [name, bytes] : examples()) {
        const auto path = fixtureDir() / (name + ".bin");
        if (write && std::string(write) == "1") {
            std::filesystem::create_directories(fixtureDir());
            std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()),
                                                        static_cast<std::streamsize>(bytes.size()));
            continue;
        }
        std::ifstream file(path, std::ios::binary);
        ASSERT_TRUE(file) << "missing fixture " << path << " (run with WRITE_PROTOCOL_FIXTURES=1)";
        const std::vector<std::uint8_t> committed{std::istreambuf_iterator<char>(file), {}};
        EXPECT_EQ(committed, bytes) << name << ": the protocol changed; update the Go side and regenerate the fixtures";
    }
}

}  // namespace
