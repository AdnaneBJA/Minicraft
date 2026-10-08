# Live stats dashboard: Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A public `/stats` dashboard (Go + PostgreSQL) fed by the C++ game server, which replays the world's ticks and reports every action exactly once.

**Architecture:**
- **game-core:** records stat events (tile broken, item collected or crafted, mob or player killed, level reached), with credit to the right player.
- **minicraft-server:** runs a `StatsObserver` (its own `Simulation` fed the same ticks) and a `StatsReporter` thread that POSTs JSON batches to the Go service with retries.
- **Go service:** stores the events idempotently in PostgreSQL with tallies, and serves a JSON API plus a server-rendered dashboard.

**Tech Stack:**
- C++20, game-core, IXWebSocket `HttpClient`/`HttpServer`, GoogleTest;
- Go 1.25+ (`net/http`, `pgx/v5`, `embedded-postgres` for local tests), PostgreSQL 17;
- Chart.js 4.4.1 (cdnjs);
- Docker Compose, Caddy, GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-10-08-stats-dashboard-design.md`

## Global Constraints
- The game works exactly as before when `STATS_URL` is unset or the stats service is down.
- Each event is counted exactly once. Event ids: `<seed>-<tick>-<index>` for simulation events, `<seed>-server-<n>` for server events.
- `/events` needs `Authorization: Bearer <STATS_TOKEN>` and is never routed by Caddy.
- The reporter retries failed batches. The backlog is capped at 600 batches; past that the oldest are dropped and the drop is logged. A 4xx response drops the batch.
- Every batch carries `online` (players in the world). A batch goes out every second even with no events. `online` shows as 0 after 30 s of silence.
- Stat events must be deterministic: the same ticks give the same events.
- Commits: no Claude/AI lines. Branch `stats-dashboard`. Code style matches the surroundings.

## Review Focus
1. **A player who leaves before their last events are reported**: the names must still resolve (the observer keeps a name for every id it has seen in the world). Test: `ObserverNamesPlayersWhoLeft`.
2. **A world reset in the middle of a batch**: ids from the old and new worlds must not collide, because the seed is in the id. Test: `ResetGivesFreshIds`.
3. **Malicious or garbage POSTs to `/events`** with the right token: wrong types, huge counts, names over 12 characters. The whole batch is rejected with 400 and nothing is stored. Test: `TestIngestRejectsBadBatch`.
4. **An empty database** (first deploy): every API endpoint and the dashboard render zeros, not errors. Test: `TestDashboardEmptyDatabase`.
5. **The Postgres restart window**: ingestion returns 503 and the reporter keeps the batch; once Postgres is back, the batch arrives once. Tests: `TestIngestUnavailable503` (Go) and `ReporterRetriesUntilAccepted` (C++).

---

### Task 1: Stat events in game-core

**Files:**
- Modify:
  - `game-core/events.h` (new kinds, `count`, `killer`)
  - `game-core/mob.h`, `game-core/mob.cpp` (`lastHitBy`)
  - `game-core/mobs.h`, `game-core/mobs.cpp` (`hit` takes the attacker; explosions and deaths emit `MobKilled`)
  - `game-core/projectiles.cpp` (arrow attacker)
  - `game-core/player.h`, `game-core/player.cpp` (`DamageSource`, `lastDamage`, `lifeStartTick`)
  - `game-core/player_actions.cpp` (`TileBroken`, attacker for melee)
  - `game-core/dropped_items.h`, `game-core/dropped_items.cpp` (pickups as events)
  - `game-core/simulation.cpp` (crafting, deaths, level reached, boss credit)
- Test: `game-core/tests/stats_test.cpp`, added to `game_core_tests` in `CMakeLists.txt`

**Interfaces (produced):**
```cpp
// events.h
struct DamageSource {
    enum class Kind { None, Player, Mob, Environment };
    Kind kind = Kind::None;
    int id = -1;  // player id (Player) or MobKind (Mob)
};
// GameEvent::Kind gains: TileBroken, ItemCollected, ItemCrafted, MobKilled, PlayerKilled, LevelReached
// GameEvent gains: int count = 0; DamageSource killer;
// BossDefeated now sets .player = the last hitter (or -1).
// Value meanings:
//   TileBroken: value = Tile
//   ItemCollected / ItemCrafted: value = ItemType, count = n
//   MobKilled: value = MobKind, count = mob level, player = killer id or -1
//   PlayerKilled: player = victim, value = seconds this life lasted, killer = source
//   LevelReached: value = World level index
bool isStatEvent(GameEvent::Kind kind);  // true for the six new kinds and BossDefeated
```

- [ ] **Step 1: Write the failing tests** in `game-core/tests/stats_test.cpp`. Fixture: same as `Multiplayer` in `multiplayer_test.cpp` (seed 99, Alice id 1 at kX,kY on grass, spawning off). Helper `statEvents()` keeps the events where `isStatEvent(kind)`. Tests:
  - `ChoppingATreeRecordsTileBrokenAndCollection`: put a Tree on the tile Alice faces, give her a Gem Axe, hit until it breaks (loop ticks with `attackPressed`, at most 200), then tick 120 times standing on the drops. Expect one `TileBroken` with value `Tile::Tree` and player 1, and `ItemCollected` events of `ItemType::Wood` for player 1 whose counts add up to at least 1.
  - `SwordKillCreditsThePlayer`: spawn a zombie of level 1 next to Alice (`surface.mobs.spawnNear`, or place one directly through the test-friendly `Mobs::add`, if it exists; otherwise use `spawnNear` with min=max=1 tile) and attack until it dies (at most 600 ticks). Expect `MobKilled` with value `MobKind::Zombie` and player 1.
  - `ArrowKillCreditsTheShooter`: give Alice a bow and arrows, put a zombie three tiles to the right and shoot until it dies. Expect `MobKilled` with player 1.
  - `LavaDeathIsEnvironmental`: set Alice's tile to Lava and tick until she dies (at most 2,000 ticks). Expect `PlayerKilled` with player 1, `killer.kind == Environment`, and value equal to (ticks since join) / 60.
  - `PvpKillCreditsTheKiller`: Bob joins next to Alice; Alice punches until Bob dies. Expect `PlayerKilled`: victim 2, killer `{Player, 1}`.
  - `CraftingRecordsTheProduct`: give Alice 20 wood and craft the workbench by hand (`PlayerCommand::craft(-1, <workbench recipe index>)`, where the index comes from `Recipe::personalRecipes()` by product). Expect `ItemCrafted` with value `ItemType::Workbench` and count 1.
  - `EnteringACaveRecordsLevelReached`: call `changeLevel` to the first cave twice (go up in between). Expect exactly one `LevelReached` with value equal to that level index.
  - `StatEventsAreDeterministic`: two simulations, the same 2,400 ticks as `Lockstep.TwoClientsGivenTheSameInputsStayIdentical`. The stat events (kind, player, value, count) are equal in order.
- [ ] **Step 2: Run them.** `cmake --build build/native --target game_core_tests` is expected to fail to compile (no `isStatEvent`, no new kinds).
- [ ] **Step 3: Implement.**
  - `events.h`: add `DamageSource`, the new kinds, `count`, `killer`, and `inline bool isStatEvent(GameEvent::Kind)`.
  - `Mob`: `int lastHitBy_ = -1; void setLastHitBy(int id)`, `int lastHitBy() const`.
  - `Mobs::hit(const Rect&, int damage, Point direction, Events&, int attacker = -1)` sets `lastHitBy` on each mob it hurts when `attacker >= 0`.
  - `player_actions.cpp:61`: pass `player_.id()`.
  - `projectiles.cpp:46`: pass `arrow.shooterId`.
  - `Mobs::explode`: damaged mobs inherit the creeper's `lastHitBy`.
  - In `Mobs::tick`'s erase pass, for dead mobs: `context.events.push({.kind = MobKilled, .value = int(kind), .count = level, .player = lastHitBy})`. For the boss, also put `.player = lastHitBy` on the `BossDefeated` event. That means `Mobs` keeps `bossKiller_`, and `Simulation` passes it on at simulation.cpp:197.
  - `Player`: `DamageSource lastDamage_`; `takeHit(..., DamageSource source = {})` sets it when the hit lands; `hurt(int damage, DamageSource source = {Environment})` sets it.
  - Callers:
    - `mob.cpp:181` passes `{Mob, int(kind())}`;
    - `mobs.cpp:164` passes `{Mob, int(MobKind::Creeper)}`, or `{Player, lastHitBy}` when the creeper was lit by a player;
    - `player_actions.cpp:65` passes `{Player, player_.id()}`;
    - `projectiles.cpp:50` passes `shooterId >= 0 ? {Player, shooterId} : {Mob, Skeleton}`;
    - `projectiles.cpp:63` passes `{Mob, AirWizard}`;
    - lava and starvation pass `{Environment}`.
  - `Player`: `int lifeStartTick_`, set by `Simulation::addPlayer` and `respawn` to `tick_`. `std::set<int> levelsReached_`, cleared on respawn.
  - `Simulation::die`: push `PlayerKilled{player, value = (tick_ - lifeStartTick)/60, killer = lastDamage}`.
  - `changeLevel`: on the first visit this life, push `LevelReached`.
  - `craft`: on success, push `ItemCrafted{player, product, count = recipe product count}`. `Recipe` exposes `productCount()`; add it if it's missing.
  - `DroppedItems::tick` gains an `Events&` parameter. When a player picks an item up, it sets the context player and pushes `ItemCollected{value = item, count = 1}`. The sound stays in `Simulation`. Repeated pickups of the same type stay separate events (count 1); the server batches them anyway.
  - `player_actions.cpp`: wherever a tile is broken or harvested (each `setTile` that replaces the target after a hit or harvest, and each `pickUp(...)`), push `TileBroken{value = previous tile}` before replacing it.
- [ ] **Step 4: Run.** `ctest --test-dir build/native` is expected to pass in full (the 58 existing tests plus the new ones).
- [ ] **Step 5: Commit.** `feat(game-core): stat events with credit to the player who did it`

### Task 2: The server reports (StatsObserver + StatsReporter)

**Files:**
- Create: `server/stats_observer.{h,cpp}`, `server/stats_reporter.{h,cpp}`, `server/stats_json.{h,cpp}`
- Modify:
  - `server/server.{h,cpp}`: own the observer and reporter when configured; feed them ticks, joins, leaves, chat and world starts
  - `server/main.cpp`: read `STATS_URL` and `STATS_TOKEN`
  - `CMakeLists.txt`: add the files to `server_lib`
- Test: `server/tests/stats_test.cpp`, added to `server_tests`

**Interfaces (produced):**
```cpp
struct StatEvent {            // what the server sends, already named
    std::string id;           // "<seed>-<tick>-<i>" or "<seed>-server-<n>"
    std::string type;         // "TileBroken", "ItemCollected", "ItemCrafted", "MobKilled", "PlayerKilled",
                              // "LevelReached", "BossDefeated", "PlayerJoined", "PlayerLeft", "WorldStarted", "ChatSent"
    std::int64_t at = 0;      // unix ms
    std::string player;       // may be empty
    std::string subject;      // item/tile/mob name, or the killer's name for PvP
    std::string killerKind;   // PlayerKilled: "player" | "mob" | "environment" | ""
    int count = 0;            // count, mob level, seconds alive or session seconds, level index, depending on type
    int icon = -1;            // ItemType index, for item icons
};
std::string toJson(int online, const std::vector<StatEvent>& events);  // {"online":N,"events":[...]}

