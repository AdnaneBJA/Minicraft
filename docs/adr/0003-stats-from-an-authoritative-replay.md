# ADR 0003: Stats from the server's replay of the world

## Context
- We want a public dashboard of what everyone does in the shared world: resources gathered, creatures killed,
  deaths, levels reached, leaderboards.
- With lockstep ([ADR 0001](0001-multiplayer-lockstep-over-enet.md)), the server runs no game. It only relays
  inputs, so it doesn't know "Alice chopped a tree": that's decided inside each client's simulation.
- Every client simulates every player. If clients reported, each action would be reported once per player online,
  and anyone could post made-up stats from the browser console.

## Decision
- **The game server replays the world.** `minicraft-server` keeps its own `Simulation` (`StatsObserver`) and
  applies every tick it sends to the players. The simulation is deterministic, so the server sees exactly what the
  players see, once, and nothing a browser sends can change it except its inputs, which every simulation checks.
- **game-core records stat events** as it runs: tile broken, item collected or crafted, mob killed (with the
  player who hit it last), player killed (with the killer: a player, a mob, or the environment, and how long that
  life lasted), level reached, boss defeated. Clients ignore them.
- **A separate Go service owns the stats.** The game server posts JSON batches once a second over HTTP with a
  bearer token (`StatsReporter`):
  - failed batches are kept and retried with backoff, up to a bounded backlog;
  - every event has a stable id (world seed + tick + position), and the service stores events with
    `ON CONFLICT DO NOTHING`, updating the running totals only for new ones, so a retried batch counts once.
- **PostgreSQL** stores every raw event (so new stats can be computed from history) plus running totals, updated in
  the same transaction, so the dashboard only reads small tables.
- **Direct HTTP rather than a message broker.** The traffic is one small POST a second, and the retry buffer plus
  idempotent ingestion give the same delivery guarantee a queue would here, without another service to run on a
  1 GB machine.

## Consequences
- **Good:**
  - Stats are authoritative and exactly-once.
  - The game itself doesn't depend on them: without `STATS_URL`, the server behaves exactly as before, and when the
    stats service is down the game keeps running and the reporter catches up later.
  - The tick stream is reused, so no new protocol is needed between the game and the server.
- **Cost:** the server simulates one world at 60 Hz, a small fraction of one core (the simulation runs about 11,000
  ticks/s).
- **Bounded loss:** during an outage longer than the backlog (600 batches, about 10 minutes), the oldest batches are
  dropped and logged.
- **Names, not accounts:** stats for one name merge across sessions and across different people who typed it.
- **Determinism matters twice now:** a stat event that depended on anything outside the simulation would make the
  server's count drift from what players saw. A test feeds two simulations the same ticks and compares their stat
  events.
