# Observability Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A Go probe that plays the live game as a hidden observer, the game's health and the stats service's own
numbers exported to Prometheus, and public read-only Grafana dashboards with SLOs, all on the existing t3.micro.

**Architecture:** Three new protocol messages (`Observe`, `ProbePing`, `ProbePong`) let a connection watch the
world without being in it and time a round trip through the tick loop. The game server adds a `health` object to
the batch it already posts to the stats service every second; the stats service turns it into Prometheus gauges
at `/metrics`. A Go probe (`loadtest/cmd/probe`) exports its own histograms. Prometheus scrapes both and evaluates
SLO recording rules (unit-tested with `promtool test rules`); Grafana, served at `/grafana`, shows two dashboards
and alert rules on the SLO series.

**Tech Stack:** C++20 / IXWebSocket / GoogleTest; Go 1.25 (stats) and 1.26 (loadtest), `prometheus/client_golang`,
`coder/websocket`; Prometheus v3.5.0, Grafana 12.1.0, Docker Compose, Caddy.

**Spec:** `docs/superpowers/specs/2026-10-09-observability-design.md`

## Global Constraints
- Message types are appended after `Error`: `Observe` = 10, `ProbePing` = 11, `ProbePong` = 12. Existing values never change; the browser client's `Hello` is unchanged.
- `kMaxObservers = 2`. Observers never count towards `kMaxPlayers`, the online count, the stats, or keeping a world alive.
- The probe runs every 30 s, 10 pings 100 ms apart, 10 s deadline per run; results: `ok`, `connect_failed`, `refused`, `join_failed`, `timeout`.
- SLOs over a rolling 1 h: ≥ 99% probe runs `ok`; 99% joins < 1 s; 99% pings answered < 50 ms; the game reported within 30 s and every scrape target `up`.
- Alerts are dashboard only (no contact points). Grafana is anonymous Viewer at `https://<DOMAIN>/grafana`, admin password `GRAFANA_ADMIN_PASSWORD` in `.env`.
- Memory caps: Prometheus 128m, Grafana 160m, probe 32m. Prometheus retention 15d.
- Prometheus, the probe's `/metrics` and the stats service's `/metrics` are never routed by Caddy.
- Commits: conventional prefix (`feat:`, `docs:`, `ci:`), no AI co-author lines. Comments and docs match the repo's plain style.

