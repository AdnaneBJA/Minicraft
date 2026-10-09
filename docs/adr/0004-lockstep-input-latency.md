# ADR 0004: What lockstep input latency is made of, measured

## Context
- With lockstep ([ADR 0001](0001-multiplayer-lockstep-over-enet.md)) there is no client-side prediction: a key press
  changes the world only when the server puts it in a tick and the tick comes back. We wanted to know how long that
  really takes, and how much load the server takes before it degrades.
- The Go load-testing bots (`loadtest/`) measure **input latency** directly: the time from a bot sending a key change
  to the first tick carrying it.

## The model
A key press goes through three stages:
1. **The network** to the server: half a round trip.
2. **Waiting for the next tick.** The server sends a tick every 16.7 ms and puts in it the keys it holds at that
   moment. A press arrives at a random point between two ticks, so it waits 0 to 16.7 ms: 8.3 ms on average and
   16.7 ms at most.
3. **The network** back: the other half of the round trip.

Input latency ≈ one round trip + uniform(0, 16.7 ms). On localhost the round trip is negligible, so the prediction
is p50 ≈ 8.3 ms and p99 ≈ 16.5 ms.

## Measurements
32 bots for 3.5 minutes each; full reports in `docs/perf/`.

| | Input latency p50 | p99 | max | Tick jitter p99 |
|---|---|---|---|---|
| AWS t3.micro, bots on the same box | 9.7 ms | 20 ms | 56 ms | 6.2 ms |
| AWS t3.micro (us-east-1), bots on a home PC, round trip ~27 ms | 32 ms | 73 ms | 220 ms | 9.0 ms |
| WSL2 Linux, server pinned to 2 cores, bots on the same box | 8.6 ms | 17 ms | 21 ms | 0.65 ms |

The measurements match the model. On localhost the latency is all tick waiting (8.3 ms predicted, 8.6–9.7 ms
measured; the t3.micro's burstable vCPUs add a little scheduling noise). Over the internet, the median round trip
of 27 ms plus 8.3 ms of waiting predicts 35 ms; 32 ms was measured. On the t3.micro the server used 4.0% of one
core on average (13% max) and 23 MB.

Ramping 4 bots every 15 s on the t3.micro, p99 stayed between 17 and 36 ms at every step up to 32. At 36 bots the
server refused connections: `kMaxPlayers = 32` is reached long before any performance limit.

**A join-burst finding.** The same ramp run from the home PC showed a flat median (30–38 ms) but a p99 that climbed
to 430 ms at 32 bots. A joining player downloads the world's whole tick history, which grows with time and players
(1–2 MB after a few minutes with 20+ players). With every bot behind one residential connection, each wave of 4
joins saturated that connection's downlink and delayed the ticks of the bots already playing. The on-box ramp, which
has no such link, stayed flat, so the server isn't the cause. Real players each have their own connection, but a
large history still makes joining slower (183 ms median join over the internet at 32 players vs 3 ms on the box).
Also, a ramp step only has a few dozen latency samples, so over a noisy link its p99 is close to its maximum: the
steady run is the better measure of what a player sees.

**A Windows finding.** Run on Windows, the same server ticked in bursts (jitter p95 15 ms) and latency read about
0 ms. The tick loop's `wait_until` wakes on Windows' coarse timer (about 15.6 ms), oversleeps, then ticks at once
when an input arrives. Production runs on Linux, which has precise timers, so this only affects the Windows dev
build, and the numbers are measured on Linux.

## Consequences
- **Players feel** a tick of waiting (8 ms on average) plus their round trip to the server: 32 ms at the median
  from a home connection in the same part of the continent, 100 ms or more across an ocean. The server is not the
  bottleneck: the distance is.
- **Snapshots would shorten joins.** Sending a joining player a snapshot of the world plus the recent ticks, instead
  of the whole history, would bound the join download however long the world has been running.
- **Client-side prediction** (showing your own movement at once, then reconciling) is the way to hide the round
  trip, if it ever matters. Ticking faster would only shave the 8 ms average.
- **The 32-player limit can rise** if wanted: the server's CPU cost is tiny. Each player's own bandwidth grows with
  the number of players, since every tick lists every turn: about 16 KB/s per player at 32.
- **CI** runs an 8-bot load test against the real server on every push, and fails if p99 reaches 250 ms or any bot
  fails, so a performance regression shows up in review.
