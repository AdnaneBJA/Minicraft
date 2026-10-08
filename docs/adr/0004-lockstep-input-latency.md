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
32 bots for 3.5 minutes (WSL2 Linux, server pinned to 2 cores; full reports in `docs/perf/`):

| | p50 | p99 | max |
|---|---|---|---|
| Input latency | 8.6 ms | 17 ms | 21 ms |
| Tick jitter | 0.10 ms | 0.65 ms | 9.7 ms |

The measurement matches the model within a fraction of a millisecond: on localhost the latency is all tick waiting.
The server used 1.9% of one core on average (4.0% max) and 23 MB.

Ramping 4 bots every 15 s, p99 stayed at 17 ms at every step up to 32. At 36 bots the server refused connections:
`kMaxPlayers = 32` is reached long before any performance limit.

**A Windows finding.** Run on Windows, the same server ticked in bursts (jitter p95 15 ms) and latency read about
0 ms. The tick loop's `wait_until` wakes on Windows' coarse timer (about 15.6 ms), oversleeps, then ticks at once
when an input arrives. Production runs on Linux, which has precise timers, so this only affects the Windows dev
build, and the numbers are measured on Linux.

## Consequences
- **Players feel** a tick of waiting (8 ms on average) plus their round trip to the server. From the same region
  that's about 20–40 ms; across an ocean, 100 ms or more. The server is not the bottleneck: the distance is.
- **Client-side prediction** (showing your own movement at once, then reconciling) is the way to hide the round
  trip, if it ever matters. Ticking faster would only shave the 8 ms average.
- **The 32-player limit can rise** if wanted: the server's CPU cost is tiny. Each player's own bandwidth grows with
  the number of players, since every tick lists every turn: about 16 KB/s per player at 32.
- **CI** runs an 8-bot load test against the real server on every push, and fails if p99 reaches 250 ms or any bot
  fails, so a performance regression shows up in review.
