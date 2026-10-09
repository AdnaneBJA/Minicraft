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
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

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

bool waitUntil(const std::function<bool()>& done, std::chrono::milliseconds timeout = 5000ms) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (done()) return true;
        std::this_thread::sleep_for(5ms);
    }
    return done();
}

// A bare WebSocket connection, for what NetworkClient doesn't do: observing and pinging.
class RawClient {
public:
    explicit RawClient(int port) {
        socket_.setUrl("ws://localhost:" + std::to_string(port));
        socket_.disableAutomaticReconnection();
        socket_.setOnMessageCallback([this](const ix::WebSocketMessagePtr& message) {
            const std::lock_guard lock(mutex_);
            if (message->type == ix::WebSocketMessageType::Open) open_ = true;
            if (message->type == ix::WebSocketMessageType::Close) closed_ = true;
            if (message->type == ix::WebSocketMessageType::Message) {
                received_.emplace_back(message->str.begin(), message->str.end());
            }
        });
        socket_.start();
    }
    ~RawClient() { socket_.stop(); }

    bool waitOpen() {
        return waitUntil([this] {
            const std::lock_guard lock(mutex_);
            return open_;
        });
    }
    template <typename Message>
    void send(const Message& message) {
        const auto bytes = protocol::encode(message);
        socket_.sendBinary(std::string(bytes.begin(), bytes.end()));
    }
    // Every message of this type received so far, decoded.
    template <typename Message>
    std::vector<Message> received() {
        const std::lock_guard lock(mutex_);
        std::vector<Message> out;
        for (const auto& bytes : received_) {
            if (auto message = protocol::decode<Message>(bytes)) out.push_back(*message);
        }
        return out;
    }
    template <typename Message>
    bool waitFor(std::size_t count, std::chrono::milliseconds timeout = 5000ms) {
        return waitUntil([&] { return received<Message>().size() >= count; }, timeout);
    }
    bool closed() {
        const std::lock_guard lock(mutex_);
        return closed_;
    }

private:
    ix::WebSocket socket_;
    std::mutex mutex_;
    bool open_ = false;
    bool closed_ = false;
    std::vector<std::vector<std::uint8_t>> received_;
};

constexpr const char* kProbeToken = "probe-secret";

// Every test gets its own server on its own port, running on a background thread.
class ServerTest : public ::testing::Test {
protected:
    void SetUp() override { startServer(Server::kDefaultResetAfterTicks); }
    void TearDown() override { stopServer(); }

