// The server's side of the stats: replaying the world's ticks into named events (StatsObserver), sending them to
// the stats service in batches that survive failures (StatsReporter), and the server wiring it all together.
#include "network_client.h"
#include "server.h"
#include "stats_json.h"
#include "stats_observer.h"
#include "stats_reporter.h"

#include <ixwebsocket/IXHttpServer.h>
#include <ixwebsocket/IXNetSystem.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <set>
#include <thread>

namespace {

using namespace std::chrono_literals;

// A stand-in for the stats service: records every request, and answers with the status `respond` picks.
class FakeStatsService {
public:
    explicit FakeStatsService(int port) : server_(port, "127.0.0.1") {
        ix::initNetSystem();
        server_.setOnConnectionCallback([this](ix::HttpRequestPtr request, std::shared_ptr<ix::ConnectionState>) {
            int status = 200;
            {
                const std::lock_guard lock(mutex_);
                ++calls_;
                all_.push_back(request->body);
                status = respond ? respond(calls_) : 200;
                if (status == 200) accepted_.push_back(request->body);
                const auto auth = request->headers.find("Authorization");
                authorization_ = auth == request->headers.end() ? "" : auth->second;
            }
            return std::make_shared<ix::HttpResponse>(status, "", ix::HttpErrorCode::Ok, ix::WebSocketHttpHeaders{},
                                                      "{}");
        });
        EXPECT_TRUE(server_.listen().first);
        server_.start();
    }
    ~FakeStatsService() { server_.stop(); }

    std::vector<std::string> accepted() {
        const std::lock_guard lock(mutex_);
        return accepted_;
    }
    int calls() {
        const std::lock_guard lock(mutex_);
        return calls_;
    }
    std::string authorization() {
        const std::lock_guard lock(mutex_);
        return authorization_;
    }
    // How many accepted bodies mention `text`.
    int count(const std::string& text) {
        int n = 0;
        for (const std::string& body : accepted()) {
            for (std::size_t at = body.find(text); at != std::string::npos; at = body.find(text, at + 1)) ++n;
        }
        return n;
    }

    // How many requests (accepted or not) mention `text`.
    int requestsMentioning(const std::string& text) {
        const std::lock_guard lock(mutex_);
        return static_cast<int>(std::count_if(all_.begin(), all_.end(), [&](const std::string& body) {
            return body.find(text) != std::string::npos;
        }));
    }

    std::function<int(int call)> respond;  // set before traffic starts

private:
    ix::HttpServer server_;
    std::mutex mutex_;
    int calls_ = 0;
    std::vector<std::string> accepted_;
    std::vector<std::string> all_;
    std::string authorization_;
};

bool waitFor(const std::function<bool()>& done, std::chrono::milliseconds timeout = 3000ms) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (done()) return true;
        std::this_thread::sleep_for(10ms);
    }
    return done();
}

StatEvent event(const std::string& id) { return {.id = id, .type = "ChatSent", .at = 1, .player = "Alice"}; }

TEST(StatsJson, EscapesAndShapesBatch) {
    StatEvent e{.id = "7-1-0", .type = "ItemCollected", .at = 42, .player = "Alice", .subject = "Wo\"od\\",
                .killerKind = "", .count = 3, .icon = 0};
    EXPECT_EQ(toJson(2, {e}),
              R"({"online":2,"events":[{"id":"7-1-0","type":"ItemCollected","at":42,"player":"Alice",)"
              R"("subject":"Wo\"od\\","killerKind":"","count":3,"icon":0}]})");
    EXPECT_EQ(toJson(0, {}), R"({"online":0,"events":[]})");
}

TEST(StatsObserver, ReportsNamedActions) {
    StatsObserver observer(99);
    observer.nameJoined(1, "Alice");
    observer.apply({.tick = 1, .turns = {{1, {}, {PlayerCommand::join("Alice")}}}}, 0);
    Simulation& sim = observer.simulation();
    Player& alice = *sim.findPlayer(1);
    Level& surface = sim.world().level(World::kSurfaceIndex);
    const int tx = static_cast<int>(alice.center().x) / 16;
    const int ty = static_cast<int>(alice.center().y) / 16;
    alice.setHeldItem(Inventory::Stack{ItemType::GemAxe, 1, 500});
    surface.map.setTile(tx, ty + 1, Tile::Tree);  // players face down when they arrive
    std::vector<StatEvent> seen;
    for (int t = 2; t < 600; ++t) {
        const bool press = t % 8 == 0;
        for (StatEvent& e : observer.apply({.tick = t, .turns = {{1, {.attack = press, .attackPressed = press}, {}}}},
                                           1000 + t)) {
            seen.push_back(e);
        }
        if (std::any_of(seen.begin(), seen.end(), [](const StatEvent& e) { return e.type == "TileBroken"; })) break;
    }
    const auto broken = std::find_if(seen.begin(), seen.end(), [](const StatEvent& e) { return e.type == "TileBroken"; });
    ASSERT_NE(broken, seen.end());
    EXPECT_EQ(broken->player, "Alice");
    EXPECT_EQ(broken->subject, "Tree");
    EXPECT_EQ(broken->id.rfind("99-", 0), 0u);
}

TEST(StatsObserver, NamesPlayersWhoLeft) {
    StatsObserver observer(5);
    observer.nameJoined(1, "Alice");
    observer.apply({.tick = 1, .turns = {{1, {}, {PlayerCommand::join("Alice")}}}}, 0);
    observer.apply({.tick = 2, .turns = {{1, {}, {PlayerCommand::leave()}}}}, 0);
    EXPECT_EQ(observer.nameOf(1), "Alice");
    EXPECT_EQ(observer.nameOf(9), "");
}

