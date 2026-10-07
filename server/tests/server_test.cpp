// The server and the game's NetworkClient, end to end: a real minicraft-server on localhost and real clients
// talking to it over WebSockets. The server runs one shared world: saying Hello puts you in it.
#include "client_socket.h"
#include "network_client.h"
#include "protocol.h"
#include "server.h"

#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>
#include <ixwebsocket/IXWebSocketServer.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <thread>

namespace {

using namespace std::chrono_literals;

// Polls the clients every few milliseconds until `done` holds or the time runs out.
bool pump(std::initializer_list<NetworkClient*> clients, const std::function<bool()>& done,
          std::chrono::milliseconds timeout = 5000ms) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        for (NetworkClient* client : clients) client->poll();
        if (done()) return true;
        std::this_thread::sleep_for(5ms);
    }
    return false;
}

bool sameTick(const TickInput& a, const TickInput& b) {
    if (a.tick != b.tick || a.turns.size() != b.turns.size()) return false;
    for (std::size_t i = 0; i < a.turns.size(); ++i) {
        if (a.turns[i].playerId != b.turns[i].playerId) return false;
    }
    return true;
}

// Collects a client's chat until `text` from `from` ("" = the server) shows up.
bool hears(NetworkClient& client, const std::string& from, const std::string& text,
           std::chrono::milliseconds timeout = 5000ms) {
    std::vector<protocol::ChatLine> heard;
    return pump({&client}, [&] {
        for (auto& line : client.takeChat()) heard.push_back(line);
        return std::any_of(heard.begin(), heard.end(),
                           [&](const protocol::ChatLine& l) { return l.from == from && l.text == text; });
    }, timeout);
}

// Every test gets its own server on its own port, running on a background thread.
class ServerTest : public ::testing::Test {
protected:
    void SetUp() override { startServer(Server::kDefaultResetAfterTicks); }
    void TearDown() override { stopServer(); }

    void startServer(int resetAfterTicks) {
        // A port of its own per test (ctest may run several test processes at once).
        const std::string name = ::testing::UnitTest::GetInstance()->current_test_info()->name();
        port = 30000 + static_cast<int>(std::hash<std::string>{}(name) % 20000);
        server = std::make_unique<Server>(resetAfterTicks);
        ASSERT_TRUE(server->start(static_cast<std::uint16_t>(port)));
        thread = std::jthread([this] { server->run(); });
    }
    void stopServer() {
        if (!server) return;
        server->stop();
        thread.join();
        server.reset();
    }

    std::string address() const { return "localhost:" + std::to_string(port); }

    // Connects under `name` and waits until the client is in the world.
    protocol::Joined join(NetworkClient& client, const std::string& name) {
        EXPECT_TRUE(client.connect(address(), name));
        std::optional<protocol::Joined> joined;
        EXPECT_TRUE(pump({&client}, [&] { return (joined = client.takeJoined()).has_value(); }));
        return joined.value_or(protocol::Joined{});
    }

    int port = 0;
    std::unique_ptr<Server> server;
    std::jthread thread;
};

TEST(NetworkClientUrl, BareAddressesBecomeWebSocketUrls) {
    EXPECT_EQ(NetworkClient::urlFor("localhost"), "ws://localhost:7777");
    EXPECT_EQ(NetworkClient::urlFor("192.168.1.20:9000"), "ws://192.168.1.20:9000");
    EXPECT_EQ(NetworkClient::urlFor("wss://minicraft.duckdns.org"), "wss://minicraft.duckdns.org");
    EXPECT_EQ(NetworkClient::urlFor("ws://localhost:1234"), "ws://localhost:1234");
}