class StatsObserver {
public:
    explicit StatsObserver(std::uint32_t seed);
    void nameJoined(int playerId, const std::string& name);  // names stay known after they leave
    std::vector<StatEvent> apply(const TickInput& tick, std::int64_t nowMs);
};

class StatsReporter {
public:
    StatsReporter(std::string url, std::string token, std::chrono::milliseconds interval = std::chrono::seconds(1),
                  std::size_t maxBacklog = 600);
    ~StatsReporter();  // stops the thread
    void add(std::vector<StatEvent> events);
    void setOnline(int online);
    std::size_t backlog() const;  // batches waiting (for tests)
};
```

- [ ] **Step 1: Write the failing tests** in `server/tests/stats_test.cpp`:
  - `JsonEscapesAndShapesBatch`: `toJson(2, {one event with a quote in subject})` gives exactly the expected string.
  - `ObserverReportsNamedActions`: an observer on seed 99. Apply a tick with Alice joining (`nameJoined(1,"Alice")`), then put a Tree in front of her through `observer.simulation()` (a test accessor) and feed attack ticks until a `TileBroken` comes out. Its `player == "Alice"`, `subject == "Tree"`, and the id starts with `"99-"`.
  - `ObserverNamesPlayersWhoLeft`: Alice joins and leaves (Leave tick). An event attributed to id 1 afterwards still has player "Alice". Test it through `observer.nameOf(1)`.
  - `ResetGivesFreshIds`: two observers with seeds 1 and 2 fed the same ticks produce disjoint id sets.
  - `ReporterSendsBatchesWithToken`: an `ix::HttpServer` on 127.0.0.1:28779 records requests. The reporter, with interval 50 ms, gets `add`ed 3 events and `setOnline(2)`. Within 2 s the server got a POST with header `Authorization: Bearer secret` and a body containing `"online":2` and the 3 ids.
  - `ReporterRetriesUntilAccepted`: the HttpServer returns 503 for the first 3 requests, then 200. Each event id arrives in exactly one accepted (200) request.
  - `ReporterDropsOldestWhenBacklogFull`: the server always returns 503; `maxBacklog` = 5; add 8 single-event batches spaced by intervals. Within 2 s, `backlog() == 5`.
  - `ServerReportsJoins`: a Server started with stats pointing at the test HttpServer. A NetworkClient joins as "Alice". Within 3 s, a `PlayerJoined` with player "Alice" and a `WorldStarted` arrive.
  - `NoStatsUrlNoReporting`: a Server without stats config. A join works, and the test HttpServer receives nothing within 1.5 s.
- [ ] **Step 2: Run them.** They're expected to fail to compile.
- [ ] **Step 3: Implement.**
  - **JSON writer:** escapes `"`, `\` and control characters.
  - **Observer:**
    - `Simulation sim_` with `startNewWorld(seed)`, `singlePlayer = false`;
    - `apply` runs `sim_.tick(tick)`, takes the events, and maps those with `isStatEvent` to `StatEvent`: names via `nameOf`, subjects via `itemName`/`tileName`/`mobName`, and for `PlayerKilled` with a Player killer, `subject = nameOf(killer.id)`;
    - ids `"<seed>-<tick.tick>-<i>"`, where `i` counts the stat events within that tick;
    - leave commands keep the names.
  - **Reporter:**
    - a thread that, every interval, moves pending events into a batch (even when empty: the heartbeat) and posts all queued batches in order;
    - stops at the first failure and keeps the rest;
    - `ix::HttpClient` (synchronous `post` with a `HttpRequestArgs` carrying the extraHeaders and a 5 s timeout);
    - 2xx → done; 4xx → log and drop; otherwise keep, with doubling backoff up to 30 s;
    - when the queue exceeds `maxBacklog`, the oldest batch is popped and the drop is logged.
  - **Server:**
    - `struct StatsConfig { std::string url, token; }`, passed to `Server(int resetAfterTicks, std::optional<StatsConfig>)`;
    - `newWorld()` creates the observer with the seed and reports `WorldStarted`;
    - `tickWorld` feeds the sent tick into `observer->apply` and reports the result;
    - `enterWorld` calls `nameJoined`, then reports `PlayerJoined`;
    - `disconnect` reports `PlayerLeft`, with `count` = session seconds (`Client` gains `joinedAt`);
    - `chat` reports `ChatSent`;
    - `setOnline(count of named clients)` every tick.
  - **main.cpp:** `getenv("STATS_URL")`, and `STATS_TOKEN`.
