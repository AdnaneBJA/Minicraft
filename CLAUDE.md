## 1. What this project is

A 2D top-down survival/crafting game in the spirit of Minicraft, where many players share a persistent world. It is also a portfolio project demonstrating distributed systems, real-time netcode, and ML. Every component must have a real job, and the whole thing must be demoable by a recruiter with minimal effort.
If you want inspiration, you can find the source code of minicraft+ which is an open source project at C:\Users\Adnane Bejja\Downloads\minicraft-plus-revived-main
Do not hesitate to take assets from there to use them, this project will not be distributed, it is purely for learning purposes and resume value

**Primary goals**
1. Playable, fun-enough 2D survival game (small scope: ~5 resources, ~10 craftable items, 2-3 enemy types, day/night cycle).
2. Authoritative multiplayer server over **UDP** with a custom reliability layer.
3. Java platform services (auth, matchmaking/fleet, persistence, stats) using Kafka, Redis, Postgres.
4. Python/PyTorch data + ML layer fed by the Kafka event stream (and a Gym-style env wrapping the real C++ simulation).
5. One-command local deploy, CI/CD, observability, load-test numbers in the README.

**Non-goals**
- Photorealistic graphics, huge content, 3D, mobile builds, monetization.
- Perfect anti-cheat. Basic server-side validation only.

**Guiding rule:** the game is the vehicle, the distributed system is the point. When in doubt, cut game content, not engineering quality.

**Bare-bones rule (applies to everything below):** build only what the current task needs. No empty placeholder folders, no scaffolding "for later", no tooling, tests, docs, or abstractions before there is real code that needs them. Everything later in this file (target layout, CI, ADRs, sanitizers, and so on) is a *destination*, not a checklist to set up up front. Add each piece when the phase that needs it starts, and keep it as small as possible. If something can be one file, make it one file.

## 2. Tech stack

| Area | Choice                                                                      |
|---|-----------------------------------------------------------------------------|
| Game core / client / game server | **C++20**, CMake. Dependencies come in via CMake `FetchContent` (no package manager). |
| Rendering/input/audio (client) | **SDL3** (fetched and statically linked by CMake) |
| Server networking | **standalone ASIO** (or raw epoll + `recvmmsg`) over UDP                    |
| Wire format | **Protobuf** (or hand-rolled binary for hot-path packets)                   |
| Platform services | **Java 21**, Spring Boot 3, Gradle                                          |
| Messaging | **Kafka** (events), **Redis** (sessions, pub/sub, leaderboards, rate limits) |
| Database | **PostgreSQL**                                                              |
| Service-to-service | **gRPC** between C++ server and Java services; REST for client-facing APIs  |
| ML / analytics | **Python 3.11+**, PyTorch, pandas, FastAPI, Gymnasium                       |
| C++ <-> Python binding | **pybind11** exposing `game-core`                                           |
| Containers/deploy | Docker, Docker Compose, Kubernetes (kind locally), optional Terraform       |
| CI/CD | GitHub Actions                                                              |
| Observability | Prometheus + Grafana, structured logs                                       |
| Testing | GoogleTest (C++), JUnit 5 + Testcontainers (Java), pytest (Python)          |
| Sanitizers/fuzzing | ASan/UBSan/TSan in CI, libFuzzer on packet parsers                          |

## 3. Architecture

```
C++ client (native) ──UDP + custom reliability──► C++ game server (uses game-core)
C++ client (WASM)   ──WebSocket fallback───────►        │
                                                        │ gRPC / Kafka (librdkafka)
                                                        ▼
            Java platform: auth, fleet/matchmaking, persistence, stats
                 │               │                │
               Redis          Postgres          Kafka ──► Python (PyTorch)
                                                              │
                                     trained NPC policy ◄─────┘ (ONNX, loaded by server)
```

**Login flow:** client -> HTTPS to Java auth service -> receives JWT + short-lived signed **connect token** + game server address -> opens UDP session with the token -> server validates token (signature + expiry + nonce).

**Key decision: the simulation is written ONCE in `game-core` (C++)** and reused by the client (prediction), the server (authority), and the Python RL environment (pybind11). Never reimplement game rules in another language.
## 4. Repo layout (USE OOP FOR ALL CODE)

**Current layout** (keep it this small until a task needs more):

