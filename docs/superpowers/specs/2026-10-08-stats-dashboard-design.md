# Live stats dashboard (Go + PostgreSQL)

## Goal
A public dashboard at `https://<domain>/stats` with live, all-time numbers from the shared world:
- **Totals:** players joined (+1 each time a name is accepted and the player enters the world), resources gathered per item, tiles broken, creatures killed per kind, deaths, boss defeats.
- **Leaderboards:** most kills, longest life, most play time, most resources, PvP kills, deepest level.
- **A per-player page.**

It's served by a **Go** service backed by **PostgreSQL**, fed by the game server.

**What was decided with the user:**
- **The game server reports.** The C++ server runs its own copy of the simulation, so stats can't be faked from a browser and nothing is counted twice.
- **Transport:** direct HTTP batches from the C++ server to the Go service.
- **Approach A:** Go + PostgreSQL + a server-rendered dashboard (html/template + Chart.js) that polls a JSON API.

**Assumptions:**
- The dashboard is public and read-only.
- All stats are all-time, across world resets.
- A player is their typed name: stats for one name merge across sessions and people. There are no accounts.

**Success criteria:**
1. Playing on the live site (chopping, mining, killing, crafting, dying) shows up on `/stats` within about 15 s.
2. Each action is counted exactly once, even when a batch is retried.
3. The game works exactly as before when the stats service is down or not configured.
4. `/events` can't be reached from the internet.
5. All tests pass in CI (C++, Go with a real PostgreSQL), and the browser smoke test checks the dashboard.

## 1. Stat events (game-core)
New `GameEvent::Kind` values, recorded during ticks like the existing events. All of them are deterministic: two simulations fed the same ticks record the same stat events.

| Kind | Recorded when | Fields used |
|---|---|---|
| `TileBroken` | a player's action breaks or harvests a tile (tree, rock, ore, hard rock, cactus, wheat, flowers, walls, doors, floors, cloud...) | `player`, `value` = Tile |
| `ItemCollected` | a dropped item enters a player's inventory | `player`, `value` = ItemType, `count` |
| `ItemCrafted` | a craft succeeds | `player`, `value` = ItemType (the product), `count` |
| `MobKilled` | a mob dies | `player` = killer id or -1, `value` = MobKind, `count` = mob level |
| `PlayerKilled` | a player dies | `player` = victim, `killer` (see below), `value` = seconds this life lasted |
| `BossDefeated` (existing) | the Air Wizard dies | gains `player` = the last hitter (-1 if none) |
| `LevelReached` | a player enters a level for the first time in the current life | `player`, `value` = level depth (1 = sky, -1..-3 = caves) |