- [ ] **Step 4: Run.** ctest is expected to pass fully.
- [ ] **Step 5: Commit.** `feat(server): report stats from a replay of the world's ticks`

### Task 3: The Go service: ingestion and storage

**Files** (all new, under `stats/`):
- `go.mod` (module `github.com/AdnaneBJA/Minicraft/stats`)
- `cmd/stats/main.go`
- `internal/ingest/event.go`, `internal/ingest/validate.go`
- `internal/store/store.go`, `internal/store/migrations/001_init.sql`
- `internal/server/server.go` (HTTP routing; `POST /events`)
- Tests: `internal/ingest/validate_test.go`, `internal/store/store_test.go`, `internal/server/events_test.go`, `internal/testdb/testdb.go` (it starts an embedded PostgreSQL unless `TEST_DATABASE_URL` is set)

**Interfaces (produced):**
```go
// ingest
type Event struct {
    ID         string `json:"id"`
    Type       string `json:"type"`
    At         int64  `json:"at"`
    Player     string `json:"player"`
    Subject    string `json:"subject"`
    KillerKind string `json:"killerKind"`
    Count      int    `json:"count"`
    Icon       int    `json:"icon"`
}
type Batch struct { Online int `json:"online"`; Events []Event `json:"events"` }
func Validate(b Batch) error

// store
type Store struct{ /* pgxpool */ }
func Open(ctx context.Context, url string) (*Store, error)  // runs migrations
func (s *Store) Apply(ctx context.Context, events []ingest.Event) (applied int, err error)  // one tx, idempotent

// server
func New(st *store.Store, token string, web http.Handler) http.Handler  // routes /events, /stats...
```