```
Minicraft/
├── CLAUDE.md
├── README.md
├── PROGRESS.md        # after each task, record progress here so multiple agents can sync
├── CMakeLists.txt     # single top-level CMake file; fetches SDL3 + Dear ImGui, copies assets/ next to the exe
├── assets/
│   ├── ASSETS.md      # source + license of every asset
│   └── sprites/       # player.png, zombie.png, cow.png, pig.png, sheep.png, tiles.png (atlas), hud.png, font.png, items.png, inventory_counter.png, smash.png, title.png, furniture.png
└── client/
    ├── main.cpp       # Game class: window, loop, rendering; run the `Minicraft` target in CLion
    ├── player.h/.cpp  # Player: sprite, movement, walk animation
    ├── tile_map.h/.cpp  # TileMap: seeded island generation + tile rendering
    ├── camera.h/.cpp  # Camera: follows the player, clamped to the map
    ├── debug_overlay.h/.cpp  # F3 debug mode: outlines + ImGui panel
    ├── effects.h/.cpp  # short-lived world effects (smash X, damage numbers)
    ├── texture.h/.cpp  # shared PNG -> SDL texture loader
    ├── font.h/.cpp     # Minicraft 8x8 bitmap font
    ├── items.h/.cpp    # ItemType, Inventory, item icons
    ├── bounce.h        # Minicraft toss/bounce motion (dropped items, damage numbers)
    ├── dropped_items.h/.cpp  # items on the ground: physics, pickup, despawn
    ├── hud.h/.cpp      # hearts, energy bolts, menu frame
    ├── inventory_menu.h/.cpp  # inventory screen (E)
    ├── recipe.h/.cpp   # Recipe: product + costs, crafted against an Inventory
    ├── crafting_menu.h/.cpp  # crafting screen (Z): recipe list + Have/Cost boxes
    ├── furniture.h/.cpp  # placed furniture (workbench): placement, collision boxes, drawing
    ├── world_gen.h/.cpp  # original-Minicraft-style island generation (+ beaches, few lakes)
    ├── day_night.h/.cpp  # day/night cycle and the night lighting overlay
    ├── collision.h     # tile collision shared by the player and mobs
    ├── mob.h/.cpp      # Mob base (MobAi movement/hurt/draw) + Zombie and Animal (cow/pig/sheep)
    ├── mobs.h/.cpp     # Mobs: owns, spawns, despawns, punches and draws all mobs
    ├── game_menu.h/.cpp  # title screen, new/load world screens, pause menu
    └── world_save.h/.cpp  # one binary save file per world (validated on load)
```

**Target layout** (eventual destination; create each folder only when its phase starts):

```
Minicraft/
├── CLAUDE.md                 # this file
├── README.md                 # recruiter-facing: GIF, live demo link, arch diagram, load-test results
├── PROGRESS.md               # After each task finished, report the current progress in that file so multiple agents can synchronize
├── docs/
│   ├── architecture.md
│   ├── protocol.md           # packet formats, channels, handshake
│   └── adr/                  # short Architecture Decision Records (0001-udp-custom-reliability.md, ...)
├── proto/                    # .proto files shared by C++ and Java
├── game-core/                # C++ static lib: tiles, entities, crafting, combat, RNG, tick step. NO I/O, NO rendering.
├── net-common/               # C++ lib: UDP socket wrapper, reliability layer, serialization, transport interface
├── client/                   # C++ SDL client (native + WASM targets)
├── server/                   # C++ authoritative game server
├── bindings/python/          # pybind11 module exposing game-core as `sharedworld_env`
├── platform/                 # Java (Gradle multi-module)
│   ├── auth-service/
│   ├── fleet-service/        # matchmaking + game server registry/heartbeats + shard assignment
│   ├── persistence-service/  # Kafka consumer -> Postgres world/inventory saves
│   └── stats-service/        # leaderboards (Redis ZSET), player profiles
├── ml/
│   ├── consumers/            # Kafka -> feature tables
│   ├── clustering/           # play-style clustering
│   ├── rl/                   # Gymnasium env, PPO training, ONNX export
│   └── serving/              # FastAPI model API
├── tools/
│   └── bot-loadtest/         # C++ headless bot clients for load testing
├── assets/                   # see section 8
├── deploy/
│   ├── docker-compose.yml
│   ├── k8s/
│   └── terraform/            # optional
└── .github/workflows/
```

## 5. Component requirements

### 5.1 `game-core` (C++)
- Deterministic, fixed-timestep simulation (`step(state, inputs, dt)`), seeded RNG, no global state, no I/O.
- Tile map (chunked, e.g. 32x32 tiles per chunk), procedural generation from a seed (noise-based biomes: grass, forest, sand, water, rock).
- Entities: player, mobs, dropped items, placeable objects (workbench, chest, furnace).
- Systems: movement/collision, gathering/mining, crafting recipes (data-driven from JSON), combat, hunger/health, day/night cycle, mob spawning.
- Snapshot/delta serialization for network sync.
- Must compile to native, WASM, and as a Python extension.

