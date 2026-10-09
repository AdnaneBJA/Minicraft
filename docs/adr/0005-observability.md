# ADR 0005: Watching the hosted game: a probe, metrics and SLOs

## Context
- The game runs on one small server (an AWS t3.micro). Until now the only way to know it worked was to open it,
  and the only performance numbers were one-off load tests ([ADR 0004](0004-lockstep-input-latency.md)).
- We wanted to see, all the time and publicly, whether a player can join and how fast the game answers, with the
  history to look back on, without a paid monitoring service and within the machine's 1 GB of memory.

## Decision
- **A synthetic player.** A Go probe (`loadtest/cmd/probe`, reusing the bots' protocol code) joins the live game
  every 30 s through the public `wss://` address (Caddy and TLS included), times the join and 10 round trips, and
  leaves. It fails when a player would, and measures what a player feels.
- **A hidden observer.** In lockstep everything in a tick is simulated by every client, so the probe can't send
  keys without putting a player in everyone's world. It says `Observe` instead of `Hello`: it gets the world and
  the ticks, but it isn't in the world, the player list, the stats or the 32-player limit (at most 2 observers).
  `Observe` carries a token (`PROBE_TOKEN`, shared by the server and the probe): without it anyone could take both
  observer slots, making the probe report an outage, and watch every player unseen. Without a token set on the
  server, nobody can observe.
- **A ping answered after the next tick.** `ProbePing` is answered with `ProbePong` right after the server's next
  tick goes out: network in, waiting for the tick, network out, the same path as a key press, without touching the
  world. Pongs go out on the 60 Hz clock even when nobody plays.
- **The game's health rides the stats batch.** The game server already posts a batch to the Go stats service every
  second; it now carries `health` (connections, observers, ticks sent, the world's history in ticks and bytes, the
  outgoing backlog, the process's memory and CPU). The stats service serves it, with its own numbers, at `/metrics`.
  No HTTP server in the C++ process, and the Prometheus code lives in Go. The cost: when the stats service is down
  the game's metrics stop too; the freshness SLO catches that.
- **Prometheus and Grafana on the same machine**, with memory caps (128 MB and 160 MB; measured at about 30 MB and
  100 MB). Prometheus isn't reachable from outside; Grafana is public at `/grafana`, read-only for anonymous
  viewers. Dashboards and alert rules are provisioned from files in the repository. Anonymous viewers can still make
  Grafana run any query, so Prometheus bounds each one (5 million samples, 15 s, 4 at a time): a heavy query fails
  instead of taking the machine's memory.

## SLOs
Over a rolling hour:

| SLO | Target | Why this number |
|---|---|---|
| Availability | ≥ 99% of probe runs succeed | One failed run in 100 is a blip; more is a problem a player would hit. |
| Join | 99% of joins under 1 s | Joins took 3 ms on the box and 183 ms over the internet at 32 players; 1 s leaves room for an aging world's history. |
| Latency | 99% of pings answered under 50 ms | On the box the p99 was 20 ms; 50 ms leaves room for the burstable CPU. |
| Freshness | the game reported in the last 30 s, every scrape target up | Batches come every second; 30 s means something stopped. |

- The ratios are Prometheus **recording rules** (`slo:*`), unit-tested with `promtool test rules` in CI; Grafana's
  alert rules only compare them with the targets. Prometheus 3 stores bucket bounds as floats, so the 1 s bucket is
  `le="1.0"` (checked against live data).
- A probe ping the server replaced with a newer one (it keeps one per observer) gets the newer one's answer time:
  the tick that answered it would have answered both.
- Alerts show on the dashboard only: nothing is sent anywhere.

## Consequences
- **A full server answers.** The connection limit now leaves room for the observers and 8 spare sockets, so the
  32-player limit moved into `Hello`: a 33rd player is told "Server is full" instead of being refused at the socket. The load test's
  ramp, which saw 4 refused connections at 36 bots, now sees 2 refused connections and 2 refused joins; it still
  stops there.
- **A stale count fixed.** The player count in the stats heartbeat used to be updated only while a world ran, so
  after the last player left it kept the old number. Health and the count are now published on every tick of the
  server's clock.
- **The history is visible.** The slow-join problem from ADR 0004 (late joiners download the whole tick history)
  now has a graph: the history's size in bytes, growing with the world's age.
- **Limits.** The probe sits on the server's own machine, so it doesn't see internet distance (add the player's
  round trip). A probe in another region, sending alerts somewhere, and logs or traces are left for later.