**Schema (`001_init.sql`):** the spec's tables:
- `events (id text pk, type, at, player, subject, killer_kind, count, icon)`
- `players (name pk, first_seen, last_seen, sessions, play_seconds, kills, pvp_kills, deaths, longest_life_seconds, boss_kills, deepest_level, items_collected)`
- `item_totals (item pk, icon, collected, crafted)`
- `tile_totals (tile pk, broken)`
- `mob_totals (mob pk, killed, players_killed)`
- `kills (killer, victim, count, pk(killer,victim))`
- `counters (name pk, value)`
- `player_items (player, item, icon, collected, pk)`
- `player_mobs (player, mob, killed, pk)`
- indexes on `events(at)` and `events(type, at)`.

**Validation rules:**
- types from the closed list of 11;
- `player`: empty or 1–12 characters of `[A-Za-z0-9_-]`;
- `subject`: up to 32 printable characters;
- `count` between 0 and 1,000,000;
- `icon` between -1 and 255;
- `killerKind` in {"", "player", "mob", "environment"};
- at most 5,000 events per batch;
- `id` 1–64 printable characters.

**Tally rules** (applied only when the event row was newly inserted):
| Type | Tallies updated |
|---|---|
| PlayerJoined | `counters.players_joined` +1; the player's `sessions` +1 and `last_seen` |
| PlayerLeft | the player's `play_seconds` += count |
| WorldStarted | `counters.worlds` +1 |
| ChatSent | `counters.chat` +1 |
| ItemCollected | `item_totals.collected`, `player_items.collected`, `players.items_collected` += count |
| ItemCrafted | `item_totals.crafted` += count |
| TileBroken | `tile_totals.broken` +1 |
| MobKilled | `mob_totals.killed` +1; the player's `kills` +1 and `player_mobs.killed` +1, if a player was credited |
| PlayerKilled | the victim's `deaths` +1 and `longest_life_seconds` = max(…, count); then by killer kind: `player` → the killer's `pvp_kills` +1 and `kills(killer,victim)` +1; `mob` → `mob_totals.players_killed` +1 for the subject |
| LevelReached | the player's `deepest_level` = max over the depth ordering, where caves score 1–3 and the sky scores 4 (a count < 0 is cave depth −count; level index 0 is the sky) |
| BossDefeated | `counters.boss_defeats` +1, and the credited player's `boss_kills` +1 |