**Who gets credited:**
- **Mobs** remember `lastHitBy` (a player id, or -1). It's set by:
  - a melee hit (the acting player);
  - a player's arrow (`Arrow` gains `shooter`);
  - a creeper explosion lit by a player (the creeper's own `lastHitBy`).
- **Players** remember their last damage source, `{kind: Player | Mob | Environment, id or MobKind}`, set by PvP melee or arrows, mob touches or arrows, explosions, lava, and starvation. `PlayerKilled` reports it.
- **Lives:** a player's life starts at join and at each respawn; `Player` stores `lifeStartTick`.
- **`GameEvent` gains `count` and `killer` fields;** `killer` is a small struct `{kind, id}`. The existing `PlayerDied` stays as it is for the client UI.

## 2. The game server reports (C++)
- **`StatsObserver`:**
  - owns a `Simulation`, started on every new world with the world's seed;
  - for every tick the server sends, applies the same `TickInput` and takes the tick's stat events;
  - turns player ids into names (from the server's client list, kept for players who already left so late events still resolve), and adds a stable `id` = `<seed>-<tick>-<index>` to each.
- **Server-level events:**
  - `PlayerJoined {player}` when a Hello is accepted;
  - `PlayerLeft {player, sessionSeconds}` on disconnect;
  - `WorldStarted {seed}` for every new world;
  - `ChatSent {player}`, counted only; the text is never sent.

  Their ids are `<seed>-server-<counter>`.
- **`StatsReporter`:**
  - a background thread that POSTs a JSON batch to `STATS_URL` once a second, with the `Authorization: Bearer <STATS_TOKEN>` header, using IXWebSocket's `HttpClient`;
  - on failure (network or 5xx) it keeps the batch and retries with backoff;
  - the backlog is capped at 600 batches, about 10 min; past that the oldest are dropped and the drop is logged;
  - a 4xx is logged and the batch dropped.
  - Configured by environment (`STATS_URL`, `STATS_TOKEN`). If `STATS_URL` is unset, there's no observer or reporter at all: the server behaves exactly as today.
- **Cost:** one simulated world at 60 Hz is small next to the measured ~11k ticks/s.

## 3. The Go service (`stats/`)
- **One binary:** `net/http` plus `pgx`, with migrations embedded and run at startup. Layout:
  - `cmd/stats`
  - `internal/ingest` (validation, applying events)
  - `internal/store` (SQL)
  - `internal/api` (JSON)
  - `internal/web` (templates and static files)
- **`POST /events`:**
  - requires the bearer token (otherwise 401);
  - validates every event: a known type, a valid name, counts within bounds, enum values in range (otherwise 400 for the batch);
  - applies the batch in **one transaction**, `INSERT ... ON CONFLICT (id) DO NOTHING` on `events`, and updates the tallies only for newly inserted rows;
  - returns 503 when the database is unavailable.
- **Schema:**
  - `events (id text pk, type text, at timestamptz, player text null, data jsonb)`
  - `players (name pk, first_seen, last_seen, sessions, play_seconds, kills, pvp_kills, deaths, longest_life_seconds, boss_kills, deepest_level, items_collected)`
  - `item_totals (item pk, collected, crafted)`
  - `tile_totals (tile pk, broken)`
  - `mob_totals (mob pk, killed, players_killed)`
  - `kills (killer, victim, count)`
  - `counters (name pk, value)` for players joined, worlds started, chat lines, boss defeats
  - `player_items (player, item, collected)` and `player_mobs (player, mob, killed)` for the player page.
- **Read API (public JSON):**
  - `GET /stats/api/summary`
  - `/leaderboards`
  - `/items`
  - `/mobs`
  - `/tiles`
  - `/timeline` (joins and kills per hour, last 7 days)
  - `/recent` (last 30 notable events)
  - `/player/{name}`
- **Names:** enum values travel as names (`"Wood"`, `"Zombie"`, `"Tree"`), from tables in `net-common/stat_names.cpp` that match the Go side's lists.
- **Players online:**
  - every batch carries the server's current player count, `{"online": 3, "events": [...]}`;
  - the reporter sends a batch every second even when it has no events, so it doubles as a heartbeat;
  - Go keeps the latest count in memory with its time, and `summary.online` is that count, or 0 when nothing has arrived for 30 s (server down).

## 4. The dashboard (`/stats`)
- **One page:** server-rendered with `html/template` (it works without JavaScript), then refreshed every 10 s by a small script polling the API. Chart.js comes from cdnjs at a pinned version.
- **Look:**
  - dark, with the game's pixel font for headings;
  - item and mob icons are PNGs cut from `assets/sprites` by a Go `go generate` step and embedded;
  - responsive down to phone width.
- **Sections:**
  1. **Header:** "Play now" link, players online.
  2. **Number cards:** players joined, unique players, total play time, worlds, creatures killed, deaths, boss defeats.
  3. **Resources:** a bar chart of collected items with icons, plus trees chopped and rocks mined.
  4. **Leaderboards:** six top-10 lists.
  5. **Creatures:** kills by kind, and the deadliest mob.
  6. **Activity:** joins and kills per hour over 7 days, and a recent-events feed.
  7. **Player page:** `/stats/player/{name}`.
- **From the game:** the shell page gets a "Live stats" link, both on the overlay and in the footer.

## 5. Deployment
- **`deploy/docker-compose.yml`:**
  - `postgres:17-alpine`: a named volume `pgdata`, `mem_limit: 192m`, restart on failure;
  - `stats`: built from `stats/Dockerfile` (Go build → distroless, non-root), `DATABASE_URL` and `STATS_TOKEN` from the environment;
  - `server` gets `STATS_URL=http://stats:8080/events` and `STATS_TOKEN`.
- **`.env`** gains `POSTGRES_PASSWORD` and `STATS_TOKEN`. The guide generates them with `openssl rand -hex 24`.
- **Caddy:** `handle /stats*` → `stats:8080`; everything else → `server:7777`. `/events` is never routed from outside.
- **CI:** a Go job in `tests.yml` (`go vet`, `go test ./...` with a `postgres:17` service).
- **Docs:** `deploy/README.md` (new env values, backups with `pg_dump`), README (dashboard section and screenshot), ADR 0003 "Stats from an authoritative replay".

## 6. Testing
- **game-core:** a test per event kind:
  - a chopped tree gives `TileBroken(Tree)`, and picking up the wood gives `ItemCollected`;
  - sword and arrow kills credit the right player;
  - a creeper explosion credits the player who lit it;
  - death by lava reports the Environment and the life length;
  - crafting gives `ItemCrafted`;
  - entering a cave gives `LevelReached`;
  - **determinism:** two simulations fed the same 2,400 ticks give identical stat-event lists.
- **Server:** with a test HTTP endpoint (IXWebSocket `HttpServer`):
  - a join plus actions produce named events;
  - a failed POST is retried and arrives once;
  - the backlog cap drops the oldest batches;
  - no `STATS_URL` means no observer and no reporting.
- **Go:**
  - validation unit tests;
  - integration tests against a real PostgreSQL (CI service; locally an embedded PostgreSQL): the same batch twice counts once, tallies, leaderboards, the player page;
  - HTTP tests: 401 without the token, 400 on a bad batch, the read API's JSON shapes, the dashboard rendering numbers.
- **Browser smoke:** with the stats service running locally, two players join and act, and `/stats` shows the players-joined count and their names.

## Out of scope
- Accounts or login; renaming or deleting players.
- Push updates (polling is enough).
- Stats from before this ships.
- Per-world history pages.