    void startServer(int resetAfterTicks, const std::string& probeToken = kProbeToken) {
        // A port of its own per test (ctest may run several test processes at once).
        const std::string name = ::testing::UnitTest::GetInstance()->current_test_info()->name();
        port = 30000 + static_cast<int>(std::hash<std::string>{}(name) % 20000);
        server = std::make_unique<Server>(resetAfterTicks, std::nullopt, probeToken);
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

TEST_F(ServerTest, ObserverWatchesWithoutPlaying) {
    NetworkClient alice;
    const protocol::Joined world = join(alice, "Alice");
    RawClient probe(port);
    ASSERT_TRUE(probe.waitOpen());
    probe.send(protocol::Observe{kProbeToken});
    ASSERT_TRUE(probe.waitFor<protocol::Joined>(1));
    EXPECT_EQ(probe.received<protocol::Joined>()[0].seed, world.seed);
    ASSERT_TRUE(probe.waitFor<protocol::TickMessage>(30));
    // Nobody heard of it: no join line, and every tick lists Alice alone.
    for (const auto& tick : probe.received<protocol::TickMessage>()) {
        for (const auto& turn : tick.input.turns) EXPECT_EQ(turn.playerId, alice.playerId());
    }
    for (const auto& line : alice.takeChat()) {
        if (line.text != "Alice joined the game") EXPECT_EQ(line.text.find("joined"), std::string::npos) << line.text;
    }
}

TEST_F(ServerTest, PingIsAnsweredAfterTheNextTick) {
    NetworkClient alice;
    join(alice, "Alice");
    RawClient probe(port);
    ASSERT_TRUE(probe.waitOpen());
    probe.send(protocol::Observe{kProbeToken});
    ASSERT_TRUE(probe.waitFor<protocol::TickMessage>(1));
    const auto sent = std::chrono::steady_clock::now();
    probe.send(protocol::ProbePing{42});
    ASSERT_TRUE(probe.waitFor<protocol::ProbePong>(1));
    const auto took = std::chrono::steady_clock::now() - sent;
    EXPECT_EQ(probe.received<protocol::ProbePong>()[0].id, 42);
    EXPECT_LT(took, 200ms);  // a tick is 16.7 ms; leave room for a slow CI machine
}

TEST_F(ServerTest, PingWorksWithoutAWorld) {
    RawClient probe(port);
    ASSERT_TRUE(probe.waitOpen());
    probe.send(protocol::Observe{kProbeToken});
    ASSERT_TRUE(probe.waitFor<protocol::Joined>(1));
    probe.send(protocol::ProbePing{7});
    ASSERT_TRUE(probe.waitFor<protocol::ProbePong>(1));
    EXPECT_EQ(probe.received<protocol::ProbePong>()[0].id, 7);
}

TEST_F(ServerTest, PingFromPlayerIsIgnored) {
    RawClient player(port);
    ASSERT_TRUE(player.waitOpen());
    player.send(protocol::Hello{"Alice"});
    ASSERT_TRUE(player.waitFor<protocol::Joined>(1));
    player.send(protocol::ProbePing{1});
    ASSERT_TRUE(player.waitFor<protocol::TickMessage>(20));
    EXPECT_TRUE(player.received<protocol::ProbePong>().empty());
}

TEST_F(ServerTest, ObserverOnEmptyServerGetsWorldLater) {
    RawClient probe(port);
    ASSERT_TRUE(probe.waitOpen());
    probe.send(protocol::Observe{kProbeToken});
    ASSERT_TRUE(probe.waitFor<protocol::Joined>(1));
    EXPECT_EQ(probe.received<protocol::Joined>()[0].seed, 0u);  // no world: nothing to watch yet
    std::this_thread::sleep_for(200ms);
    EXPECT_TRUE(probe.received<protocol::TickMessage>().empty());  // and the observer didn't start one
    NetworkClient alice;
    const protocol::Joined world = join(alice, "Alice");
    ASSERT_TRUE(probe.waitFor<protocol::Joined>(2));
    EXPECT_EQ(probe.received<protocol::Joined>()[1].seed, world.seed);
    EXPECT_TRUE(probe.waitFor<protocol::TickMessage>(10));
}

TEST_F(ServerTest, ObserverDoesNotKeepWorldAlive) {
    NetworkClient alice;
    const protocol::Joined first = join(alice, "Alice");
    RawClient probe(port);
    ASSERT_TRUE(probe.waitOpen());
    probe.send(protocol::Observe{kProbeToken});
    ASSERT_TRUE(probe.waitFor<protocol::TickMessage>(1));
    alice.disconnect();
    NetworkClient bob;
    const protocol::Joined second = join(bob, "Bob");
    EXPECT_NE(second.seed, first.seed);  // the world ended with Alice, observer or not
}

TEST_F(ServerTest, ObserverCannotPlay) {
    NetworkClient alice;
    join(alice, "Alice");
    RawClient probe(port);
    ASSERT_TRUE(probe.waitOpen());
    probe.send(protocol::Observe{kProbeToken});
    ASSERT_TRUE(probe.waitFor<protocol::Joined>(1));
    probe.send(protocol::Hello{"Sneaky"});
    probe.send(protocol::InputMessage{{.moveX = 1}});
    probe.send(protocol::ChatMessage{"hello"});
    ASSERT_TRUE(probe.waitFor<protocol::TickMessage>(30));
    for (const auto& tick : probe.received<protocol::TickMessage>()) {
        for (const auto& turn : tick.input.turns) EXPECT_EQ(turn.playerId, alice.playerId());
    }
    for (const auto& line : alice.takeChat()) {
        EXPECT_EQ(line.text.find("Sneaky"), std::string::npos);
        EXPECT_NE(line.text, "hello");
    }
}

TEST_F(ServerTest, ObserverLimit) {
    // One after the other: connections are served on their own threads, so messages sent at once on several of
    // them reach the server in any order.
    RawClient a(port), b(port), c(port);
    for (RawClient* probe : {&a, &b}) {
        ASSERT_TRUE(probe->waitOpen());
        probe->send(protocol::Observe{kProbeToken});
        ASSERT_TRUE(probe->waitFor<protocol::Joined>(1));
    }
    ASSERT_TRUE(c.waitOpen());
    c.send(protocol::Observe{kProbeToken});
    ASSERT_TRUE(c.waitFor<protocol::ErrorMessage>(1));
    EXPECT_EQ(c.received<protocol::ErrorMessage>()[0].text, "Too many observers");
    EXPECT_TRUE(waitUntil([&] { return c.closed(); }));
    EXPECT_TRUE(c.received<protocol::Joined>().empty());
}

TEST_F(ServerTest, FullServerRefusesPlayerNotObserver) {
    std::vector<std::unique_ptr<RawClient>> players;
    for (int i = 0; i < protocol::kMaxPlayers; ++i) {
        players.push_back(std::make_unique<RawClient>(port));
        ASSERT_TRUE(players.back()->waitOpen());
        players.back()->send(protocol::Hello{"P" + std::to_string(i)});
    }
    for (auto& player : players) ASSERT_TRUE(player->waitFor<protocol::Joined>(1));
    RawClient probe(port), probe2(port);  // both observer slots taken
    for (RawClient* p : {&probe, &probe2}) {
        ASSERT_TRUE(p->waitOpen());
        p->send(protocol::Observe{kProbeToken});
        ASSERT_TRUE(p->waitFor<protocol::Joined>(1));
    }
    RawClient late(port);
    ASSERT_TRUE(late.waitOpen());
    late.send(protocol::Hello{"Late"});
    ASSERT_TRUE(late.waitFor<protocol::ErrorMessage>(1));
    EXPECT_EQ(late.received<protocol::ErrorMessage>()[0].text, "Server is full");
    EXPECT_TRUE(late.received<protocol::Joined>().empty());
}

TEST_F(ServerTest, ObserveNeedsTheToken) {
    NetworkClient alice;
    join(alice, "Alice");
    RawClient snoop(port);
    ASSERT_TRUE(snoop.waitOpen());
    snoop.send(protocol::Observe{"guess"});
    ASSERT_TRUE(snoop.waitFor<protocol::ErrorMessage>(1));
    EXPECT_EQ(snoop.received<protocol::ErrorMessage>()[0].text, "Not allowed to observe");
    EXPECT_TRUE(waitUntil([&] { return snoop.closed(); }));
    EXPECT_TRUE(snoop.received<protocol::Joined>().empty());
    EXPECT_TRUE(snoop.received<protocol::TickMessage>().empty());
}

TEST_F(ServerTest, NoTokenNoObservers) {
    stopServer();
    startServer(Server::kDefaultResetAfterTicks, "");  // observing is off
    RawClient snoop(port);
    ASSERT_TRUE(snoop.waitOpen());
    snoop.send(protocol::Observe{""});
    ASSERT_TRUE(snoop.waitFor<protocol::ErrorMessage>(1));
    EXPECT_EQ(snoop.received<protocol::ErrorMessage>()[0].text, "Not allowed to observe");
    EXPECT_TRUE(snoop.received<protocol::Joined>().empty());
}

}  // namespace