Every event with a player upserts `players(name)` and sets `last_seen`.

- [ ] **Step 1: Write the failing tests.**
  - Validation table tests: good, bad type, long name, huge count, too many events.
  - Store (against PostgreSQL):
    - `TestApplyIsIdempotent`: apply the same 3 events twice; `applied` is 3 then 0, and the tallies count once.
    - `TestTallies`: one event of each type; check each table.
    - `TestLongestLifeKeepsMax`.
  - HTTP:
    - `TestEventsNeedToken` (401);
    - `TestIngestRejectsBadBatch` (400, nothing stored);
    - `TestIngestUnavailable503` (the store is closed, so 503);
    - `TestIngestAccepts` (200, `{"applied":N}`);
    - the online value is kept: `server.Online()` returns it within 30 s, and 0 after (with an injectable clock).
- [ ] **Step 2: Run** `cd stats && go test ./...`. It's expected to fail: packages missing.
- [ ] **Step 3: Implement.**
  - `pgxpool`; migrations embedded with `embed.FS`, applied in order inside a `schema_migrations` table.
  - `Apply` uses one transaction: for each event, `INSERT ... ON CONFLICT DO NOTHING RETURNING id`, and the tallies only when a row came back.
  - `main.go` reads `DATABASE_URL`, `STATS_TOKEN`, `LISTEN` (default `:8080`) and `SPRITES_DIR` (default `/sprites`), with a graceful shutdown on SIGTERM.