### 5.2 Networking (`net-common`, `server`, `client`)
- **Transport interface** with two implementations: `UdpTransport` (primary) and `WebSocketTransport` (WASM fallback). Game code depends only on the interface.
- UDP packet header: protocol id, sequence number, ack, ack bitfield (Gaffer on Games "reliable UDP" style).
- Channels: **unreliable** (inputs, snapshots; newest wins) and **reliable ordered** (join, chat, inventory/crafting results).
- Keep packets <= ~1200 bytes. No IP fragmentation.
- Handshake with connect token + challenge/response to prevent spoofed-source amplification.
- Server: authoritative tick loop (20-30 Hz to start), input buffering per client, interest management (only send nearby chunks/entities), snapshot delta compression.
- Client: input prediction + server reconciliation, entity interpolation, clock sync.
- Built-in network simulator (configurable loss/latency/jitter) for tests and demos.
- Timeouts, rate limiting, malformed-packet handling; fuzz the parser.

### 5.3 Java platform
- **auth-service:** register/login, password hashing (Argon2/bcrypt), JWT, connect-token issuance (signed, short TTL).
- **fleet-service:** game servers register + heartbeat (Redis TTL keys); assigns players to a shard; exposes server list; Kubernetes scaling hook (stretch).
- **persistence-service:** consumes Kafka events and periodic world snapshots; writes to Postgres; provides load API used by game server at startup (gRPC).
- **stats-service:** leaderboards via Redis sorted sets, player profile endpoints.
- Shared conventions: OpenAPI docs, health endpoints, Micrometer metrics, structured logging, Testcontainers integration tests.

### 5.4 Event pipeline (Kafka)
- Topics: `game.events` (player_joined, item_crafted, mob_killed, player_died, resource_gathered, position_sample), `game.snapshots`.
- Events are Protobuf, versioned, keyed by player id. Document schema evolution rules.
- C++ server publishes via librdkafka (async, batched, never blocks the tick loop; drop or buffer on backpressure).

### 5.5 Python / ML
Implement in this priority order:
1. **Play-style clustering:** features from event stream -> embeddings/KMeans -> labels (builder/fighter/explorer). Dashboard or notebook.
2. **Churn/retention prediction:** early-session events -> PyTorch classifier; report metrics honestly.
3. **RL NPC:** `bindings/python` exposes the real sim as a Gymnasium env; train PPO in PyTorch; export ONNX; game server loads it (ONNX Runtime C++) for a demo NPC.
4. (Stretch) Anomaly/cheat detection on movement/resource-gain streams.
- Include reproducible training scripts, seeds, and saved metrics. Synthetic data from bot load tests is acceptable and should be labeled as such.

## 6. Build order (vertical slices)

**Phase 1: Single-player core (2-4 wks).** `game-core` + SDL client: map, movement, collision, gather, craft, one enemy, day/night. Get a WASM build working now.
**Phase 2: Networking (3-4 wks).** UDP layer, handshake, authoritative server, 2 players visible to each other, prediction + interpolation, network simulator.
**Phase 3: Platform (3-4 wks).** Auth + connect tokens, Postgres saves, Redis sessions/leaderboard, fleet service, multiple game server instances.
**Phase 4: Event pipeline (2 wks).** Kafka publish from server, persistence + stats consumers.
**Phase 5: ML (4+ wks).** Clustering -> churn -> RL NPC.
**Phase 6: Ship it (2-3 wks).** CI/CD, k8s manifests, Grafana dashboards, bot load test (e.g., 500 bots, report p50/p99 tick time and bandwidth), docs, README with GIFs.

Each phase ends with something runnable and committed. Do not start a phase until the previous one works end to end.

## 7. Engineering conventions

- Tooling (clang-format/clang-tidy, warnings-as-errors, CI, sanitizers, ADRs) is added when it starts paying for itself, not before.
- C++: RAII, no raw owning pointers, no exceptions across hot paths. Prefer data-oriented layouts for entities.
- Java: Spotless/Checkstyle, constructor injection, records for DTOs, no business logic in controllers.
- Python: ruff + black, type hints, pinned dependencies.
- Commits: conventional commits. Small PRs, even if solo.
- Every non-trivial decision gets an ADR in `docs/adr/` (context, decision, consequences). Expected ADRs: UDP + custom reliability, C++ server with shared core, Kafka for events, WebSocket fallback for WASM, gRPC between C++ and Java.
- CI must: build all targets, run unit tests, run sanitizers on the C++ tests, build Docker images, and run a short integration test (server + 2 bots) via Compose.
- Config via environment variables; no secrets in the repo.