## Spec adjustments made while planning (update the spec in Task 7)
- **Welcome:** the server sends `Welcome` the moment a socket opens, before it knows what the connection is; an observer gets it too and ignores it. (The spec said no `Welcome`.)
- **No world yet:** an observer that arrives while nobody plays gets `Joined{seed 0, empty history}` and no ticks; the world is not started for it. When a world starts or resets, observers get its `Joined`.
- **Pongs without a world:** pongs go out on the 60 Hz clock whether or not a world exists, so the probe works on an empty server.
- **Full server:** the socket limit becomes `kMaxPlayers + kMaxObservers`, so the 32-player cap moves to `Hello`: a 33rd player gets `Error "Server is full"`.
- **Game server CPU/memory:** the C++ server has no `/metrics`, so `health` also carries `rssBytes` and `cpuSeconds` (read from `/proc/self/stat`; 0 elsewhere).
- **SLO math lives in Prometheus recording rules** (`slo:*`), checked with `promtool check config` and unit-tested with `promtool test rules`; Grafana alert rules threshold those series. (The spec's `promtool check rules` on Grafana rules doesn't apply: Grafana rules aren't Prometheus rules.)
- **Stale online count fixed:** today `online` is only updated while a world ticks, so after the last player leaves the heartbeat keeps the old count. Health and online are now published every tick of the server clock.

## Review Focus
1. A 33rd player while 2 observers are connected gets "Server is full" (not a silent refusal, and never a 33rd player in the world). Test: Task 2 `FullServerRefusesPlayerNotObserver`.
2. An observer arriving on an empty server gets `Joined{0}` and doesn't start a world or a `WorldStarted` stat; it gets the real `Joined` when a player starts one. Test: Task 2 `ObserverOnEmptyServerGetsWorldLater`.
3. After the last player leaves, the heartbeat reports `"online":0` (it used to stay at the old count). Test: Task 3 `OnlineDropsToZeroWhenWorldEnds`.
4. A world reset (a second `Joined`) in the middle of a probe run doesn't fail the run. Test: Task 5 `TestOnceSurvivesWorldReset`.
5. An observer that sends `Hello`, `Input` or `Chat` stays invisible (never becomes a player, nothing reaches the world). Test: Task 2 `ObserverCannotPlay`.

---

### Task 1: Protocol messages on both sides

**Files:**
- Modify: `net-common/protocol.h` (enum, three structs)
- Modify: `net-common/protocol.cpp` (`typeOf`)
- Modify: `server/tests/protocol_fixtures.cpp` (three examples)
- Create: `loadtest/protocol/testdata/observe.bin`, `probeping.bin`, `probepong.bin` (generated)
- Modify: `loadtest/protocol/protocol.go` (types, encoders, decoder, `ReadMessage`)
- Modify: `loadtest/protocol/protocol_test.go`
- Modify: `loadtest/bot/bot.go` (use `protocol.ReadMessage`)

**Interfaces:**
- Produces (C++): `protocol::Observe{}`, `protocol::ProbePing{int id}`, `protocol::ProbePong{int id}`, `protocol::kMaxObservers = 2`.
- Produces (Go): `protocol.Observe`, `protocol.ProbePing`, `protocol.ProbePong` (`MsgType` 10–12); `Message.PingID int32`; `EncodeObserve() []byte`, `EncodeProbePing(id int32) []byte`, `EncodeProbePong(id int32) []byte`; `ReadMessage(r io.Reader, maxMessage int64) (Message, int64, error)` (the message and how many bytes it was; `Joined`'s history is streamed and only counted).

- [ ] **Step 1: C++ messages.** In `net-common/protocol.h` add `constexpr int kMaxObservers = 2;  // probes watching the world, on top of kMaxPlayers` after `kMaxPlayers`, and extend the enum after `Error,`:

```cpp
    Error,
    // Appended, so the values above never change. Monitoring: a probe watches the world without being in it.
    Observe,      // client -> server, instead of Hello: send me the world and its ticks, but I'm not a player
    ProbePing,    // client -> server (observers only): answer me with the next tick
    ProbePong,    // server -> client: the answer, sent right after a tick went out
};
```

Add after `StateHashMessage`:

```cpp
struct Observe {
    static constexpr MessageType kType = MessageType::Observe;
    void write(ByteWriter&) const {}
    void read(ByteReader&) {}
};

struct ProbePing {
    static constexpr MessageType kType = MessageType::ProbePing;
    int id = 0;
    void write(ByteWriter& out) const { out.i32(id); }
    void read(ByteReader& in) { id = in.i32(); }
};
```

and after `ErrorMessage`:

```cpp
struct ProbePong {
    static constexpr MessageType kType = MessageType::ProbePong;
    int id = 0;  // the ping's
    void write(ByteWriter& out) const { out.i32(id); }
    void read(ByteReader& in) { id = in.i32(); }
};
```

In `protocol.cpp`, `typeOf` must accept the new last value:

```cpp
    if (bytes.empty() || bytes[0] > static_cast<std::uint8_t>(MessageType::ProbePong)) return std::nullopt;
```

- [ ] **Step 2: C++ fixtures.** In `server/tests/protocol_fixtures.cpp` `examples()`, add to the returned list:

```cpp
        {"observe", encode(Observe{})},
        {"probeping", encode(ProbePing{5})},
        {"probepong", encode(ProbePong{5})},
```

- [ ] **Step 3: Build and see the fixture test fail.** From a shell set up per the local-toolchain memory (CLion MinGW + Ninja on PATH):

Run: `cmake --build cmake-build-debug --target server_tests && ./cmake-build-debug/server_tests --gtest_filter=ProtocolFixtures.*`
Expected: FAIL with `missing fixture .../observe.bin`.

- [ ] **Step 4: Write the fixtures.** Run: `WRITE_PROTOCOL_FIXTURES=1 ./cmake-build-debug/server_tests --gtest_filter=ProtocolFixtures.*` then the same without the variable. Expected: PASS. `loadtest/protocol/testdata/` now has `observe.bin` (1 byte `0a`), `probeping.bin` (`0b 05 00 00 00`), `probepong.bin` (`0c 05 00 00 00`).

- [ ] **Step 5: Go tests first.** In `loadtest/protocol/protocol_test.go`:
  - `TestFixturesDecode` cases, add:
    ```go
    {"observe", Message{Type: Observe}},
    {"probeping", Message{Type: ProbePing, PingID: 5}},
    {"probepong", Message{Type: ProbePong, PingID: 5}},
    ```
  - `TestFixturesReencode` map, add:
    ```go
    "observe":   EncodeObserve(),
    "probeping": EncodeProbePing(5),
    "probepong": EncodeProbePong(5),
    ```
  - `TestDecodeRejectsMalformed`: change `"unknown type": {10}` to `"unknown type": {13}`, and add `"truncated ping": {byte(ProbePing), 1}` and `"observe with a body": {byte(Observe), 0}`.
  - `FuzzDecode` seed list: add `"observe", "probeping", "probepong"`.
  - Add a streaming test:
    ```go
    func TestReadMessageStreamsJoined(t *testing.T) {
    	history := make([]TickMsg, 1000)
    	for i := range history {
    		history[i] = TickMsg{Tick: int32(i + 1), Turns: []Turn{{PlayerID: 1, Keys: KeyRight}}}
    	}
    	joined := EncodeJoined(9, history)
    	msg, n, err := ReadMessage(bytes.NewReader(joined), 64) // far smaller than the history: it's not buffered
    	if err != nil || msg.Type != Joined || msg.Seed != 9 || msg.HistoryLen != 1000 || n != int64(len(joined)) {
    		t.Fatalf("got %+v, %d bytes, %v", msg, n, err)
    	}
    	msg, n, err = ReadMessage(bytes.NewReader(EncodeProbePong(3)), 64)
    	if err != nil || msg.Type != ProbePong || msg.PingID != 3 || n != 5 {
    		t.Fatalf("got %+v, %d bytes, %v", msg, n, err)
    	}
    	if _, _, err := ReadMessage(bytes.NewReader(EncodeChat("a long line of chat")), 8); err == nil {
    		t.Error("a message over the limit must fail")
    	}
    }
    ```

Run: `cd loadtest && go test ./protocol/` — Expected: FAIL (undefined: Observe, EncodeObserve, ReadMessage...).

- [ ] **Step 6: Go protocol.** In `protocol.go`:
  - Constants: after `Error` add `Observe // client -> server (monitoring)`, `ProbePing`, `ProbePong // server -> client`.
  - `Message`: add `PingID int32 // ProbePing, ProbePong`.
  - Encoders:
    ```go
    // EncodeObserve asks to watch the world without playing (monitoring probes).
    func EncodeObserve() []byte { return start(Observe).b }

    // EncodeProbePing asks for an answer with the next tick.
    func EncodeProbePing(id int32) []byte { w := start(ProbePing); w.i32(id); return w.b }

    // EncodeProbePong answers a ping.
    func EncodeProbePong(id int32) []byte { w := start(ProbePong); w.i32(id); return w.b }
    ```
  - `Decode` switch: `case Observe:` (nothing to read), `case ProbePing, ProbePong: m.PingID = r.i32()`.
  - `String()` names: append `"Observe", "ProbePing", "ProbePong"`.
  - Move `bot.readMessage`'s body here as `ReadMessage` (add imports `io`):
    ```go
    // ReadMessage reads one message from a WebSocket message reader, and how many bytes it was. The world's history
    // (Joined) is streamed: its header is read and the rest only counted, so a history of any size never sits in
    // memory. Every other message must fit in maxMessage.
    func ReadMessage(r io.Reader, maxMessage int64) (Message, int64, error) {
    	var header [9]byte // type, then Joined's seed and tick count
    	n, err := io.ReadFull(r, header[:1])
    	if err != nil {
    		return Message{}, 0, err
    	}
    	if MsgType(header[0]) == Joined {
    		if _, err := io.ReadFull(r, header[1:]); err != nil {
    			return Message{}, 0, err
    		}
    		rest, err := io.Copy(io.Discard, r)
    		if err != nil {
    			return Message{}, 0, err
    		}
    		count := int32(binary.LittleEndian.Uint32(header[5:]))
    		if count < 0 {
    			return Message{}, 0, fmt.Errorf("%w: Joined history count %d", ErrMalformed, count)
    		}
    		return Message{Type: Joined, Seed: binary.LittleEndian.Uint32(header[1:]), HistoryLen: int(count)},
    			int64(len(header)) + rest, nil
    	}
    	body, err := io.ReadAll(io.LimitReader(r, maxMessage))
    	if err != nil {
    		return Message{}, 0, err
    	}
    	if extra, _ := io.Copy(io.Discard, r); extra > 0 {
    		return Message{}, 0, fmt.Errorf("message over %d bytes", maxMessage)
    	}
    	data := append(header[:n:n], body...)
    	msg, err := Decode(data)
    	return msg, int64(len(data)), err
    }
    ```
  - In `bot/bot.go`, replace `readMessage`'s body with:
    ```go
    func readMessage(ctx context.Context, conn *websocket.Conn, s *Shared) (protocol.Message, error) {
    	_, r, err := conn.Reader(ctx)
    	if err != nil {
    		return protocol.Message{}, err
    	}
    	msg, n, err := protocol.ReadMessage(r, maxMessage)
    	if n > 0 {
    		s.Counters.Messages.Add(1)
    		s.Counters.Bytes.Add(n)
    	}
    	return msg, err
    }
    ```
    and keep its doc comment one line: `// readMessage reads one message and counts it (see protocol.ReadMessage).` Remove imports that become unused (`encoding/binary`, `io` if unused).

- [ ] **Step 7: Run everything Go.** Run: `cd loadtest && go vet ./... && go test -count=1 ./...` — Expected: PASS.

- [ ] **Step 8: Run all C++ tests.** Run: `ctest --test-dir cmake-build-debug --output-on-failure` — Expected: PASS (80 tests + the unchanged rest).

- [ ] **Step 9: Commit.**
```bash
git add net-common/protocol.h net-common/protocol.cpp server/tests/protocol_fixtures.cpp loadtest/protocol loadtest/bot/bot.go
git commit -m "feat(protocol): Observe, ProbePing and ProbePong for monitoring probes"
```

---

### Task 2: Observers and pings in the game server

**Files:**
- Modify: `server/server.h` (`Client` fields, new methods)
- Modify: `server/server.cpp`
- Test: `server/tests/server_test.cpp`

**Interfaces:**
- Consumes: Task 1's `protocol::Observe`, `ProbePing`, `ProbePong`, `kMaxObservers`.
- Produces: `Client::observing` (bool), `Client::pendingPing` (`std::optional<int>`), `int Server::observersOnline() const`, `void Server::answerPings()`; used by Task 3's health.

- [ ] **Step 1: A raw test client.** In `server_test.cpp`, inside the anonymous namespace after `hears`, add:

```cpp
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
```

Add `#include <mutex>` and `#include <vector>` at the top.

- [ ] **Step 2: The failing tests.** Append to `server_test.cpp` (before the closing `}  // namespace`):

```cpp
TEST_F(ServerTest, ObserverWatchesWithoutPlaying) {
    NetworkClient alice;
    const protocol::Joined world = join(alice, "Alice");
    RawClient probe(port);
    ASSERT_TRUE(probe.waitOpen());
    probe.send(protocol::Observe{});
    ASSERT_TRUE(probe.waitFor<protocol::Joined>(1));
    EXPECT_EQ(probe.received<protocol::Joined>()[0].seed, world.seed);
    ASSERT_TRUE(probe.waitFor<protocol::TickMessage>(30));
    // Nobody heard of it: no join line, and every tick lists Alice alone.
    for (const auto& tick : probe.received<protocol::TickMessage>()) {
        for (const auto& turn : tick.input.turns) EXPECT_EQ(turn.playerId, alice.playerId());
    }
    for (const auto& line : alice.takeChat()) EXPECT_EQ(line.text.find("joined"), std::string::npos) << line.text;
}

TEST_F(ServerTest, PingIsAnsweredAfterTheNextTick) {
    NetworkClient alice;
    join(alice, "Alice");
    RawClient probe(port);
    ASSERT_TRUE(probe.waitOpen());
    probe.send(protocol::Observe{});
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
    probe.send(protocol::Observe{});
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
    probe.send(protocol::Observe{});
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
    probe.send(protocol::Observe{});
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
    probe.send(protocol::Observe{});
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
    RawClient a(port), b(port), c(port);
    for (RawClient* probe : {&a, &b, &c}) {
        ASSERT_TRUE(probe->waitOpen());
        probe->send(protocol::Observe{});
    }
    ASSERT_TRUE(a.waitFor<protocol::Joined>(1));
    ASSERT_TRUE(b.waitFor<protocol::Joined>(1));
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
    RawClient probe(port);
    ASSERT_TRUE(probe.waitOpen());
    probe.send(protocol::Observe{});
    ASSERT_TRUE(probe.waitFor<protocol::Joined>(1));
    RawClient late(port);
    ASSERT_TRUE(late.waitOpen());
    late.send(protocol::Hello{"Late"});
    ASSERT_TRUE(late.waitFor<protocol::ErrorMessage>(1));
    EXPECT_EQ(late.received<protocol::ErrorMessage>()[0].text, "Server is full");
    EXPECT_TRUE(late.received<protocol::Joined>().empty());
}
```

Run: `cmake --build cmake-build-debug --target server_tests && ./cmake-build-debug/server_tests --gtest_filter=ServerTest.*Observ*:ServerTest.*Ping*:ServerTest.FullServer*`
Expected: FAIL (observers get nothing; `Late` gets in or is refused at the socket).

- [ ] **Step 3: Server state.** In `server.h`, `Client` gains:

```cpp
        bool observing = false;            // a monitoring probe: gets the world and its ticks, but isn't in it
        std::optional<int> pendingPing;    // observers: a ProbePing to answer after the next tick
```

New private methods:

```cpp
    // An observer's Observe: they get the world (if any) and its ticks from now on.
    void observe(Client& client);
    // Every observer's waiting ping gets its pong. Runs on every tick of the clock, world or not.
    void answerPings();
    int observersOnline() const;
    // Observers get the current world: its seed and history (seed 0, no history: there's no world).
    void sendWorldToObservers();
```

- [ ] **Step 4: Server behaviour.** In `server.cpp`:
  - `start()`: the connection limit becomes `static_cast<std::size_t>(protocol::kMaxPlayers + protocol::kMaxObservers)`.
  - `run()`: in the tick branch, after `tickWorld();` add `answerPings();`.
  - `handleMessage()`: replace the gate line `if (client.name.empty() && *type != MessageType::Hello) return;` with:

```cpp
    // An observer only ever pings; everything else it sends is ignored.
    if (client.observing) {
        if (const auto ping = protocol::decode<protocol::ProbePing>(bytes); ping && *type == MessageType::ProbePing) {
            client.pendingPing = ping->id;  // a newer ping replaces one not answered yet
        }
        return;
    }
    // Until a player is in the world, Hello (or Observe, for a probe) is the only thing they can do.
    if (client.name.empty() && *type != MessageType::Hello && *type != MessageType::Observe) return;
```

  and add cases:

```cpp
        case MessageType::Observe:
            if (protocol::decode<protocol::Observe>(bytes) && client.name.empty()) observe(client);
            break;
```

  In the `Hello` case, before `client.name = hello->name;`:

```cpp
                if (playersOnline() >= protocol::kMaxPlayers) {
                    sendTo(client, protocol::ErrorMessage{"Server is full"});
                    return;
                }
```

  - New functions:

```cpp
void Server::observe(Client& client) {
    if (observersOnline() >= protocol::kMaxObservers) {
        sendTo(client, protocol::ErrorMessage{"Too many observers"});
        if (const std::shared_ptr<ix::WebSocket> socket = client.socket.lock()) socket->close();
        return;
    }
    client.observing = true;
    std::printf("Connection %d is an observer\n", client.id);
    if (world_) {
        sendTo(client, protocol::Joined{world_->seed(), world_->history()});
    } else {
        sendTo(client, protocol::Joined{});
    }
}

void Server::sendWorldToObservers() {
    for (const auto& [id, client] : clients_) {
        if (client.observing) sendTo(client, protocol::Joined{world_->seed(), world_->history()});
    }
}

void Server::answerPings() {
    for (auto& [id, client] : clients_) {
        if (client.pendingPing) sendTo(client, protocol::ProbePong{*std::exchange(client.pendingPing, std::nullopt)});
    }
}

int Server::observersOnline() const {
    return static_cast<int>(std::count_if(clients_.begin(), clients_.end(),
                                          [](const auto& entry) { return entry.second.observing; }));
}
```

  - `startWorld()`: at its end add `sendWorldToObservers();` (covers a new world and a reset: `resetWorld` calls `startWorld`).
  - `tickWorld()`: after the loop over `memberIds()`, add:

```cpp
    for (const auto& [id, client] : clients_) {
        if (client.observing) sendTo(client, message);
    }
```

  - `disconnect()`: the log line should say what it was: `std::printf(client.observing ? "Observer %d disconnected\n" : "Player %d (%s) disconnected\n", ...)` — split into an `if` so the format arguments match:

```cpp
    if (client.observing) {
        std::printf("Observer %d disconnected\n", client.id);
    } else {
        std::printf("Player %d (%s) disconnected\n", client.id, client.name.c_str());
    }
```

  Add `#include <utility>` for `std::exchange`.

- [ ] **Step 5: Run the new tests.** Run the Step 2 command. Expected: PASS.

- [ ] **Step 6: All C++ tests.** Run: `ctest --test-dir cmake-build-debug --output-on-failure` — Expected: PASS.

- [ ] **Step 7: The load test still breaks at the right place.** The ramp used to see 4 *connections refused* at 36 bots; it now sees 2 refused connections and 2 join refusals ("Server is full"). Check that `loadtest` counts an `Error` reply to `Hello` as a join refusal: `grep -n "JoinRefusals" loadtest/bot/bot.go`. If it does, nothing to change; note it for the ADR in Task 7.

- [ ] **Step 8: Commit.**
```bash
git add server/server.h server/server.cpp server/tests/server_test.cpp
git commit -m "feat(server): hidden observers and probe pings"
```

---

### Task 3: Health on the stats batch

**Files:**
- Create: `server/process_usage.h`, `server/process_usage.cpp`
- Modify: `CMakeLists.txt:102` (add `server/process_usage.cpp` to `server_lib`)
- Modify: `server/stats_json.h`, `server/stats_json.cpp`
- Modify: `server/stats_reporter.h`, `server/stats_reporter.cpp`
- Modify: `server/server.h`, `server/server.cpp`
- Test: `server/tests/stats_test.cpp`

**Interfaces:**
- Consumes: Task 2's `observersOnline()`.
- Produces: the batch JSON `{"online":N,"events":[...],"health":{"at":ms,"connections":n,"observers":n,"ticks":n,"historyTicks":n,"historyBytes":n,"backlog":n,"rssBytes":n,"cpuSeconds":x}}` that Task 4 parses. `struct ServerHealth`, `StatsReporter::setHealth(const ServerHealth&)`.

- [ ] **Step 1: Failing tests.** In `stats_test.cpp`:
  - Replace `TEST(StatsJson, EscapesAndShapesBatch)` with:

```cpp
TEST(StatsJson, EscapesAndShapesBatch) {
    StatEvent e{.id = "7-1-0", .type = "ItemCollected", .at = 42, .player = "Alice", .subject = "Wo\"od\\",
                .killerKind = "", .count = 3, .icon = 0};
    const ServerHealth health{.connections = 3, .observers = 1, .ticks = 600, .historyTicks = 500,
                              .historyBytes = 9000, .rssBytes = 4096, .cpuSeconds = 1.5};
    EXPECT_EQ(toJson(2, {e}, 1000, health, 4),
              R"({"online":2,"events":[{"id":"7-1-0","type":"ItemCollected","at":42,"player":"Alice",)"
              R"("subject":"Wo\"od\\","killerKind":"","count":3,"icon":0}],)"
              R"("health":{"at":1000,"connections":3,"observers":1,"ticks":600,"historyTicks":500,)"
              R"("historyBytes":9000,"backlog":4,"rssBytes":4096,"cpuSeconds":1.500}})");
    EXPECT_EQ(toJson(0, {}, 5, {}, 0),
              R"({"online":0,"events":[],"health":{"at":5,"connections":0,"observers":0,"ticks":0,)"
              R"("historyTicks":0,"historyBytes":0,"backlog":0,"rssBytes":0,"cpuSeconds":0.000}})");
}
```

  - `SendsHeartbeatsWithoutEvents`: its pattern becomes `"\"online\":1,\"events\":[],\"health\""`.
  - Add:

```cpp
TEST(StatsReporter, SendsHealth) {
    FakeStatsService service(28786);
    StatsReporter reporter("http://127.0.0.1:28786/events", "secret", 50ms);
    reporter.setHealth({.connections = 5, .observers = 1, .ticks = 77});
    EXPECT_TRUE(waitFor([&] { return service.count("\"connections\":5,\"observers\":1,\"ticks\":77") >= 1; }));
}

TEST_F(ServerStats, OnlineDropsToZeroWhenWorldEnds) {
    FakeStatsService service(28787);
    start(StatsConfig{"http://127.0.0.1:28787/events", "secret"});
    {
        NetworkClient alice;
        ASSERT_TRUE(join(alice, "Alice"));
        ASSERT_TRUE(waitFor([&] {
            alice.poll();
            return service.count("\"online\":1,") >= 1;
        }, 5000ms));
    }  // Alice leaves; the world ends
    const int before = service.count("\"online\":0,");
    EXPECT_TRUE(waitFor([&] { return service.count("\"online\":0,") > before + 1; }, 5000ms));
}

TEST_F(ServerStats, HealthCountsConnectionsAndHistory) {
    FakeStatsService service(28788);
    start(StatsConfig{"http://127.0.0.1:28788/events", "secret"});
    NetworkClient alice;
    ASSERT_TRUE(join(alice, "Alice"));
    // One player connected, a world that has ticked, and history growing in bytes.
    EXPECT_TRUE(waitFor([&] {
        alice.poll();
        for (const std::string& body : service.accepted()) {
            if (body.find("\"connections\":1,\"observers\":0") != std::string::npos &&
                body.find("\"historyTicks\":0,") == std::string::npos &&
                body.find("\"historyBytes\":0,") == std::string::npos) {
                return true;
            }
        }
        return false;
    }, 5000ms));
}
```

Run: `cmake --build cmake-build-debug --target server_tests` — Expected: FAIL to compile (`ServerHealth`, `setHealth`, 5-argument `toJson`).

- [ ] **Step 2: Process usage.** `server/process_usage.h`:

```cpp
#pragma once

#include <cstdint>

// The server process's own memory and CPU, for the health it reports: read from /proc on Linux (production), zero
// elsewhere.
struct ProcessUsage {
    std::int64_t rssBytes = 0;  // resident memory
    double cpuSeconds = 0;      // user + system CPU time since the process started
};

ProcessUsage currentProcessUsage();
```

`server/process_usage.cpp`:

```cpp
#include "process_usage.h"

#ifdef __linux__
#include <unistd.h>

#include <fstream>
#include <sstream>
#include <string>
#endif

ProcessUsage currentProcessUsage() {
    ProcessUsage usage;
#ifdef __linux__
    // /proc/self/stat: field 14 is utime, 15 stime (clock ticks), 24 rss (pages). The second field (the name, in
    // parentheses) may hold spaces, so fields are counted from after its closing parenthesis.
    std::ifstream file("/proc/self/stat");
    std::string stat((std::istreambuf_iterator<char>(file)), {});
    const auto close = stat.rfind(')');
    if (close == std::string::npos) return usage;
    std::istringstream fields(stat.substr(close + 2));  // starts at field 3
    std::string field;
    long long utime = 0, stime = 0, rss = 0;
    for (int i = 3; i <= 24 && fields >> field; ++i) {
        if (i == 14) utime = std::stoll(field);
        if (i == 15) stime = std::stoll(field);
        if (i == 24) rss = std::stoll(field);
    }
    usage.cpuSeconds = static_cast<double>(utime + stime) / static_cast<double>(sysconf(_SC_CLK_TCK));
    usage.rssBytes = rss * sysconf(_SC_PAGESIZE);
#endif
    return usage;
}
```

Add `server/process_usage.cpp` to `server_lib` in `CMakeLists.txt`.

- [ ] **Step 3: JSON.** In `stats_json.h` add before `toJson`:

```cpp
// How the game server is doing, sent with every batch (the stats service turns it into metrics).
struct ServerHealth {
    int connections = 0;             // open WebSocket connections: players, observers, and ones still saying Hello
    int observers = 0;               // monitoring probes watching
    std::int64_t ticks = 0;          // ticks sent since the server started
    int historyTicks = 0;            // the current world's age in ticks (0: no world)
    std::int64_t historyBytes = 0;   // what a player joining now downloads (Joined)
    std::int64_t rssBytes = 0;       // the server's resident memory
    double cpuSeconds = 0;           // the server's CPU time so far
};
```

and change the declaration to:

```cpp
// A batch as JSON: {"online":N,"events":[...],"health":{...}}. `at` (Unix ms) is when the batch was cut, `backlog`
// how many batches were waiting to go out.
std::string toJson(int online, const std::vector<StatEvent>& events, std::int64_t at, const ServerHealth& health,
                   std::size_t backlog);
```

In `stats_json.cpp`, the end of `toJson` becomes:

```cpp
    out += "],\"health\":{";
    appendField(out, "at", at);
    out += ',';
    appendField(out, "connections", health.connections);
    out += ',';
    appendField(out, "observers", health.observers);
    out += ',';
    appendField(out, "ticks", health.ticks);
    out += ',';
    appendField(out, "historyTicks", health.historyTicks);
    out += ',';
    appendField(out, "historyBytes", health.historyBytes);
    out += ',';
    appendField(out, "backlog", static_cast<std::int64_t>(backlog));
    out += ',';
    appendField(out, "rssBytes", health.rssBytes);
    char cpu[32];
    std::snprintf(cpu, sizeof cpu, ",\"cpuSeconds\":%.3f}}", health.cpuSeconds);
    out += cpu;
    return out;
```

(and the old `out += "]}";` goes). Update the function signature in the `.cpp` to match.

- [ ] **Step 4: Reporter.** In `stats_reporter.h` add `#include "stats_json.h"` (already there) and:

```cpp
    void setHealth(const ServerHealth& health);
```

and a private member `ServerHealth health_;  // guarded by mutex_`. In `.cpp`:

```cpp
void StatsReporter::setHealth(const ServerHealth& health) {
    const std::lock_guard lock(mutex_);
    health_ = health;
}
```

In `run()`, the batch line becomes:

```cpp
            const auto at = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::system_clock::now().time_since_epoch())
                                .count();
            batches_.push_back(toJson(online_, std::exchange(pending_, {}), at, health_, batches_.size()));
```

Update the class comment: "(even an empty one: it doubles as a heartbeat with the players online and the server's health)".

- [ ] **Step 5: Server publishes health every tick of the clock.** In `server.h`: private members `std::int64_t ticksSent_ = 0;` and `std::int64_t historyBytes_ = 0;  // the current world's Joined, in bytes`, and a method `void publishHealth();  // stats only: online count and health, on every tick of the clock`. In `server.cpp`:
  - `run()` tick branch: `tickWorld(); answerPings(); publishHealth();`
  - `tickWorld()`: remove `reporter_->setOnline(playersOnline());`. After `const protocol::TickMessage message{world_->nextTick()};` add:

```cpp
    ++ticksSent_;
    historyBytes_ += static_cast<std::int64_t>(protocol::encode(message).size()) - 1;  // the tick, without its type byte
```

  - `startWorld()`: after creating `world_`, `historyBytes_ = 9;  // Joined's type, seed and tick count`.
  - `disconnect()` where the world ends (`world_.reset();`): add `historyBytes_ = 0;`.
  - New:

```cpp
void Server::publishHealth() {
    if (!reporter_) return;
    const ProcessUsage usage = currentProcessUsage();
    reporter_->setOnline(playersOnline());
    reporter_->setHealth({.connections = static_cast<int>(clients_.size()),
                          .observers = observersOnline(),
                          .ticks = ticksSent_,
                          .historyTicks = world_ ? static_cast<int>(world_->history().size()) : 0,
                          .historyBytes = world_ ? historyBytes_ : 0,
                          .rssBytes = usage.rssBytes,
                          .cpuSeconds = usage.cpuSeconds});
}
```

  `#include "process_usage.h"`. Reading `/proc/self/stat` 60 times a second is wasteful: only call `currentProcessUsage()` once a second — keep a `Clock::time_point lastUsage_` and a cached `ProcessUsage usage_`, refreshed when `Clock::now() - lastUsage_ >= 1s`.

- [ ] **Step 6: Run.** Run: `cmake --build cmake-build-debug --target server_tests && ./cmake-build-debug/server_tests --gtest_filter=Stats*:ServerStats*` — Expected: PASS. Then `ctest --test-dir cmake-build-debug --output-on-failure` — Expected: PASS.

- [ ] **Step 7: Commit.**
```bash
git add CMakeLists.txt server/process_usage.h server/process_usage.cpp server/stats_json.h server/stats_json.cpp server/stats_reporter.h server/stats_reporter.cpp server/server.h server/server.cpp server/tests/stats_test.cpp
git commit -m "feat(server): report health with every stats batch"
```

---

### Task 4: Stats service `/metrics`

**Files:**
- Modify: `stats/internal/ingest/event.go` (`Health`, `Batch.Health`)
- Modify: `stats/internal/ingest/validate.go`, `validate_test.go`
- Create: `stats/internal/metrics/metrics.go`, `stats/internal/metrics/metrics_test.go`
- Modify: `stats/internal/server/server.go`, `stats/internal/server/events_test.go`
- Modify: `stats/cmd/stats/main.go`
- Modify: `stats/go.mod`, `stats/go.sum`

**Interfaces:**
- Consumes: Task 3's batch JSON.
- Produces: `GET /metrics` on the stats service with the gauge/counter names below (Task 6 scrapes and graphs them). `server.New(st *store.Store, token string, public http.Handler, m *metrics.Metrics) *Server`.

Metric names (exact):
`minicraft_players_online`, `minicraft_connections`, `minicraft_observers`, `minicraft_ticks_total` (gauge), `minicraft_history_ticks`, `minicraft_history_bytes`, `minicraft_reporter_backlog`, `minicraft_process_resident_bytes`, `minicraft_process_cpu_seconds_total` (gauge), `minicraft_last_report_timestamp_seconds`, `stats_events_ingested_total{type}`, `stats_batches_total{result}` (`accepted|duplicate|invalid|unauthorized|error`), `stats_db_write_seconds` (histogram), plus `go_*` and `process_*`.

- [ ] **Step 1: Dependency.** Run: `cd stats && go get github.com/prometheus/client_golang@latest && go mod tidy`.

- [ ] **Step 2: Ingest types and validation, test first.** In `validate_test.go`'s table add:

```go
		{"health", Batch{Health: &Health{At: 1, Connections: 3, Ticks: 10, HistoryTicks: 5, HistoryBytes: 90}}, true},
		{"negative health", Batch{Health: &Health{At: 1, Connections: -1}}, false},
		{"health without a time", Batch{Health: &Health{}}, false},
```

(Match the table's existing field names; if the cases are `{name, batch, ok}`, these fit.) Run `go test ./internal/ingest/` — FAIL (undefined Health).

In `event.go`:

```go
// Batch is one POST from the game server. Online is how many players are in the world right now: batches come
// every second, empty or not, so it doubles as a heartbeat. Health is how the server is doing (absent from servers
// older than it).
type Batch struct {
	Online int     `json:"online"`
	Events []Event `json:"events"`
	Health *Health `json:"health"`
}

// Health is the game server's state when it cut the batch.
type Health struct {
	At           int64   `json:"at"` // Unix milliseconds, when the batch was cut
	Connections  int     `json:"connections"`
	Observers    int     `json:"observers"`
	Ticks        int64   `json:"ticks"`
	HistoryTicks int64   `json:"historyTicks"`
	HistoryBytes int64   `json:"historyBytes"`
	Backlog      int     `json:"backlog"`
	RSSBytes     int64   `json:"rssBytes"`
	CPUSeconds   float64 `json:"cpuSeconds"`
}
```

In `Validate`, after the online check:

```go
	if h := b.Health; h != nil {
		if h.At <= 0 || h.Connections < 0 || h.Connections > 10000 || h.Observers < 0 || h.Ticks < 0 ||
			h.HistoryTicks < 0 || h.HistoryBytes < 0 || h.Backlog < 0 || h.RSSBytes < 0 || h.CPUSeconds < 0 {
			return errors.New("health out of range")
		}
	}
```

Run — PASS.

- [ ] **Step 3: Metrics tests.** `stats/internal/metrics/metrics_test.go`:

```go
package metrics

import (
	"strings"
	"testing"
	"time"

	"github.com/prometheus/client_golang/prometheus/testutil"

	"github.com/AdnaneBJA/Minicraft/stats/internal/ingest"
)

func TestReportSetsGauges(t *testing.T) {
	m := New()
	m.Report(3, &ingest.Health{At: 1000, Connections: 4, Observers: 1, Ticks: 600, HistoryTicks: 500,
		HistoryBytes: 9000, Backlog: 2, RSSBytes: 4096, CPUSeconds: 1.5}, time.Unix(50, 0))
	want := `
# HELP minicraft_connections Open WebSocket connections to the game server.
# TYPE minicraft_connections gauge
minicraft_connections 4
# HELP minicraft_players_online Players in the world.
# TYPE minicraft_players_online gauge
minicraft_players_online 3
`
	if err := testutil.GatherAndCompare(m.Registry(), strings.NewReader(want),
		"minicraft_connections", "minicraft_players_online"); err != nil {
		t.Error(err)
	}
	if got := testutil.ToFloat64(m.lastReport); got != 50 {
		t.Errorf("last report %v", got)
	}
}

func TestOlderHealthDoesNotOverwrite(t *testing.T) {
	m := New()
	m.Report(5, &ingest.Health{At: 2000, Connections: 5}, time.Unix(10, 0))
	m.Report(1, &ingest.Health{At: 1000, Connections: 1}, time.Unix(11, 0)) // a retried old batch
	if got := testutil.ToFloat64(m.connections); got != 5 {
		t.Errorf("connections %v, want 5", got)
	}
	if got := testutil.ToFloat64(m.online); got != 5 {
		t.Errorf("online %v, want 5", got)
	}
	if got := testutil.ToFloat64(m.lastReport); got != 11 {
		t.Errorf("last report %v: an old batch still shows the game server is alive", got)
	}
}

func TestBatchWithoutHealthKeepsGauges(t *testing.T) {
	m := New()
	m.Report(2, &ingest.Health{At: 1000, Connections: 2}, time.Unix(10, 0))
	m.Report(9, nil, time.Unix(11, 0))
	if got := testutil.ToFloat64(m.connections); got != 2 {
		t.Errorf("connections %v", got)
	}
	if got := testutil.ToFloat64(m.online); got != 9 {
		t.Errorf("online %v: without health, online still comes from the batch", got)
	}
}

func TestCountsBatchesAndEvents(t *testing.T) {
	m := New()
	m.Batch("accepted")
	m.Batch("invalid")
	m.Events([]ingest.Event{{Type: ingest.TileBroken}, {Type: ingest.TileBroken}, {Type: ingest.ChatSent}})
	m.DBWrite(3 * time.Millisecond)
	if got := testutil.ToFloat64(m.batches.WithLabelValues("accepted")); got != 1 {
		t.Errorf("accepted %v", got)
	}
	if got := testutil.ToFloat64(m.events.WithLabelValues(ingest.TileBroken)); got != 2 {
		t.Errorf("TileBroken %v", got)
	}
	if n := testutil.CollectAndCount(m.dbWrite); n != 1 {
		t.Errorf("db write series %d", n)
	}
}
```

Run `go test ./internal/metrics/` — FAIL (package missing).

- [ ] **Step 4: Metrics package.** `stats/internal/metrics/metrics.go`:

```go
// Package metrics is what the stats service shows Prometheus at /metrics: the game server's health (from the
// batches it posts), and the service's own work.
package metrics

import (
	"net/http"
	"sync"
	"time"

	"github.com/prometheus/client_golang/prometheus"
	"github.com/prometheus/client_golang/prometheus/collectors"
	"github.com/prometheus/client_golang/prometheus/promhttp"

	"github.com/AdnaneBJA/Minicraft/stats/internal/ingest"
)

// Metrics holds every metric; safe for concurrent use.
type Metrics struct {
	reg *prometheus.Registry

	mu     sync.Mutex
	lastAt int64 // the newest health applied (its batch's cut time)

	online, connections, observers, ticks, historyTicks, historyBytes, backlog, rss, cpu prometheus.Gauge
	lastReport                                                                         prometheus.Gauge
	events, batches                                                                    *prometheus.CounterVec
	dbWrite                                                                            prometheus.Histogram
}

// New registers every metric, plus the Go runtime's and the process's.
func New() *Metrics {
	m := &Metrics{reg: prometheus.NewRegistry()}
	gauge := func(name, help string) prometheus.Gauge {
		g := prometheus.NewGauge(prometheus.GaugeOpts{Name: name, Help: help})
		m.reg.MustRegister(g)
		return g
	}
	m.online = gauge("minicraft_players_online", "Players in the world.")
	m.connections = gauge("minicraft_connections", "Open WebSocket connections to the game server.")
	m.observers = gauge("minicraft_observers", "Monitoring probes watching the world.")
	m.ticks = gauge("minicraft_ticks_total", "Ticks the game server sent since it started (use rate()).")
	m.historyTicks = gauge("minicraft_history_ticks", "The current world's age in ticks.")
	m.historyBytes = gauge("minicraft_history_bytes", "What a player joining now downloads, in bytes.")
	m.backlog = gauge("minicraft_reporter_backlog", "Stats batches waiting to go out on the game server.")
	m.rss = gauge("minicraft_process_resident_bytes", "The game server's resident memory.")
	m.cpu = gauge("minicraft_process_cpu_seconds_total", "The game server's CPU time (use rate()).")
	m.lastReport = gauge("minicraft_last_report_timestamp_seconds", "When the game server's last batch arrived.")
	m.events = prometheus.NewCounterVec(prometheus.CounterOpts{
		Name: "stats_events_ingested_total", Help: "Events in batches that were stored, by type."}, []string{"type"})
	m.batches = prometheus.NewCounterVec(prometheus.CounterOpts{
		Name: "stats_batches_total", Help: "Batches posted to /events, by result."}, []string{"result"})
	m.dbWrite = prometheus.NewHistogram(prometheus.HistogramOpts{
		Name: "stats_db_write_seconds", Help: "Time to store one batch.",
		Buckets: []float64{.001, .0025, .005, .01, .025, .05, .1, .25, .5, 1}})
	m.reg.MustRegister(m.events, m.batches, m.dbWrite,
		collectors.NewGoCollector(), collectors.NewProcessCollector(collectors.ProcessCollectorOpts{}))
	for _, result := range []string{"accepted", "duplicate", "invalid", "unauthorized", "error"} {
		m.batches.WithLabelValues(result) // every result shows from the start, at 0
	}
	return m
}

// Registry is where the metrics live (for tests).
func (m *Metrics) Registry() *prometheus.Registry { return m.reg }

// Handler serves the metrics in Prometheus's format.
func (m *Metrics) Handler() http.Handler { return promhttp.HandlerFor(m.reg, promhttp.HandlerOpts{}) }

// Report records a valid batch: it arrived `now`. Its health updates the gauges only if it's newer than the last one
// applied (a retried old batch must not roll them back); a batch without health only updates the online count.
func (m *Metrics) Report(online int, h *ingest.Health, now time.Time) {
	m.mu.Lock()
	defer m.mu.Unlock()
	m.lastReport.Set(float64(now.UnixMilli()) / 1000)
	if h == nil {
		m.online.Set(float64(online))
		return
	}
	if h.At <= m.lastAt {
		return
	}
	m.lastAt = h.At
	m.online.Set(float64(online))
	m.connections.Set(float64(h.Connections))
	m.observers.Set(float64(h.Observers))
	m.ticks.Set(float64(h.Ticks))
	m.historyTicks.Set(float64(h.HistoryTicks))
	m.historyBytes.Set(float64(h.HistoryBytes))
	m.backlog.Set(float64(h.Backlog))
	m.rss.Set(float64(h.RSSBytes))
	m.cpu.Set(h.CPUSeconds)
}

// Batch counts a POST to /events by its result.
func (m *Metrics) Batch(result string) { m.batches.WithLabelValues(result).Inc() }

// Events counts the events of a stored batch by type.
func (m *Metrics) Events(events []ingest.Event) {
	for _, e := range events {
		m.events.WithLabelValues(e.Type).Inc()
	}
}

// DBWrite records how long storing a batch took.
func (m *Metrics) DBWrite(d time.Duration) { m.dbWrite.Observe(d.Seconds()) }
```

Run `go test ./internal/metrics/` — PASS.

- [ ] **Step 5: Wire into the server, test first.** In `events_test.go`, `newServer` becomes:

```go
func newServer(t *testing.T) (*Server, *store.Store) {
	t.Helper()
	st, err := store.Open(context.Background(), testdb.New(t))
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(st.Close)
	return New(st, "secret", http.NotFoundHandler(), metrics.New()), st
}
```

and add:

```go
func TestMetricsEndpoint(t *testing.T) {
	srv, _ := newServer(t)
	batch := `{"online":1,"events":[],"health":{"at":5,"connections":2,"observers":1,"ticks":60,` +
		`"historyTicks":60,"historyBytes":600,"backlog":0,"rssBytes":1,"cpuSeconds":0.1}}`
	if rec := post(srv, "secret", batch); rec.Code != http.StatusOK {
		t.Fatalf("post: %d %s", rec.Code, rec.Body)
	}
	post(srv, "wrong", batch)
	rec := httptest.NewRecorder()
	srv.ServeHTTP(rec, httptest.NewRequest(http.MethodGet, "/metrics", nil))
	body := rec.Body.String()
	for _, want := range []string{
		"minicraft_connections 2", "minicraft_observers 1", `stats_batches_total{result="accepted"} 1`,
		`stats_batches_total{result="unauthorized"} 1`, "stats_db_write_seconds_count 1",
	} {
		if !strings.Contains(body, want) {
			t.Errorf("/metrics lacks %q", want)
		}
	}
}
```

Add the `metrics` import. Run `go test ./internal/server/` (needs `TEST_DATABASE_URL` or the embedded Postgres the package already uses) — FAIL (New's arity).

- [ ] **Step 6: Server changes.** In `server.go`: field `metrics *metrics.Metrics`; `New(st *store.Store, token string, public http.Handler, m *metrics.Metrics) *Server` stores it. `ServeHTTP`:

```go
	switch r.URL.Path {
	case "/events":
		s.events(w, r)
	case "/metrics":
		// Not routed by Caddy: only Prometheus, inside the Docker network, reaches it.
		s.metrics.Handler().ServeHTTP(w, r)
	default:
		s.public.ServeHTTP(w, r)
	}
```

In `events`: `s.metrics.Batch("unauthorized")` before the 401 return; `s.metrics.Batch("invalid")` before each 400 return; after a valid batch, next to the `s.online` update, `s.metrics.Report(batch.Online, batch.Health, s.now())`; time the store:

```go
	started := time.Now()
	applied, err := s.store.Apply(r.Context(), batch.Events)
	s.metrics.DBWrite(time.Since(started))
	if err != nil {
		s.metrics.Batch("error")
		...existing...
	}
	if len(batch.Events) > 0 && applied == 0 {
		s.metrics.Batch("duplicate")
	} else {
		s.metrics.Batch("accepted")
		s.metrics.Events(batch.Events)
	}
```

Update the package comment: "POST /events from the game server, GET /metrics for Prometheus, and the public pages." In `main.go`: `srv = server.New(st, token, public, metrics.New())` and add the import; add `/metrics` to the file comment's description.

- [ ] **Step 7: Run.** Run: `cd stats && go vet ./... && go test -count=1 ./...` — Expected: PASS.

- [ ] **Step 8: Commit.**
```bash
git add stats
git commit -m "feat(stats): Prometheus metrics for the game's health and the service's own work"
```

---

### Task 5: The probe

**Files:**
- Modify: `loadtest/internal/fakeserver/fakeserver.go` (observe, ping, refusal, reset)
- Create: `loadtest/probe/probe.go`, `loadtest/probe/probe_test.go`
- Create: `loadtest/cmd/probe/main.go`
- Create: `loadtest/Dockerfile`
- Modify: `loadtest/go.mod`, `loadtest/go.sum`, `loadtest/README.md`

**Interfaces:**
- Consumes: Task 1's `protocol.EncodeObserve`, `EncodeProbePing`, `ProbePong`, `ReadMessage`.
- Produces: `probe.Once(ctx context.Context, cfg probe.Config) probe.Run`; the probe's `/metrics` (`probe_runs_total{result}`, `probe_join_seconds`, `probe_latency_seconds`, `probe_tick_jitter_seconds`, `probe_last_success_timestamp_seconds`) scraped in Task 6. Histogram buckets include exactly `1` (join) and `0.05` (latency), which the SLO rules use.

- [ ] **Step 1: Fake server support.** In `fakeserver.go`:
  - `Server` fields: `RefuseObservers bool // Observe gets "Too many observers"` and `PongDelay time.Duration // extra wait before a pong goes out`. Private: `observers map[*observer]bool`.
  - Type:
    ```go
    type observer struct {
    	conn    *websocket.Conn
    	ctx     context.Context
    	pending []pendingPing
    }

    type pendingPing struct {
    	id int32
    	at time.Time
    }
    ```
  - `Start` initialises `observers: map[*observer]bool{}`.
  - In `serve`, inside the Hello loop, before `if err != nil || msg.Type != protocol.Hello`, handle `Observe`:
    ```go
    		if err == nil && msg.Type == protocol.Observe {
    			s.serveObserver(ctx, conn)
    			return
    		}
    ```
  - New:
    ```go
    // serveObserver: the world (Joined), then every tick, and a pong after the first tick past PongDelay.
    func (s *Server) serveObserver(ctx context.Context, conn *websocket.Conn) {
    	s.mu.Lock()
    	refuse, stalled := s.RefuseObservers, s.Stalled
    	s.mu.Unlock()
    	if stalled { // a wedged server never answers; read until the client gives up
    		for {
    			if _, _, err := conn.Read(ctx); err != nil {
    				return
    			}
    		}
    	}
    	if refuse {
    		_ = conn.Write(ctx, websocket.MessageBinary, protocol.EncodeError("Too many observers"))
    		return
    	}
    	_ = conn.Write(ctx, websocket.MessageBinary, protocol.EncodeJoined(7, s.History))
    	me := &observer{conn: conn, ctx: ctx}
    	s.mu.Lock()
    	s.observers[me] = true
    	s.mu.Unlock()
    	defer func() {
    		s.mu.Lock()
    		delete(s.observers, me)
    		s.mu.Unlock()
    	}()
    	for {
    		_, data, err := conn.Read(ctx)
    		if err != nil {
    			return
    		}
    		if msg, err := protocol.Decode(data); err == nil && msg.Type == protocol.ProbePing {
    			s.mu.Lock()
    			me.pending = append(me.pending, pendingPing{id: msg.PingID, at: time.Now()})
    			s.mu.Unlock()
    		}
    	}
    }

    // ResetWorld sends every observer a new Joined, as a world reset does.
    func (s *Server) ResetWorld() {
    	s.mu.Lock()
    	var targets []*observer
    	for o := range s.observers {
    		targets = append(targets, o)
    	}
    	s.mu.Unlock()
    	for _, o := range targets {
    		_ = o.conn.Write(o.ctx, websocket.MessageBinary, protocol.EncodeJoined(8, nil))
    	}
    }
    ```
  - In `tickLoop`, after collecting player `targets` (still under the lock), collect observers and due pongs:
    ```go
    			type pong struct {
    				o  *observer
    				id int32
    			}
    			var watchers []*observer
    			var pongs []pong
    			for o := range s.observers {
    				watchers = append(watchers, o)
    				for len(o.pending) > 0 && now.Sub(o.pending[0].at) >= s.PongDelay {
    					pongs = append(pongs, pong{o, o.pending[0].id})
    					o.pending = o.pending[1:]
    				}
    			}
    ```
    and after writing `data` to the players, write it to each of `watchers`, then each pong's `protocol.EncodeProbePong(p.id)` to `p.o.conn` (each write with the same 1 s timeout context as the players').

- [ ] **Step 2: Probe tests.** `loadtest/probe/probe_test.go`:

```go
package probe

import (
	"context"
	"testing"
	"time"

	"github.com/AdnaneBJA/Minicraft/loadtest/internal/fakeserver"
)

func config(url string) Config {
	return Config{URL: url, Pings: 5, PingEvery: 50 * time.Millisecond, Timeout: 3 * time.Second}
}

func TestOnceOK(t *testing.T) {
	srv := fakeserver.Start(t)
	srv.PongDelay = 20 * time.Millisecond
	run := Once(context.Background(), config(srv.URL))
	if run.Result != OK {
		t.Fatalf("result %s: %v", run.Result, run.Err)
	}
	if run.Join <= 0 || run.Join > time.Second {
		t.Errorf("join %v", run.Join)
	}
	if len(run.Latencies) != 5 {
		t.Fatalf("%d latencies", len(run.Latencies))
	}
	for _, l := range run.Latencies {
		if l < 20*time.Millisecond || l > 200*time.Millisecond { // the delay, plus up to a tick and scheduling
			t.Errorf("latency %v", l)
		}
	}
	if len(run.TickGaps) == 0 {
		t.Error("no tick gaps recorded")
	}
}

func TestOnceRefused(t *testing.T) {
	srv := fakeserver.Start(t)
	srv.RefuseObservers = true
	if run := Once(context.Background(), config(srv.URL)); run.Result != Refused {
		t.Errorf("result %s: %v", run.Result, run.Err)
	}
}

func TestOnceConnectFailed(t *testing.T) {
	if run := Once(context.Background(), config("ws://127.0.0.1:1")); run.Result != ConnectFailed {
		t.Errorf("result %s: %v", run.Result, run.Err)
	}
}

func TestOnceStalledServerTimesOut(t *testing.T) {
	srv := fakeserver.Start(t)
	srv.Stall()
	cfg := config(srv.URL)
	cfg.Timeout = 500 * time.Millisecond
	started := time.Now()
	run := Once(context.Background(), cfg)
	if run.Result != Timeout {
		t.Errorf("result %s: %v", run.Result, run.Err)
	}
	if took := time.Since(started); took > 2*time.Second {
		t.Errorf("took %v: the deadline wasn't kept", took)
	}
}

func TestOnceLostPongsTimeOut(t *testing.T) {
	srv := fakeserver.Start(t)
	srv.PongDelay = time.Hour // pings never answered
	cfg := config(srv.URL)
	cfg.Timeout = 700 * time.Millisecond
	if run := Once(context.Background(), cfg); run.Result != Timeout {
		t.Errorf("result %s: %v", run.Result, run.Err)
	}
}

func TestOnceSurvivesWorldReset(t *testing.T) {
	srv := fakeserver.Start(t)
	cfg := config(srv.URL)
	cfg.PingEvery = 100 * time.Millisecond
	go func() {
		time.Sleep(150 * time.Millisecond)
		srv.ResetWorld()
	}()
	if run := Once(context.Background(), cfg); run.Result != OK {
		t.Errorf("result %s: %v", run.Result, run.Err)
	}
}
```

Run: `cd loadtest && go test ./probe/` — FAIL (package missing).

- [ ] **Step 3: The probe package.** `loadtest/probe/probe.go`:

```go
// Package probe plays the hosted game the way monitoring needs: it joins as a hidden observer (never in the world),
// times the join, then times pings that the server answers right after its next tick (the path a key press takes).
package probe

import (
	"context"
	"errors"
	"fmt"
	"sync"
	"time"

	"github.com/coder/websocket"

	"github.com/AdnaneBJA/Minicraft/loadtest/protocol"
)

// Result is how a run ended.
type Result string

// The results, as the probe_runs_total label.
const (
	OK            Result = "ok"
	ConnectFailed Result = "connect_failed"
	Refused       Result = "refused"    // the server said no (Error)
	JoinFailed    Result = "join_failed" // the connection broke before the world arrived
	Timeout       Result = "timeout"    // the deadline passed (no world, or pongs missing)
)

// Results lists them all.
var Results = []Result{OK, ConnectFailed, Refused, JoinFailed, Timeout}

// Config is one run's settings.
type Config struct {
	URL       string
	Pings     int
	PingEvery time.Duration
	Timeout   time.Duration // the whole run's deadline
}

// Run is what one run measured.
type Run struct {
	Result    Result
	Err       error
	Join      time.Duration   // from dialing to having the whole world
	Latencies []time.Duration // ping -> its pong, one per answered ping
	TickGaps  []time.Duration // between consecutive ticks
}

const maxMessage = 1 << 20 // anything but the history (streamed) is far smaller

// Once does one run. It never panics on a bad server; every failure is a Result.
func Once(ctx context.Context, cfg Config) Run {
	ctx, cancel := context.WithTimeout(ctx, cfg.Timeout)
	defer cancel()
	started := time.Now()
	fail := func(r Result, err error) Run {
		if ctx.Err() != nil {
			r = Timeout
		}
		return Run{Result: r, Err: err}
	}

	conn, _, err := websocket.Dial(ctx, cfg.URL, nil)
	if err != nil {
		return fail(ConnectFailed, err)
	}
	defer conn.CloseNow()
	conn.SetReadLimit(-1) // ReadMessage enforces the limits itself
	if err := conn.Write(ctx, websocket.MessageBinary, protocol.EncodeObserve()); err != nil {
		return fail(JoinFailed, err)
	}
	for joined := false; !joined; {
		msg, err := read(ctx, conn)
		if err != nil {
			return fail(JoinFailed, err)
		}
		switch msg.Type {
		case protocol.Error:
			return Run{Result: Refused, Err: errors.New(msg.Text)}
		case protocol.Joined:
			joined = true
		}
	}
	run := Run{Result: OK, Join: time.Since(started)}

	// Pings go out on their own; the reads record ticks and pongs until every ping is answered.
	var mu sync.Mutex
	sent := map[int32]time.Time{}
	go func() {
		for i := 1; i <= cfg.Pings; i++ {
			mu.Lock()
			sent[int32(i)] = time.Now()
			mu.Unlock()
			if conn.Write(ctx, websocket.MessageBinary, protocol.EncodeProbePing(int32(i))) != nil {
				return
			}
			select {
			case <-ctx.Done():
				return
			case <-time.After(cfg.PingEvery):
			}
		}
	}()
	var lastTick time.Time
	for len(run.Latencies) < cfg.Pings {
		msg, err := read(ctx, conn)
		if err != nil { // keep what was measured; the run still failed
			failed := fail(JoinFailed, fmt.Errorf("after %d of %d pongs: %w", len(run.Latencies), cfg.Pings, err))
			run.Result, run.Err = failed.Result, failed.Err
			return run
		}
		now := time.Now()
		switch msg.Type {
		case protocol.Tick:
			if !lastTick.IsZero() {
				run.TickGaps = append(run.TickGaps, now.Sub(lastTick))
			}
			lastTick = now
		case protocol.Joined:
			lastTick = time.Time{} // a world reset: the tick clock restarts
		case protocol.ProbePong:
			mu.Lock()
			at, ok := sent[msg.PingID]
			delete(sent, msg.PingID)
			mu.Unlock()
			if ok {
				run.Latencies = append(run.Latencies, now.Sub(at))
			}
		}
	}
	_ = conn.Close(websocket.StatusNormalClosure, "")
	return run
}

func read(ctx context.Context, conn *websocket.Conn) (protocol.Message, error) {
	_, r, err := conn.Reader(ctx)
	if err != nil {
		return protocol.Message{}, err
	}
	msg, _, err := protocol.ReadMessage(r, maxMessage)
	return msg, err
}
```

Note the server answers at most one pending ping per observer (a newer one replaces the older). With `PingEvery` ≥ one tick (16.7 ms) every ping is answered; the command-line default is 100 ms. Run: `go test -race ./probe/` — Expected: PASS.

- [ ] **Step 4: The command.** Run `cd loadtest && go get github.com/prometheus/client_golang@latest`. `loadtest/cmd/probe/main.go`:

```go
// probe: watches the hosted game from the outside, forever. Every --every it joins as a hidden observer, times the
// join and --pings round trips through the server's tick loop, and serves the results to Prometheus on --listen.
//
//	go run ./cmd/probe --url wss://minicraft.example.org
package main

import (
	"context"
	"errors"
	"flag"
	"log"
	"math"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/prometheus/client_golang/prometheus"
	"github.com/prometheus/client_golang/prometheus/collectors"
	"github.com/prometheus/client_golang/prometheus/promhttp"

	"github.com/AdnaneBJA/Minicraft/loadtest/probe"
)

const tickLength = time.Second / 60

func main() {
	url := flag.String("url", "", "the game's WebSocket URL (required), e.g. wss://minicraft.example.org")
	every := flag.Duration("every", 30*time.Second, "time between runs")
	pings := flag.Int("pings", 10, "pings per run")
	pingEvery := flag.Duration("ping-every", 100*time.Millisecond, "time between pings (at least a tick)")
	timeout := flag.Duration("timeout", 10*time.Second, "one run's deadline")
	listen := flag.String("listen", ":9100", "where to serve /metrics")
	flag.Parse()
	if *url == "" {
		flag.Usage()
		os.Exit(2)
	}

	reg := prometheus.NewRegistry()
	runs := prometheus.NewCounterVec(prometheus.CounterOpts{
		Name: "probe_runs_total", Help: "Probe runs, by how they ended."}, []string{"result"})
	join := prometheus.NewHistogram(prometheus.HistogramOpts{
		Name: "probe_join_seconds", Help: "From connecting to having the whole world.",
		Buckets: []float64{.01, .025, .05, .1, .25, .5, 1, 2.5, 5, 10}})
	latency := prometheus.NewHistogram(prometheus.HistogramOpts{
		Name: "probe_latency_seconds", Help: "A ping to its pong, sent after the server's next tick.",
		Buckets: []float64{.002, .005, .01, .015, .02, .03, .05, .1, .25, .5, 1}})
	jitter := prometheus.NewHistogram(prometheus.HistogramOpts{
		Name: "probe_tick_jitter_seconds", Help: "How far the gap between two ticks strays from 16.7 ms.",
		Buckets: []float64{.0005, .001, .002, .005, .01, .02, .05, .1, .5}})
	lastSuccess := prometheus.NewGauge(prometheus.GaugeOpts{
		Name: "probe_last_success_timestamp_seconds", Help: "When a run last ended ok."})
	reg.MustRegister(runs, join, latency, jitter, lastSuccess,
		collectors.NewGoCollector(), collectors.NewProcessCollector(collectors.ProcessCollectorOpts{}))
	for _, r := range probe.Results {
		runs.WithLabelValues(string(r)) // every result shows from the start, at 0
	}

	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()
	mux := http.NewServeMux()
	mux.Handle("/metrics", promhttp.HandlerFor(reg, promhttp.HandlerOpts{}))
	srv := &http.Server{Addr: *listen, Handler: mux, ReadHeaderTimeout: 5 * time.Second}
	go func() {
		if err := srv.ListenAndServe(); !errors.Is(err, http.ErrServerClosed) {
			log.Fatal(err)
		}
	}()
	log.Printf("probing %s every %v; metrics on %s", *url, *every, *listen)

	cfg := probe.Config{URL: *url, Pings: *pings, PingEvery: *pingEvery, Timeout: *timeout}
	ticker := time.NewTicker(*every)
	defer ticker.Stop()
	for ctx.Err() == nil {
		run := probe.Once(ctx, cfg)
		if ctx.Err() != nil {
			break
		}
		runs.WithLabelValues(string(run.Result)).Inc()
		if run.Join > 0 {
			join.Observe(run.Join.Seconds())
		}
		for _, l := range run.Latencies {
			latency.Observe(l.Seconds())
		}
		for _, g := range run.TickGaps {
			jitter.Observe(math.Abs((g - tickLength).Seconds()))
		}
		if run.Result == probe.OK {
			lastSuccess.SetToCurrentTime()
		} else {
			log.Printf("run %s: %v", run.Result, run.Err)
		}
		select {
		case <-ctx.Done():
		case <-ticker.C:
		}
	}
	shutdown, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	_ = srv.Shutdown(shutdown)
}
```

- [ ] **Step 5: Try it against the real server.** Build and start the server locally (`./cmake-build-debug/minicraft-server 7777` in one shell), then run `cd loadtest && go run ./cmd/probe --url ws://localhost:7777 --every 5s` and in another shell `curl -s localhost:9100/metrics | grep probe_`. Expected: `probe_runs_total{result="ok"}` counting up, `probe_latency_seconds_count` growing by 10 per run, `probe_join_seconds_count` growing by 1. The server log shows "Connection N is an observer". Stop both.

- [ ] **Step 6: Dockerfile.** `loadtest/Dockerfile`:

```dockerfile
# The monitoring probe in a container. Build from the repository root:
#   docker build -f loadtest/Dockerfile -t minicraft-probe .
FROM golang:1.26-alpine AS build
WORKDIR /src/loadtest
COPY loadtest/go.mod loadtest/go.sum ./
RUN go mod download
COPY loadtest/ ./
RUN CGO_ENABLED=0 go build -trimpath -ldflags="-s -w" -o /out/probe ./cmd/probe

FROM gcr.io/distroless/static-debian12:nonroot
COPY --from=build /out/probe /probe
EXPOSE 9100
ENTRYPOINT ["/probe"]
```

Run: `docker build -f loadtest/Dockerfile -t minicraft-probe .` — Expected: builds. (`distroless/static` carries CA certificates, which `wss://` needs.)

- [ ] **Step 7: README.** In `loadtest/README.md` add a short "Probe" section: what it does (hidden observer, join time, ping round trips through the tick loop), the flags, the metrics list, and that it runs in production via `deploy/docker-compose.yml`.

- [ ] **Step 8: Run all Go.** Run: `cd loadtest && go vet ./... && go test -race -count=1 ./...` — Expected: PASS.

- [ ] **Step 9: Commit.**
```bash
git add loadtest
git commit -m "feat(loadtest): a monitoring probe that watches the game as a hidden observer"
```

---

### Task 6: Prometheus, Grafana and the deployment

**Files:**
- Create: `deploy/prometheus/prometheus.yml`, `deploy/prometheus/slo-rules.yml`, `deploy/prometheus/slo-rules.test.yml`
- Create: `deploy/grafana/provisioning/datasources/prometheus.yml`
- Create: `deploy/grafana/provisioning/dashboards/minicraft.yml`
- Create: `deploy/grafana/provisioning/alerting/slo.yml`
- Create: `deploy/grafana/dashboards/player-experience.json`, `deploy/grafana/dashboards/server-internals.json`
- Modify: `deploy/docker-compose.yml`, `deploy/Caddyfile`
- Modify: `.github/workflows/tests.yml`

**Interfaces:**
- Consumes: the metric names from Tasks 4 and 5.
- Produces: recording rules `slo:probe_success:ratio_1h`, `slo:probe_join_under_1s:ratio_1h`, `slo:probe_latency_under_50ms:ratio_1h`, `slo:game_report_age_seconds`, `slo:targets_down`; Grafana data source uid `prometheus`.

- [ ] **Step 1: SLO rule tests first.** `deploy/prometheus/slo-rules.test.yml`:

```yaml
# promtool test rules slo-rules.test.yml
rule_files:
  - slo-rules.yml
evaluation_interval: 1m
tests:
  - name: all good
    interval: 1m
    input_series:
      - series: 'probe_runs_total{result="ok"}'
        values: '0+2x120'
      - series: 'probe_runs_total{result="timeout"}'
        values: '0x120'
      - series: 'probe_join_seconds_bucket{le="1"}'
        values: '0+2x120'
      - series: 'probe_join_seconds_count'
        values: '0+2x120'
      - series: 'probe_latency_seconds_bucket{le="0.05"}'
        values: '0+20x120'
      - series: 'probe_latency_seconds_count'
        values: '0+20x120'
    promql_expr_test:
      - expr: slo:probe_success:ratio_1h
        eval_time: 2h
        exp_samples:
          - value: 1
      - expr: slo:probe_join_under_1s:ratio_1h
        eval_time: 2h
        exp_samples:
          - value: 1
      - expr: slo:probe_latency_under_50ms:ratio_1h
        eval_time: 2h
        exp_samples:
          - value: 1
  - name: failures and slow pings
    interval: 1m
    input_series:
      # 2 runs a minute: 1 in 10 ends in a timeout.
      - series: 'probe_runs_total{result="ok"}'
        values: '0+1.8x120'
      - series: 'probe_runs_total{result="timeout"}'
        values: '0+0.2x120'
      # 20 pings a minute, 15 of them under 50 ms.
      - series: 'probe_latency_seconds_bucket{le="0.05"}'
        values: '0+15x120'
      - series: 'probe_latency_seconds_count'
        values: '0+20x120'
    promql_expr_test:
      - expr: slo:probe_success:ratio_1h
        eval_time: 2h
        exp_samples:
          - value: 0.9
      - expr: slo:probe_latency_under_50ms:ratio_1h
        eval_time: 2h
        exp_samples:
          - value: 0.75
  - name: freshness
    interval: 1m
    input_series:
      - series: 'minicraft_last_report_timestamp_seconds'
        values: '60 120 180 180 180'   # the game server stops reporting after 3 minutes
      - series: 'up{job="stats"}'
        values: '1 1 1 1 1'
      - series: 'up{job="probe"}'
        values: '1 1 1 0 0'
    promql_expr_test:
      - expr: slo:game_report_age_seconds
        eval_time: 4m
        exp_samples:
          - value: 60
      - expr: slo:targets_down
        eval_time: 4m
        exp_samples:
          - value: 1
```

- [ ] **Step 2: The rules.** `deploy/prometheus/slo-rules.yml`:

```yaml
# The SLOs, as ratios over the last hour (Grafana's alert rules compare them with the targets). Unit-tested:
#   promtool test rules slo-rules.test.yml
groups:
  - name: slo
    rules:
      - record: slo:probe_success:ratio_1h
        expr: sum(increase(probe_runs_total{result="ok"}[1h])) / sum(increase(probe_runs_total[1h]))
      - record: slo:probe_join_under_1s:ratio_1h
        expr: sum(increase(probe_join_seconds_bucket{le="1"}[1h])) / sum(increase(probe_join_seconds_count[1h]))
      - record: slo:probe_latency_under_50ms:ratio_1h
        expr: >-
          sum(increase(probe_latency_seconds_bucket{le="0.05"}[1h]))
          / sum(increase(probe_latency_seconds_count[1h]))
      - record: slo:game_report_age_seconds
        expr: time() - max(minicraft_last_report_timestamp_seconds)
      - record: slo:targets_down
        expr: count(up == 0) or vector(0)
```

Run (Docker): `docker run --rm -v "$PWD/deploy/prometheus:/p" -w /p --entrypoint promtool prom/prometheus:v3.5.0 test rules slo-rules.test.yml`
Expected: `SUCCESS`. (Git Bash on Windows: prefix with `MSYS_NO_PATHCONV=1`.) If `increase()` extrapolation makes a ratio come out as e.g. 0.8999, keep the inputs linear as above (they are) — the ratios of two linear series are exact.

- [ ] **Step 3: Prometheus config.** `deploy/prometheus/prometheus.yml`:

```yaml
# What Prometheus scrapes (inside the Docker network; nothing here is routed by Caddy).
global:
  scrape_interval: 15s
  evaluation_interval: 1m
rule_files:
  - slo-rules.yml
scrape_configs:
  - job_name: stats      # the game server's health (via its batches) and the stats service's own metrics
    static_configs:
      - targets: ["stats:8080"]
  - job_name: probe      # the synthetic player
    static_configs:
      - targets: ["probe:9100"]
  - job_name: prometheus
    static_configs:
      - targets: ["localhost:9090"]
```

Run: `docker run --rm -v "$PWD/deploy/prometheus:/p" -w /p --entrypoint promtool prom/prometheus:v3.5.0 check config prometheus.yml` — Expected: `SUCCESS` for the config and the rule file.

- [ ] **Step 4: Grafana provisioning.**

`deploy/grafana/provisioning/datasources/prometheus.yml`:
```yaml
apiVersion: 1
datasources:
  - name: Prometheus
    uid: prometheus
    type: prometheus
    access: proxy
    url: http://prometheus:9090
    isDefault: true
    editable: false
```

`deploy/grafana/provisioning/dashboards/minicraft.yml`:
```yaml
apiVersion: 1
providers:
  - name: minicraft
    folder: Minicraft
    type: file
    disableDeletion: true
    allowUiUpdates: false
    options:
      path: /etc/grafana/dashboards
```

`deploy/grafana/provisioning/alerting/slo.yml` — one rule per SLO, each thresholding a recording rule; dashboard only, so no contact points or policies are provisioned:
```yaml
apiVersion: 1
groups:
  - orgId: 1
    name: SLOs
    folder: Minicraft
    interval: 1m
    rules:
      - uid: slo-availability
        title: Availability below 99%
        condition: B
        for: 5m
        noDataState: NoData
        execErrState: Error
        annotations:
          summary: Fewer than 99% of probe runs succeeded over the last hour.
        data:
          - refId: A
            relativeTimeRange: { from: 600, to: 0 }
            datasourceUid: prometheus
            model: { refId: A, expr: "slo:probe_success:ratio_1h", instant: true }
          - refId: B
            datasourceUid: __expr__
            model:
              refId: B
              type: threshold
              expression: A
              conditions: [{ evaluator: { type: lt, params: [0.99] } }]
      - uid: slo-join
        title: Joins over 1 s
        condition: B
        for: 5m
        noDataState: NoData
        execErrState: Error
        annotations:
          summary: Fewer than 99% of joins finished within 1 s over the last hour.
        data:
          - refId: A
            relativeTimeRange: { from: 600, to: 0 }
            datasourceUid: prometheus
            model: { refId: A, expr: "slo:probe_join_under_1s:ratio_1h", instant: true }
          - refId: B
            datasourceUid: __expr__
            model:
              refId: B
              type: threshold
              expression: A
              conditions: [{ evaluator: { type: lt, params: [0.99] } }]
      - uid: slo-latency
        title: Latency over 50 ms
        condition: B
        for: 5m
        noDataState: NoData
        execErrState: Error
        annotations:
          summary: Fewer than 99% of probe pings were answered within 50 ms over the last hour.
        data:
          - refId: A
            relativeTimeRange: { from: 600, to: 0 }
            datasourceUid: prometheus
            model: { refId: A, expr: "slo:probe_latency_under_50ms:ratio_1h", instant: true }
          - refId: B
            datasourceUid: __expr__
            model:
              refId: B
              type: threshold
              expression: A
              conditions: [{ evaluator: { type: lt, params: [0.99] } }]
      - uid: slo-freshness
        title: Game server silent or a target down
        condition: C
        for: 1m
        noDataState: Alerting
        execErrState: Error
        annotations:
          summary: The game server hasn't reported for 30 s, or Prometheus can't scrape a target.
        data:
          - refId: A
            relativeTimeRange: { from: 600, to: 0 }
            datasourceUid: prometheus
            model: { refId: A, expr: "slo:game_report_age_seconds", instant: true }
          - refId: B
            relativeTimeRange: { from: 600, to: 0 }
            datasourceUid: prometheus
            model: { refId: B, expr: "slo:targets_down", instant: true }
          - refId: C
            datasourceUid: __expr__
            model: { refId: C, type: math, expression: "$A > 30 || $B > 0" }
```

- [ ] **Step 5: Dashboards.** Write the two dashboard JSON files by hand (schemaVersion 39, `"uid"` set, `"editable": false`, `"refresh": "30s"`, `"time": {"from": "now-6h", "to": "now"}`, data source `{"type": "prometheus", "uid": "prometheus"}` on every panel). Panels and their exact queries:

`player-experience.json` (uid `minicraft-player`, title "Minicraft: player experience"):
| Panel | Type | Query |
|---|---|---|
| About | text (markdown) | "A probe on the server's own machine joins the world every 30 s as a hidden observer and times 10 pings through the tick loop. It measures the server, Caddy and TLS, not the internet distance to a player (add your own round trip, e.g. 30 ms in the same region)." |
| Probe success (1 h) | stat, unit percentunit, thresholds red < 0.99 green | `slo:probe_success:ratio_1h` |
| Joins under 1 s (1 h) | stat, percentunit, red < 0.99 | `slo:probe_join_under_1s:ratio_1h` |
| Pings under 50 ms (1 h) | stat, percentunit, red < 0.99 | `slo:probe_latency_under_50ms:ratio_1h` |
| Last report from the game | stat, unit s, red > 30 | `slo:game_report_age_seconds` |
| Probe runs by result | timeseries, stacked bars | `sum by (result) (increase(probe_runs_total[5m]))` |
| Join time | timeseries, unit s | `histogram_quantile(0.5, sum by (le) (rate(probe_join_seconds_bucket[15m])))` legend p50; same with 0.99, legend p99 |
| Input latency (probe) | timeseries, unit s | `histogram_quantile(0.5, sum by (le) (rate(probe_latency_seconds_bucket[15m])))` p50; 0.99 p99 |
| Tick jitter p99 | timeseries, unit s | `histogram_quantile(0.99, sum by (le) (rate(probe_tick_jitter_seconds_bucket[15m])))` |
| Tick rate | timeseries, unit "ticks/s" | `rate(minicraft_ticks_total[1m])` |
| Alerts | alertlist (state filter: firing, pending, normal) | — |

`server-internals.json` (uid `minicraft-server`, title "Minicraft: server internals"):
| Panel | Type | Query |
|---|---|---|
| Players, connections, observers | timeseries | `minicraft_players_online`, `minicraft_connections`, `minicraft_observers` |
| World history (what a joiner downloads) | timeseries, unit bytes, right axis ticks | `minicraft_history_bytes`; `minicraft_history_ticks` |
| Stats ingest by type | timeseries, stacked | `sum by (type) (rate(stats_events_ingested_total[5m]))` |
| Batches by result | timeseries | `sum by (result) (rate(stats_batches_total[5m]))` |
| DB write time p99 | timeseries, unit s | `histogram_quantile(0.99, sum by (le) (rate(stats_db_write_seconds_bucket[5m])))` |
| Stats backlog on the game server | timeseries | `minicraft_reporter_backlog` |
| Memory | timeseries, unit bytes | `minicraft_process_resident_bytes` (game server); `process_resident_memory_bytes{job="stats"}`; `process_resident_memory_bytes{job="probe"}`; `process_resident_memory_bytes{job="prometheus"}` |
| CPU (cores) | timeseries | `rate(minicraft_process_cpu_seconds_total[1m])` (game server); `rate(process_cpu_seconds_total[1m])` by job |
| Scrape targets | stat, per series, red = 0 | `up` legend `{{job}}` |

Each panel JSON follows this shape (copy and vary per row):
```json
{
  "type": "timeseries",
  "title": "Join time",
  "datasource": { "type": "prometheus", "uid": "prometheus" },
  "gridPos": { "x": 0, "y": 8, "w": 12, "h": 8 },
  "fieldConfig": { "defaults": { "unit": "s" }, "overrides": [] },
  "targets": [
    { "refId": "A", "expr": "histogram_quantile(0.5, sum by (le) (rate(probe_join_seconds_bucket[15m])))", "legendFormat": "p50" },
    { "refId": "B", "expr": "histogram_quantile(0.99, sum by (le) (rate(probe_join_seconds_bucket[15m])))", "legendFormat": "p99" }
  ]
}
```

Check they parse: `python -c "import json,sys; [json.load(open(f)) for f in sys.argv[1:]]" deploy/grafana/dashboards/*.json` — Expected: no output.

- [ ] **Step 6: Compose.** In `deploy/docker-compose.yml`, update the header comment ("...the stats service and its database, the monitoring (a probe, Prometheus and Grafana), behind Caddy..."; `.env` also holds `GRAFANA_ADMIN_PASSWORD`) and add:

```yaml
  probe:
    # Plays the game from the outside every 30 s, as a hidden observer, through Caddy and TLS like a player.
    build:
      context: ..
      dockerfile: loadtest/Dockerfile
    restart: unless-stopped
    depends_on:
      - caddy
    command: ["--url", "${PROBE_URL:-wss://${DOMAIN}}"]
    expose:
      - "9100"
    mem_limit: 32m

  prometheus:
    image: prom/prometheus:v3.5.0
    restart: unless-stopped
    command:
      - --config.file=/etc/prometheus/prometheus.yml
      - --storage.tsdb.path=/prometheus
      - --storage.tsdb.retention.time=15d
    volumes:
      - ./prometheus:/etc/prometheus:ro
      - prometheus_data:/prometheus
    expose:
      - "9090"
    mem_limit: 128m

  grafana:
    image: grafana/grafana:12.1.0
    restart: unless-stopped
    depends_on:
      - prometheus
    environment:
      GF_SERVER_ROOT_URL: https://${DOMAIN}/grafana/
      GF_SERVER_SERVE_FROM_SUB_PATH: "true"
      GF_SECURITY_ADMIN_PASSWORD: ${GRAFANA_ADMIN_PASSWORD:?set GRAFANA_ADMIN_PASSWORD in .env}
      GF_SECURITY_COOKIE_SECURE: "true"
      GF_AUTH_ANONYMOUS_ENABLED: "true"
      GF_AUTH_ANONYMOUS_ORG_ROLE: Viewer
      GF_USERS_ALLOW_SIGN_UP: "false"
      GF_ANALYTICS_REPORTING_ENABLED: "false"
      GF_ANALYTICS_CHECK_FOR_UPDATES: "false"
      GF_DASHBOARDS_DEFAULT_HOME_DASHBOARD_PATH: /etc/grafana/dashboards/player-experience.json
    volumes:
      - ./grafana/provisioning:/etc/grafana/provisioning:ro
      - ./grafana/dashboards:/etc/grafana/dashboards:ro
      - grafana_data:/var/lib/grafana
    expose:
      - "3000"
    mem_limit: 160m
```

Add `prometheus_data:` and `grafana_data:` under `volumes:`, and `grafana` to Caddy's `depends_on`.

- [ ] **Step 7: Caddy.** In `deploy/Caddyfile`, before the stats `handle`:

```
	# The monitoring dashboards: public and read-only (anonymous viewers). Prometheus is not routed.
	handle /grafana* {
		reverse_proxy grafana:3000
	}
```

and extend the top comment: "...serves the stats dashboard under /stats, the monitoring under /grafana, and passes...".

- [ ] **Step 8: Run the stack locally.** In `deploy/`, with a throwaway `.env` (never committed; check `deploy/.gitignore` or the root `.gitignore` covers `.env`):

```
DOMAIN=localhost
POSTGRES_PASSWORD=local
STATS_TOKEN=local
GRAFANA_ADMIN_PASSWORD=local
PROBE_URL=ws://server:7777
```

Run: `docker compose up -d --build`, wait a minute, then:
- `curl -sk https://localhost/grafana/api/health` → `"database": "ok"`.
- `curl -sk https://localhost/grafana/api/search` → both dashboards (anonymous works).
- `curl -sk -X POST https://localhost/grafana/api/dashboards/db -H "Content-Type: application/json" -d '{}'` → 401/403 (anonymous can't edit).
- `docker compose exec prometheus wget -qO- localhost:9090/api/v1/targets | grep -o '"health":"[a-z]*"'` → three `"health":"up"`.
- `curl -sk https://localhost/metrics` and `https://localhost/prometheus` → not Prometheus output (they go to the game server, which answers 400/426: not routed).
- Open `https://localhost/grafana/` in a browser: the player-experience dashboard loads with data; join and latency panels populate within two probe runs.
- `docker stats --no-stream` → probe < 32 MB, Prometheus < 128 MB, Grafana < 160 MB.

Then `docker compose down` (keeps volumes; that's fine locally).

- [ ] **Step 9: CI.** In `.github/workflows/tests.yml`:
  - `images` job, add a step: `- name: Build the probe image` / `run: docker build -f loadtest/Dockerfile -t minicraft-probe .`
  - New job:

```yaml
  monitoring:
    # Prometheus's config and the SLO rules (with their unit tests), and the Grafana files parse.
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v7
      - name: Prometheus config
        run: docker run --rm -v "$PWD/deploy/prometheus:/p" -w /p --entrypoint promtool prom/prometheus:v3.5.0 check config prometheus.yml
      - name: SLO rule tests
        run: docker run --rm -v "$PWD/deploy/prometheus:/p" -w /p --entrypoint promtool prom/prometheus:v3.5.0 test rules slo-rules.test.yml
      - name: Grafana files parse
        run: |
          python3 -c "import json,sys; [json.load(open(f)) for f in sys.argv[1:]]" deploy/grafana/dashboards/*.json
          python3 -c "import yaml,sys; [yaml.safe_load(open(f)) for f in sys.argv[1:]]" $(find deploy/grafana/provisioning -name '*.yml')
```

- [ ] **Step 10: Commit.**
```bash
git add deploy .github/workflows/tests.yml
git commit -m "feat(deploy): Prometheus, Grafana and the probe, with SLO rules"
```

---

### Task 7: Docs

**Files:**
- Create: `docs/adr/0005-observability.md`
- Modify: `README.md`, `deploy/README.md`, `docs/superpowers/specs/2026-10-09-observability-design.md`

- [ ] **Step 1: ADR 0005** in the style of 0004 (Context / Decision / Consequences, short bullets): why a synthetic probe (it fails when players would, and measures what they feel); why a hidden observer and a ping answered after the next tick (lockstep: anything in a tick is simulated by every client); why health rides the stats batch (Go, an existing link, no HTTP server in the C++ process; the cost: the game's metrics stop when the stats service does, which the freshness SLO catches); the SLOs and why those thresholds (on-box p99 was 20 ms → 50 ms leaves room for the burstable CPU; joins took 3 ms on the box, 183 ms over the internet → 1 s); recording rules unit-tested with promtool; the full-server change (Hello now says "Server is full"; the load test's ramp shows join refusals at 36 instead of refused connections); limits (the probe sits on the same machine, so no internet distance; alerts only on the dashboard).

- [ ] **Step 2: README.** Add an "Observability" section after "Performance": the Grafana link (`https://minicraft-adnane.duckdns.org/grafana`), one paragraph on the probe, the two dashboards and the four SLOs (as a small table), and a link to ADR 0005. Add `/grafana` to the architecture/deploy lines that list what Caddy serves, and the probe to the repository layout table (`loadtest/` row: "the load-testing bots and the monitoring probe").

- [ ] **Step 3: deploy/README.md.** The `.env` example gains `GRAFANA_ADMIN_PASSWORD=$(openssl rand -hex 16)`; "What this does" gains a monitoring bullet (`https://<domain>/grafana`, anonymous read-only, sign in as `admin` with that password to edit); a line under "Everyday operations": "Updating from a version without monitoring: add `GRAFANA_ADMIN_PASSWORD` to `.env`, then `git pull && docker compose up -d --build`." and a row "Monitoring logs | `docker compose logs -f probe prometheus grafana`".

- [ ] **Step 4: Spec.** Fold the "Spec adjustments made while planning" list from this plan into the spec's sections (Welcome, no world yet, pongs without a world, full server, game process usage, recording rules, stale online).

- [ ] **Step 5: Commit.**
```bash
git add docs README.md deploy/README.md
git commit -m "docs: observability ADR, README and deploy steps"
```

---

### Task 8: Ship

- [ ] **Step 1:** Push the branch and open a PR (changes only, no AI footer). Wait for CI: `tests`, `stats`, `images`, `loadtest`, `monitoring` all pass.
- [ ] **Step 2:** After the user merges, deploy (outward-facing: confirm with the user first). Over SSH on the production instance, in `~/Minicraft/deploy`:
  ```sh
  git pull
  echo "GRAFANA_ADMIN_PASSWORD=$(openssl rand -hex 16)" >> .env
  docker compose up -d --build
  ```
  Give the user the password privately (print it once with `grep GRAFANA .env` in their own SSH session; don't paste it into the conversation or any file in git).
- [ ] **Step 3:** Verify production: `https://minicraft-adnane.duckdns.org/grafana/` loads anonymously with data after two probe runs; `free -m` and `docker stats --no-stream` show the memory caps hold; the game still joins from the browser; the stats dashboard at `/stats` still works and shows no extra player.