TEST_F(ServerTest, HelloPutsPlayerInWorld) {
    NetworkClient alice;
    const protocol::Joined joined = join(alice, "Alice");
    EXPECT_EQ(alice.state(), NetworkClient::State::Online);
    EXPECT_GT(alice.playerId(), 0);
    EXPECT_TRUE(joined.history.empty());  // a brand new world
    // The first tick holds Alice's Join.
    ASSERT_TRUE(pump({&alice}, [&] { return alice.ticksWaiting() >= 1; }));
    const TickInput& first = alice.ticks().front();
    ASSERT_EQ(first.turns.size(), 1u);
    EXPECT_EQ(first.turns[0].playerId, alice.playerId());
    ASSERT_EQ(first.turns[0].commands.size(), 1u);
    EXPECT_EQ(first.turns[0].commands[0].kind, PlayerCommand::Kind::Join);
    EXPECT_EQ(first.turns[0].commands[0].text, "Alice");
}

TEST_F(ServerTest, SecondPlayerJoinsSameWorld) {
    NetworkClient alice;
    NetworkClient bob;
    const protocol::Joined joinedA = join(alice, "Alice");
    const protocol::Joined joinedB = join(bob, "Bob");
    EXPECT_EQ(joinedB.seed, joinedA.seed);
    EXPECT_TRUE(hears(alice, "", "Bob joined the game"));
}

TEST_F(ServerTest, BothPlayersGetSameTicks) {
    NetworkClient a;
    NetworkClient b;
    join(a, "Alice");
    join(b, "Bob");
    ASSERT_TRUE(pump({&a, &b}, [&] { return a.ticksWaiting() >= 60 && b.ticksWaiting() >= 30; }));
    // B joined later: its first tick is somewhere in A's queue. From there on, both get the same ticks.
    const auto& ticksA = a.ticks();
    const auto& ticksB = b.ticks();
    const auto start = std::find_if(ticksA.begin(), ticksA.end(),
                                    [&](const TickInput& t) { return t.tick == ticksB.front().tick; });
    ASSERT_NE(start, ticksA.end());
    const std::size_t offset = static_cast<std::size_t>(start - ticksA.begin());
    const std::size_t count = std::min(ticksA.size() - offset, ticksB.size());
    ASSERT_GE(count, 20u);
    for (std::size_t i = 0; i < count; ++i) EXPECT_TRUE(sameTick(ticksA[offset + i], ticksB[i])) << "tick " << i;
    EXPECT_EQ(ticksB.back().turns.size(), 2u);
}

TEST_F(ServerTest, ChatReachesEveryone) {
    NetworkClient a;
    NetworkClient b;
    join(a, "Alice");
    join(b, "Bob");
    a.sendChat("hello there");
    EXPECT_TRUE(hears(a, "Alice", "hello there"));
    EXPECT_TRUE(hears(b, "Alice", "hello there"));
}

TEST_F(ServerTest, LeaveIsAnnounced) {
    NetworkClient a;
    auto b = std::make_unique<NetworkClient>();
    join(a, "Alice");
    join(*b, "Bob");
    b.reset();  // Bob closes the tab
    EXPECT_TRUE(hears(a, "", "Bob left the game"));
}

TEST_F(ServerTest, LateJoinerGetsHistory) {
    NetworkClient a;
    NetworkClient b;
    join(a, "Alice");
    ASSERT_TRUE(pump({&a}, [&] { return a.ticksWaiting() >= 120; }));
    const protocol::Joined joined = join(b, "Bob");
    EXPECT_GE(joined.history.size(), 120u);
    EXPECT_EQ(joined.history.front().tick, 1);
}

TEST_F(ServerTest, InvalidNameIsRejected) {
    NetworkClient client;
    ASSERT_TRUE(client.connect(address(), "bad name!"));
    std::optional<std::string> error;
    ASSERT_TRUE(pump({&client}, [&] { return (error = client.takeError()).has_value(); }));
    EXPECT_EQ(*error, "Invalid name");
    EXPECT_FALSE(pump({&client}, [&] { return client.takeJoined().has_value(); }, 300ms));
}