## 8. Assets and sounds

**Goal:** a consistent retro 2D look (16x16 or 8x8 tiles, limited palette) with simple sound effects, loaded through a swappable asset pipeline.

Rules:
- Keep all art in `assets/sprites/`, all audio in `assets/audio/`, data (recipes, tiles, mobs) in `assets/data/*.json`. Code references assets by **ID**, never by hard-coded path.
- Maintain `assets/ASSETS.md`: one row per file or pack with source URL, author, license, and any required attribution. CI should fail if an asset file has no entry.
- Sprite atlas generated by a script (`tools/pack_atlas`) into a single texture + JSON metadata; client loads via the manifest.
- Audio: short `.ogg`/`.wav` SFX (hit, pickup, craft, hurt, death, UI click) plus optional looping music. Use SDL3 audio. Mute toggle and volume setting required.
- Safe sources to start with (check each pack's license): CC0 packs from Kenney and OpenGameArt, self-made sprites (Aseprite/LibreSprite), and SFX generated with sfxr/jsfxr/ChipTone.
- The server never loads art or audio; it only uses `assets/data/*.json` through `game-core`.

## 9. How Claude should work on this repo

1. Read this file first. Ask before changing the architecture or stack.
2. Work one phase and one component at a time. State the plan briefly, then implement.
3. Keep `game-core` free of I/O, rendering, and networking dependencies.
4. Add tests once there is real logic worth testing (core rules, protocol round-trips, parsers). Don't add test frameworks for scaffolding.
5. Never block the server tick loop on I/O (Kafka, gRPC, disk). Use queues and worker threads.
6. Once `docs/protocol.md` or ADRs exist, keep them updated in the same change as the feature.
7. Prefer simple, readable solutions first. Note performance ideas as TODOs with measurements needed.
8. After each milestone, report: what works, how to run it, what is next, and known gaps.
9. After each task, commit and push the code (under my name only, do not put yourself as co-author)
9. PR descriptions list only the changes made: no "Test plan" section, no "Generated with Claude Code" footer, and no mention of Claude/AI.
9. After each task, update the LOC table in `PROGRESS.md` (`.cpp` + `.h` files).
9. Flag anything that looks like a security issue (unvalidated packet fields, token handling, SQL, deserialization).
10. Use header and cpp files (don't use hpp files I don't really like them)
11. When creating a PR don't put
    🤖 Generated with [Claude Code](https://claude.com/claude-code)

## 10. Definition of done (recruiter-ready)

- `docker compose up` starts the full backend locally; a documented command launches the native client.
- Live demo link (WASM client plus hosted backend) at the top of the README, or a clear note on the WebSocket-fallback limitations.
- README includes: architecture diagram, gameplay GIF, netcode-under-packet-loss GIF, Grafana screenshot, load-test results table, and a "Design decisions" section linking to the ADRs.
- CI badge passing; tests and sanitizers green.
- At least clustering + one other ML component trained, reproducible, and documented; RL NPC if time allows.
- Asset licenses fully documented in `assets/ASSETS.md`.

## 11. Current state

- Repo: https://github.com/AdnaneBJA/Minicraft (branch `main`).
- The game opens on a Minicraft-style title screen: Play -> Load World / New World (name + optional seed). Esc in game pauses (Return to Game / Save Game / Save and Quit). Worlds are saved to `%APPDATA%/Minicraft/Minicraft/saves/<name>.sav`.
- `client/` shows a 256x256 island generated like the original Minicraft, with a day/night cycle (zombies spawn at night and chase the player) with a camera following the player (WASD/arrows). F3 toggles debug mode (outlines + ImGui panel). Rock and trees are solid; water is swimmable (half speed, only the head shows; drains energy, then health; respawn at 0 health). Space punches, and holding it repeats (costs 1 energy each): trees (20) and rocks (50) take 1-3 damage with damage numbers, then drop wood/stone that is picked up by walking over it. E opens the inventory; Z opens crafting (Workbench = 10 wood, crafted with Space/Enter). Space/Enter on an inventory slot puts it in hand; a held workbench is carried over the head and Space places it on the tile in front (it blocks the player and mobs). E while facing a placed workbench opens its recipes (wood/rock tools, bows, arrows). Flowers grow on grass (punch to pick); cows, pigs and sheep wander and drop loot. 10 hearts + 10 energy bolts at the bottom left. Run the `Minicraft` target from CLion (default Debug profile, no extra setup).
- For now, focus only on C++ work.