- [ ] **Step 4: Run** `go vet ./... && go test ./...`. Expected to pass.
- [ ] **Step 5: Commit.** `feat(stats): Go service that ingests game events into PostgreSQL`

### Task 4: Read API and dashboard

**Files:**
- Create:
  - `stats/internal/store/queries.go` (summary, leaderboards, items, mobs, tiles, timeline, recent, player)
  - `stats/internal/api/api.go` (JSON handlers)
  - `stats/internal/web/web.go`, `stats/internal/web/templates/{layout,index,player}.html`
  - `stats/internal/web/static/{app.js,style.css}`
- Tests: `stats/internal/api/api_test.go`, `stats/internal/web/web_test.go`

**Interfaces (produced):**
```go
type Summary struct{ Online, PlayersJoined, UniquePlayers int; PlaySeconds int64; Worlds, CreaturesKilled, Deaths, BossDefeats, TreesChopped, RocksMined, ItemsCollected int }
type Leader struct{ Name string; Value int64 }
type Leaderboards struct{ Kills, LongestLife, PlayTime, Resources, PvpKills, Deepest []Leader }  // top 10 each
type ItemStat struct{ Item string; Icon, Collected, Crafted int }
type MobStat struct{ Mob string; Killed, PlayersKilled int }
type TileStat struct{ Tile string; Broken int }
type HourPoint struct{ Hour time.Time; Joins, Kills int }
type RecentEvent struct{ At time.Time; Text string }  // "Alice killed a level 3 Zombie"
type PlayerCard struct{ Name string; FirstSeen, LastSeen time.Time; Sessions, Kills, PvpKills, Deaths, BossKills, DeepestLevel int; PlaySeconds, LongestLife int64; Items []ItemStat; Mobs []MobStat }
```

**Routes:**
- `GET /stats` → the dashboard;
- `/stats/player/{name}` → the player card, or 404;
- `/stats/api/{summary,leaderboards,items,mobs,tiles,timeline,recent}` and `/stats/api/player/{name}`;
- `/stats/static/*`;
- `/stats/sprites/*` (from `SPRITES_DIR`; only `.png`, no directory listings).

- [ ] **Step 1: Write the failing tests.**
  - API with seeded events:
    - summary numbers;
    - leaderboard order and the 10-entry limit;
    - the player card;
    - 404 for an unknown player;
    - `TestDashboardEmptyDatabase`: every endpoint returns 200 with zeros or empty lists, and the HTML renders "0".
  - Web: `/stats` contains "Players joined" and the seeded name; the player page contains its totals; template output escapes `<script>` in names (validation already forbids them; this is defence in depth).
- [ ] **Step 2: Run them; expect them to fail.**
- [ ] **Step 3: Implement.**
  - **Queries:** plain SQL over the tally tables. The timeline is `date_trunc('hour', at)` over `events` of type PlayerJoined/MobKilled from the last 7 days. Recent = the last 30 events of the types MobKilled (level ≥ 2 or AirWizard), PlayerKilled, BossDefeated, LevelReached and PlayerJoined, rendered as text.
  - **Page:**
    - the dark style and layout from spec §4;
    - pixel headings from `font.png`, via a CSS `@font-face` substitute: use the "Press Start 2P" Google Font for headings, since the page may load Google Fonts;
    - icons are CSS sprites: `background: url(/stats/sprites/items.png) -<icon*8>px 0`, scaled ×3 with `image-rendering: pixelated`; mob icons use `/stats/sprites/<mob lowercase, spaces to _>.png` at 0,0, 16×16;
    - Chart.js 4.4.1 from `https://cdnjs.cloudflare.com/ajax/libs/Chart.js/4.4.1/chart.umd.min.js`;
    - `app.js` polls `/stats/api/summary`, `/leaderboards` and `/recent` every 10 s and updates the numbers in place; the charts update on the same timer.