TEST_F(ServerTest, DuplicateNameIsRejected) {
    NetworkClient alice;
    NetworkClient impostor;
    join(alice, "Alice");
    ASSERT_TRUE(impostor.connect(address(), "Alice"));
    std::optional<std::string> error;
    ASSERT_TRUE(pump({&impostor}, [&] { return (error = impostor.takeError()).has_value(); }));
    EXPECT_EQ(*error, "Name already in use");
    EXPECT_FALSE(pump({&impostor}, [&] { return impostor.takeJoined().has_value(); }, 300ms));
    // Another name works.
    join(impostor, "Alice2");
    EXPECT_GT(impostor.playerId(), 0);
}

TEST_F(ServerTest, EmptyWorldEnds) {
    auto alice = std::make_unique<NetworkClient>();
    const protocol::Joined first = join(*alice, "Alice");
    ASSERT_TRUE(pump({alice.get()}, [&] { return alice->ticksWaiting() >= 30; }));
    alice.reset();
    std::this_thread::sleep_for(200ms);  // the server hears about it
    NetworkClient bob;
    const protocol::Joined second = join(bob, "Bob");
    EXPECT_TRUE(second.history.empty());  // a new world, not Alice's
    EXPECT_NE(second.seed, first.seed);
}

TEST_F(ServerTest, WorldResetsAfterLimit) {
    stopServer();
    startServer(180);  // 3 seconds; the warning is due 60 s before the reset, so it comes right away
    NetworkClient a;
    NetworkClient b;
    auto c = std::make_unique<NetworkClient>();
    const protocol::Joined first = join(a, "Alice");
    join(b, "Bob");
    join(*c, "Carol");
    ASSERT_TRUE(pump({&a, &b, c.get()}, [&] { return a.ticksWaiting() >= 120; }));
    c.reset();  // Carol leaves just before the reset

    std::optional<protocol::Joined> againA;
    std::optional<protocol::Joined> againB;
    std::vector<protocol::ChatLine> heard;
    ASSERT_TRUE(pump({&a, &b}, [&] {
        for (auto& line : a.takeChat()) heard.push_back(line);
        if (!againA) againA = a.takeJoined();
        if (!againB) againB = b.takeJoined();
        return againA && againB;
    }));
    EXPECT_TRUE(std::any_of(heard.begin(), heard.end(),
                            [](const protocol::ChatLine& l) { return l.text == "The world resets in 1 minute!"; }));
    EXPECT_NE(againA->seed, first.seed);
    EXPECT_EQ(againA->seed, againB->seed);
    EXPECT_TRUE(againA->history.empty());
    // The old world's ticks are gone; the new world starts at tick 1 with Alice and Bob joining.
    ASSERT_TRUE(pump({&a, &b}, [&] { return a.ticksWaiting() >= 1; }));
    const TickInput& firstTick = a.ticks().front();
    EXPECT_EQ(firstTick.tick, 1);
    ASSERT_EQ(firstTick.turns.size(), 2u);
    for (const PlayerTurn& turn : firstTick.turns) {
        ASSERT_EQ(turn.commands.size(), 1u);
        EXPECT_EQ(turn.commands[0].kind, PlayerCommand::Kind::Join);
    }
}

TEST_F(ServerTest, DisconnectDuringTicksIsHarmless) {
    NetworkClient a;
    auto b = std::make_unique<NetworkClient>();
    join(a, "Alice");
    join(*b, "Bob");
    b.reset();
    const std::size_t before = a.ticksWaiting();
    ASSERT_TRUE(pump({&a}, [&] { return a.ticksWaiting() >= before + 30; }));
    // The server still takes new players.
    NetworkClient c;
    join(c, "Carol");
    EXPECT_GT(c.playerId(), 0);
}