TEST(StatsObserver, ResetGivesFreshIds) {
    const auto ids = [](std::uint32_t seed) {
        StatsObserver observer(seed);
        observer.nameJoined(1, "Alice");
        std::set<std::string> result;
        for (int t = 1; t < 1200; ++t) {
            TickInput input{.tick = t};
            if (t == 1) input.turns = {{1, {}, {PlayerCommand::join("Alice")}}};
            else input.turns = {{1, {.moveX = (t / 60) % 2 ? 1 : -1, .attack = t % 9 == 0, .attackPressed = t % 9 == 0}, {}}};
            for (const StatEvent& e : observer.apply(input, 0)) result.insert(e.id);
        }
        return result;
    };
    const auto a = ids(1);
    const auto b = ids(2);
    for (const std::string& id : a) EXPECT_FALSE(b.contains(id)) << id;
}

TEST(StatsReporter, SendsBatchesWithToken) {
    FakeStatsService service(28779);
    StatsReporter reporter("http://127.0.0.1:28779/events", "secret", 50ms);
    reporter.setOnline(2);
    reporter.add({event("a"), event("b"), event("c")});
    ASSERT_TRUE(waitFor([&] { return service.count("\"id\":\"a\"") == 1 && service.count("\"id\":\"c\"") == 1; }));
    EXPECT_EQ(service.authorization(), "Bearer secret");
    EXPECT_GE(service.count("\"online\":2"), 1);
}

TEST(StatsReporter, SendsHeartbeatsWithoutEvents) {
    FakeStatsService service(28780);
    StatsReporter reporter("http://127.0.0.1:28780/events", "secret", 50ms);
    reporter.setOnline(1);
    EXPECT_TRUE(waitFor([&] { return service.count("\"online\":1,\"events\":[]") >= 2; }));
}

TEST(StatsReporter, RetriesUntilAccepted) {
    FakeStatsService service(28781);
    service.respond = [](int call) { return call <= 3 ? 503 : 200; };
    StatsReporter reporter("http://127.0.0.1:28781/events", "secret", 50ms);
    reporter.add({event("x1"), event("x2")});
    ASSERT_TRUE(waitFor([&] { return service.count("\"id\":\"x1\"") >= 1; }, 5000ms));
    std::this_thread::sleep_for(300ms);
    EXPECT_EQ(service.count("\"id\":\"x1\""), 1);
    EXPECT_EQ(service.count("\"id\":\"x2\""), 1);
}

TEST(StatsReporter, DropsOldestWhenBacklogFull) {
    FakeStatsService service(28782);
    service.respond = [](int) { return 503; };
    StatsReporter reporter("http://127.0.0.1:28782/events", "secret", 20ms, 5);
    for (int i = 0; i < 8; ++i) {
        reporter.add({event("e" + std::to_string(i))});
        std::this_thread::sleep_for(40ms);
    }
    EXPECT_TRUE(waitFor([&] { return reporter.backlog() == 5; }));
}

TEST(StatsReporter, DropsBatchOnClientError) {
    FakeStatsService service(28783);
    service.respond = [](int) { return 400; };
    StatsReporter reporter("http://127.0.0.1:28783/events", "secret", 20ms);
    reporter.add({event("bad")});
    ASSERT_TRUE(waitFor([&] { return service.calls() >= 5; }));
    // The refused batch went once and was dropped: only heartbeats follow, nothing piles up.
    EXPECT_EQ(service.requestsMentioning("\"id\":\"bad\""), 1);
    EXPECT_LE(reporter.backlog(), 1u);
}

class ServerStats : public ::testing::Test {
protected:
    void start(std::optional<StatsConfig> stats) {
        const std::string name = ::testing::UnitTest::GetInstance()->current_test_info()->name();
        port = 30000 + static_cast<int>(std::hash<std::string>{}(name) % 20000);
        server = std::make_unique<Server>(Server::kDefaultResetAfterTicks, std::move(stats));
        ASSERT_TRUE(server->start(static_cast<std::uint16_t>(port)));
        thread = std::jthread([this] { server->run(); });
    }
    void TearDown() override {
        server->stop();
        thread.join();
    }
    bool join(NetworkClient& client, const std::string& name) {
        if (!client.connect("localhost:" + std::to_string(port), name)) return false;
        return waitFor([&] {
            client.poll();
            return client.takeJoined().has_value();
        });
    }

    int port = 0;
    std::unique_ptr<Server> server;
    std::jthread thread;
};

TEST_F(ServerStats, ReportsJoinsAndWorlds) {
    FakeStatsService service(28784);
    start(StatsConfig{"http://127.0.0.1:28784/events", "secret"});
    NetworkClient alice;
    ASSERT_TRUE(join(alice, "Alice"));
    EXPECT_TRUE(waitFor([&] {
        alice.poll();
        return service.count("\"type\":\"PlayerJoined\",\"at\"") >= 1 && service.count("\"type\":\"WorldStarted\"") >= 1;
    }, 5000ms));
    EXPECT_GE(service.count("\"player\":\"Alice\""), 1);
}

TEST_F(ServerStats, NoStatsConfigNoReporting) {
    FakeStatsService service(28785);
    start(std::nullopt);
    NetworkClient alice;
    ASSERT_TRUE(join(alice, "Alice"));
    std::this_thread::sleep_for(1500ms);
    EXPECT_EQ(service.calls(), 0);
}

}  // namespace