- [ ] **Step 4: Run** `go vet ./... && go test ./...`; expect it to pass. Run the binary locally against the embedded Postgres plus a seed script, and screenshot `/stats` with Playwright.
- [ ] **Step 5: Commit.** `feat(stats): JSON API and live dashboard`

### Task 5: Deployment, CI, docs and an end-to-end check

**Files:**
- Create: `stats/Dockerfile`, `docs/adr/0003-stats-from-an-authoritative-replay.md`
- Modify: `deploy/docker-compose.yml`, `deploy/Caddyfile`, `deploy/README.md`, `.github/workflows/tests.yml`, `web/shell.html` (the "Live stats" link), `README.md`, `.dockerignore`, `web/smoke/smoke.mjs` (the optional stats check)

- [ ] **Step 1: `stats/Dockerfile`.**
  - Build stage `golang:1.25-alpine`: `CGO_ENABLED=0 go build -o /stats ./cmd/stats`, with the build context at the repo root to copy `stats/` and `assets/sprites`.
  - Runtime stage `gcr.io/distroless/static-debian12:nonroot` with `/stats` and `/sprites`.
  - `EXPOSE 8080`.
- [ ] **Step 2: Compose.**
  - `postgres:17-alpine`: env `POSTGRES_USER=stats`, `POSTGRES_PASSWORD=${POSTGRES_PASSWORD:?}`, `POSTGRES_DB=stats`; volume `pgdata:/var/lib/postgresql/data`; `mem_limit: 192m`; healthcheck `pg_isready`.
  - `stats`: `DATABASE_URL=postgres://stats:${POSTGRES_PASSWORD}@postgres:5432/stats`, `STATS_TOKEN=${STATS_TOKEN:?}`; `depends_on` postgres healthy; `mem_limit: 64m`.
  - `server`: env `STATS_URL=http://stats:8080/events`, `STATS_TOKEN`.
- [ ] **Step 3: Caddyfile.**
  ```
  {$DOMAIN} {
      handle /stats* { reverse_proxy stats:8080 }
      handle { reverse_proxy server:7777 }
  }
  ```
  The global `protocols h1` block stays. `/events` falls into the second `handle`, which goes to the game server, and that only speaks WebSocket, so the stats ingest is unreachable from outside.
- [ ] **Step 4: CI.** A `go` job in `tests.yml`: `actions/setup-go@v6` (go-version-file `stats/go.mod`), a `postgres:17` service, `TEST_DATABASE_URL`, then `go vet ./...` and `go test ./...` in `stats/`.
- [ ] **Step 5: Docs and the shell link.**
  - `web/shell.html`: a footer and overlay link "Live stats" → `https://<server domain>/stats`. The page gets the domain from a `MINICRAFT_STATS_URL` CMake variable passed through as `--shell-file` replacement text; the shell is processed by Emscripten, so it uses a `{{{ STATS_URL }}}`-like placeholder. Simplest: `web.yml` runs `sed` on `build-web/Minicraft.html` after the build, replacing `__STATS_URL__` with `https://<host of MINICRAFT_SERVER_URL>/stats`.
  - README: a dashboard section with a screenshot.
  - deploy/README: the new `.env` values (`openssl rand -hex 24`), the update command, and backups with `docker compose exec postgres pg_dump -U stats stats > backup.sql`.
  - ADR 0003.
- [ ] **Step 6: End to end, locally.** The native server with `STATS_URL` pointing at the local Go service (on embedded Postgres) and the web build on localhost. The smoke test runs two players, who chop a tree if one is near (not required). Then fetch `/stats/api/summary`: `playersJoined >= 2`, and both names appear in `/stats/api/leaderboards` play time once they leave. `smoke.mjs` gets an optional third argument, the stats base URL.
- [ ] **Step 7: Commit.** `feat(deploy): stats service and PostgreSQL in Compose; CI, docs, dashboard link`

### Task 6: Finish
- [ ] Final whole-branch review on the most capable model, then the fix pass, then a PR, then CI green.
- [ ] Tell the user the deploy steps:
  - `.env` additions;
  - `git pull && docker compose up -d --build`;
  - re-running the Web workflow for the shell link.