TEST_F(ServerTest, UnresponsiveClientIsDropped) {
    NetworkClient alice;
    join(alice, "Alice");
    // Ghost's connection stays open but stops answering pings, like a laptop that went to sleep mid-game.
    ix::WebSocket ghost;
    ghost.setUrl("ws://localhost:" + std::to_string(port));
    ghost.disablePong();
    ghost.disableAutomaticReconnection();
    ghost.setOnMessageCallback([&ghost](const ix::WebSocketMessagePtr& message) {
        if (message->type != ix::WebSocketMessageType::Open) return;
        const auto bytes = protocol::encode(protocol::Hello{"Ghost"});
        ghost.sendBinary(std::string(bytes.begin(), bytes.end()));
    });
    ghost.start();
    ASSERT_TRUE(hears(alice, "", "Ghost joined the game"));
    // Once the server gives up on Ghost, he leaves the game.
    EXPECT_TRUE(hears(alice, "", "Ghost left the game", 25000ms));
    ghost.stop();
}

TEST(NetworkClientConnect, SilentServerReportsLost) {
    // A server that lets the player into a world and then goes quiet: no ticks, no close.
    ix::initNetSystem();
    ix::WebSocketServer silent(28778, "127.0.0.1");
    silent.setOnClientMessageCallback(
        [](std::shared_ptr<ix::ConnectionState>, ix::WebSocket& socket, const ix::WebSocketMessagePtr& message) {
            if (message->type != ix::WebSocketMessageType::Message) return;
            for (const auto& bytes : {protocol::encode(protocol::Welcome{1}), protocol::encode(protocol::Joined{.seed = 7})}) {
                socket.sendBinary(std::string(bytes.begin(), bytes.end()));
            }
        });
    ASSERT_TRUE(silent.listen().first);
    silent.start();

    NetworkClient client;
    ASSERT_TRUE(client.connect("ws://127.0.0.1:28778", "Alice"));
    ASSERT_TRUE(pump({&client}, [&] { return client.takeJoined().has_value(); }));
    EXPECT_TRUE(pump({&client}, [&] { return client.takeConnectionLost(); }, 15000ms));
    silent.stop();
}

TEST(NetworkClientConnect, UnreachableServerReportsLost) {
    NetworkClient client;
    ASSERT_TRUE(client.connect("localhost:1", "Alice"));
    EXPECT_TRUE(pump({&client}, [&] { return client.takeConnectionLost(); }, 6500ms));
    EXPECT_EQ(client.state(), NetworkClient::State::Offline);
}

TEST(ClientSocket, LargeJoinedRoundTrips) {
    // An echo server: whatever arrives goes straight back.
    ix::initNetSystem();
    ix::WebSocketServer echo(28777, "127.0.0.1");
    echo.setOnClientMessageCallback(
        [](std::shared_ptr<ix::ConnectionState>, ix::WebSocket& socket, const ix::WebSocketMessagePtr& message) {
            if (message->type == ix::WebSocketMessageType::Message) socket.sendBinary(message->str);
        });
    ASSERT_TRUE(echo.listen().first);
    echo.start();

    protocol::Joined joined{.seed = 42};
    for (int t = 1; t <= 6000; ++t) {
        joined.history.push_back({.tick = t, .turns = {{1, {.moveX = 1}, {}}, {2, {.moveY = -1}, {}}}});
    }
    const std::vector<std::uint8_t> bytes = protocol::encode(joined);
    ASSERT_GT(bytes.size(), 64u * 1024u);

    auto socket = makeClientSocket();
    socket->open("ws://127.0.0.1:28777");
    std::optional<protocol::Joined> back;
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!back && std::chrono::steady_clock::now() < deadline) {
        for (const SocketEvent& event : socket->poll()) {
            if (event.kind == SocketEvent::Kind::Opened) socket->send(bytes);
            if (event.kind == SocketEvent::Kind::Message) back = protocol::decode<protocol::Joined>(event.bytes);
        }
        std::this_thread::sleep_for(5ms);
    }
    echo.stop();
    ASSERT_TRUE(back.has_value());
    EXPECT_EQ(back->history.size(), 6000u);
    EXPECT_EQ(back->seed, 42u);
    EXPECT_EQ(back->history.back().turns[1].input.moveY, -1);
}

}  // namespace
