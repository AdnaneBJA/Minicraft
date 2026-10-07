// The server and the game's NetworkClient, end to end: a real minicraft-server on localhost and real clients
// talking to it over WebSockets.
#include "client_socket.h"
#include "network_client.h"
#include "protocol.h"
#include "server.h"

#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocketServer.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <functional>
#include <initializer_list>
#include <memory>
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

// Every test gets its own server on its own port, running on a background thread.
class ServerTest : public ::testing::Test {
protected:
    void SetUp() override {
        static int nextPort = 27777;
        port = nextPort++;
        ASSERT_TRUE(server.start(static_cast<std::uint16_t>(port)));
        thread = std::jthread([this] { server.run(); });
    }
    void TearDown() override {
        server.stop();
        thread.join();
    }

    std::string address() const { return "localhost:" + std::to_string(port); }

    // Connects a client under `name` and waits until it sees the lobby list.
    void connect(NetworkClient& client, const std::string& name, std::vector<protocol::LobbyInfo>* lobbies = nullptr) {
        ASSERT_TRUE(client.connect(address(), name));
        std::optional<std::vector<protocol::LobbyInfo>> list;
        ASSERT_TRUE(pump({&client}, [&] { return (list = client.takeLobbies()).has_value(); }));
        if (lobbies) *lobbies = *list;
    }

    // A creates a world and B joins it; both are in it once they have their Joined.
    void createAndJoin(NetworkClient& a, NetworkClient& b) {
        connect(a, "Alice");
        connect(b, "Bob");
        a.createLobby();
        std::optional<protocol::Joined> joinedA;
        ASSERT_TRUE(pump({&a, &b}, [&] { return (joinedA = a.takeJoined()).has_value(); }));
        std::optional<std::vector<protocol::LobbyInfo>> list;
        ASSERT_TRUE(pump({&a, &b}, [&] {
            list = b.takeLobbies();
            return list && !list->empty();
        }));
        b.joinLobby(joinedA->lobbyId);
        std::optional<protocol::Joined> joinedB;
        ASSERT_TRUE(pump({&a, &b}, [&] { return (joinedB = b.takeJoined()).has_value(); }));
        EXPECT_EQ(joinedB->lobbyId, joinedA->lobbyId);
        EXPECT_EQ(joinedB->seed, joinedA->seed);
    }

    int port = 0;
    Server server;
    std::jthread thread;
};

TEST(NetworkClientUrl, BareAddressesBecomeWebSocketUrls) {
    EXPECT_EQ(NetworkClient::urlFor("localhost"), "ws://localhost:7777");
    EXPECT_EQ(NetworkClient::urlFor("192.168.1.20:9000"), "ws://192.168.1.20:9000");
    EXPECT_EQ(NetworkClient::urlFor("wss://minicraft.duckdns.org"), "wss://minicraft.duckdns.org");
    EXPECT_EQ(NetworkClient::urlFor("ws://localhost:1234"), "ws://localhost:1234");
}

TEST_F(ServerTest, ConnectGetsWelcomeAndLobbyList) {
    NetworkClient client;
    std::vector<protocol::LobbyInfo> lobbies;
    connect(client, "Alice", &lobbies);
    EXPECT_EQ(client.state(), NetworkClient::State::Online);
    EXPECT_GT(client.playerId(), 0);
    EXPECT_TRUE(lobbies.empty());
}

TEST_F(ServerTest, CreateAndJoinLobby) {
    NetworkClient a;
    NetworkClient b;
    createAndJoin(a, b);
}

TEST_F(ServerTest, BothMembersGetSameTicks) {
    NetworkClient a;
    NetworkClient b;
    createAndJoin(a, b);
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
    // ... and those ticks hold both players.
    EXPECT_EQ(ticksB.back().turns.size(), 2u);
}

TEST_F(ServerTest, ChatReachesLobby) {
    NetworkClient a;
    NetworkClient b;
    createAndJoin(a, b);
    a.takeChat();
    b.takeChat();
    a.sendChat("hello there");
    std::vector<protocol::ChatLine> heardA;
    std::vector<protocol::ChatLine> heardB;
    const auto heard = [](const std::vector<protocol::ChatLine>& lines) {
        return std::any_of(lines.begin(), lines.end(),
                           [](const protocol::ChatLine& l) { return l.from == "Alice" && l.text == "hello there"; });
    };
    ASSERT_TRUE(pump({&a, &b}, [&] {
        for (auto& line : a.takeChat()) heardA.push_back(line);
        for (auto& line : b.takeChat()) heardB.push_back(line);
        return heard(heardA) && heard(heardB);
    }));
}

TEST_F(ServerTest, LeaveIsAnnounced) {
    NetworkClient a;
    auto b = std::make_unique<NetworkClient>();
    createAndJoin(a, *b);
    b.reset();  // Bob closes the tab
    std::vector<protocol::ChatLine> heard;
    ASSERT_TRUE(pump({&a}, [&] {
        for (auto& line : a.takeChat()) heard.push_back(line);
        return std::any_of(heard.begin(), heard.end(),
                           [](const protocol::ChatLine& l) { return l.from.empty() && l.text == "Bob left the game"; });
    }));
}

TEST_F(ServerTest, LateJoinerGetsHistory) {
    NetworkClient a;
    NetworkClient b;
    connect(a, "Alice");
    a.createLobby();
    std::optional<protocol::Joined> joinedA;
    ASSERT_TRUE(pump({&a}, [&] { return (joinedA = a.takeJoined()).has_value(); }));
    ASSERT_TRUE(pump({&a}, [&] { return a.ticksWaiting() >= 120; }));
    connect(b, "Bob");
    b.joinLobby(joinedA->lobbyId);
    std::optional<protocol::Joined> joinedB;
    ASSERT_TRUE(pump({&a, &b}, [&] { return (joinedB = b.takeJoined()).has_value(); }));
    EXPECT_GE(joinedB->history.size(), 120u);
    EXPECT_EQ(joinedB->history.front().tick, 1);
}

TEST_F(ServerTest, InvalidNameIsRejected) {
    NetworkClient client;
    ASSERT_TRUE(client.connect(address(), "bad name!"));
    std::optional<std::string> error;
    ASSERT_TRUE(pump({&client}, [&] { return (error = client.takeError()).has_value(); }));
    EXPECT_FALSE(pump({&client}, [&] { return client.takeLobbies().has_value(); }, 300ms));
}

TEST_F(ServerTest, DisconnectDuringTicksIsHarmless) {
    NetworkClient a;
    auto b = std::make_unique<NetworkClient>();
    createAndJoin(a, *b);
    b.reset();
    const std::size_t before = a.ticksWaiting();
    ASSERT_TRUE(pump({&a}, [&] { return a.ticksWaiting() >= before + 30; }));
    // The server is still taking new players.
    NetworkClient c;
    connect(c, "Carol");
    EXPECT_GT(c.playerId(), 0);
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

    protocol::Joined joined{.lobbyId = 3, .lobbyName = "Alice's world", .seed = 42};
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
    EXPECT_EQ(back->lobbyName, "Alice's world");
    EXPECT_EQ(back->history.back().turns[1].input.moveY, -1);
}

}  // namespace
