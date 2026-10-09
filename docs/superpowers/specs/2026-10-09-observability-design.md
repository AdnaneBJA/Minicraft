# Observability: a synthetic probe, server metrics, Prometheus and Grafana

## Goal
See, live and publicly, whether the hosted game works and how fast it is for a player, and keep the history to
look back on. A Go probe plays the real path every 30 s; the game's health and the stats service's own numbers go
to Prometheus; Grafana shows two dashboards and SLO status, readable by anyone at
`https://<DOMAIN>/grafana`. Everything runs on the existing t3.micro.

## Decisions (agreed)
- Prometheus and Grafana are **hosted on the instance** (measured: 449 MB available, 2 GB swap, 12 GB disk free).
- The probe joins as a **hidden observer**: it never appears in the world, the player list, the stats, or the
  32-player count.
- Alerts are **dashboard only**: rules evaluate in Grafana and show firing/OK; nothing is sent anywhere.
- Grafana is **public read-only** (anonymous Viewer); the admin password is in `.env`.
- The game's health reaches Prometheus **through the Go stats service**, on the batch the game server already
  sends every second. The game server serves no HTTP of its own.
- The probe connects through the **public `wss://` URL** (Caddy, TLS), not the Docker network.

## Architecture
```
                         Caddy (TLS)
  players ──wss──▶ /            ──▶ game server (C++)
  public  ──https─▶ /stats       ──▶ stats service (Go) ─▶ Postgres
  public  ──https─▶ /grafana     ──▶ Grafana (anonymous viewer)
                                       ▲
  probe (Go) ──wss via Caddy──▶ game   │ queries
     │ /metrics                         │
     └──────────▶ Prometheus ◀── /metrics of the stats service
```
New containers in `deploy/docker-compose.yml`: `probe`, `prometheus` (`mem_limit: 128m`), `grafana`
(`mem_limit: 160m`). Prometheus and the probe are not routed by Caddy. The stats service's `/metrics` is not
routed either (Caddy only routes `/stats*`, and `/metrics` is outside it).

## Protocol (net-common/protocol.h, mirrored in loadtest/protocol)
Three message types, appended after `Error` so existing values don't change:
- `Observe{token: string}` (client → server): instead of `Hello`. Makes the connection an observer if the token
  matches the server's `PROBE_TOKEN` (none set: nobody can observe); otherwise `Error "Not allowed to observe"` and
  the connection closes.
- `ProbePing{id: i32}` (client → server): only accepted from an observer.
- `ProbePong{id: i32}` (server → client): the answer.

The browser client is untouched: its `Hello` is the same bytes as before.

## Game server (C++)
- **Observers.** An observer connection gets `Joined` (seed + history, as a player would) and every `Tick`, and
  `Joined` again when a world starts or resets. If nobody plays, it gets `Joined{seed 0, no history}` and no ticks:
  it doesn't start a world. It gets the `Welcome` every socket gets on opening (sent before the server knows what
  the connection is; the probe ignores it), but no chat, and its input, commands, chat, state hashes and `Hello`
  are ignored. It isn't a player: it doesn't count towards
  `kMaxPlayers`, the online count, or the stats, and it doesn't keep an empty world alive.
- **Observer limit.** `kMaxObservers = 2`. Past it, `Observe` gets an `Error` and the connection is closed.
- **Full server.** The socket limit becomes `kMaxPlayers + kMaxObservers + 8` spare, so the 32-player limit moves into
  `Hello`: a 33rd player gets `Error "Server is full"`.
- **Ping.** A `ProbePing` is answered with `ProbePong{same id}` sent right after the next tick goes out (on the 60 Hz
  clock, world or not), so its delay is: network in, waiting for the tick, network out: the same path as a key
  press. Pings from a
  non-observer are ignored. At most one pending ping per observer (a newer one replaces it).
- **Health on the stats batch.** The batch gains a `health` object, filled by the server thread when the batch is
  cut: `at` (Unix ms when the batch was cut), `connections`, `observers`, `ticks` (ticks sent since start),
  `historyTicks`, `historyBytes`, `backlog` (batches waiting), and the process's `rssBytes` and `cpuSeconds` (from
  `/proc/self/stat`; 0 off Linux), since the C++ server has no `/metrics` of its own. `online` stays where it is.
  Health and `online` are published on every tick of the server's clock, so the online count drops to 0 when the
  last player leaves (it used to stay at the old count). JSON written in `stats_json.cpp`.

## Stats service (Go)
- **`GET /metrics`** (prometheus/client_golang), not behind the bearer token; it's only reachable inside the Docker
  network.
- From the newest batch's `health` (and `online`): gauges `minicraft_players_online`, `minicraft_connections`,
  `minicraft_observers`, `minicraft_history_ticks`, `minicraft_history_bytes`, `minicraft_reporter_backlog`, and a
  counter-like gauge `minicraft_ticks_total` (Prometheus `rate()` works on it; it restarts at 0 with the server).
  A retried old batch must not overwrite newer health: only a batch whose `health.at` is newer than the last
  applied one updates the gauges (`minicraft_players_online` included). A batch without `health` (an older server) leaves them as they are.
- `minicraft_process_resident_bytes` and `minicraft_process_cpu_seconds_total` (the game server's).
- `minicraft_last_report_timestamp_seconds`: when the last batch arrived (Grafana computes the age).
- Its own: `stats_events_ingested_total{type}`, `stats_batches_total{result="accepted|duplicate|invalid|unauthorized|error"}`,
  `stats_db_write_seconds` (histogram), plus the default Go runtime and process collectors.

## Probe (Go, `loadtest/cmd/probe`)
- Reuses `loadtest/protocol`. Every `--every` (default 30 s): connect to `--url`, send `Observe`, time until
  `Joined` is fully received (streamed, as the bots do), then send `--pings` (default 10) `ProbePing`s, one per
  tick-ish (every 100 ms), recording each pong's delay; record the gaps between ticks received meanwhile; close.
- Each run has a `--timeout` (default 10 s) deadline. A run ends in one result: `ok`, `connect_failed`,
  `refused` (Error), `join_failed`, `timeout`. It never crashes the process on a failed run.
- Serves `/metrics` on `--listen` (default `:9100`): `probe_runs_total{result}`, `probe_join_seconds`,
  `probe_latency_seconds`, `probe_tick_jitter_seconds` (histograms with buckets suited to each), and
  `probe_last_success_timestamp_seconds`.
- Built by its own Dockerfile from the `loadtest` module.
- The probe measures the server, Caddy and TLS from the same machine; it doesn't see internet distance. The
  dashboard says so.

## Prometheus
- `deploy/prometheus/prometheus.yml`: scrape `stats:8080/metrics` and `probe:9100/metrics` every 15 s, plus itself.
- `--storage.tsdb.retention.time=15d`, data in a named volume.
- The SLO ratios are recording rules (`deploy/prometheus/slo-rules.yml`: `slo:probe_success:ratio_1h`,
  `slo:probe_join_under_1s:ratio_1h`, `slo:probe_latency_under_50ms:ratio_1h`, `slo:game_report_age_seconds`,
  `slo:targets_down`), unit-tested with `promtool test rules`. Grafana's alert rules threshold them.

## Grafana
- Served under `/grafana` (`GF_SERVER_ROOT_URL`, `GF_SERVER_SERVE_FROM_SUB_PATH=true`), anonymous access with role
  Viewer, sign-up disabled, admin password from `GRAFANA_ADMIN_PASSWORD` in `.env`. Data in a named volume.
- Provisioned from files in `deploy/grafana/`: the Prometheus data source, two dashboards, and the alert rules.
- **Player experience** dashboard: probe success rate, join time p50/p99, latency p50/p99, tick rate, one SLO
  status panel per SLO, and a note that the probe runs on the server's own machine.
- **Server internals** dashboard: players, connections and observers; history ticks and bytes over time; stats
  ingest rate by type and DB write time; memory and CPU per service (process metrics); scrape health (`up`).

## SLOs (rolling 1 h, Grafana alert rules, dashboard only)
| SLO | Rule |
|---|---|
| Availability | ≥ 99% of probe runs end `ok` |
| Join | 99% of joins under 1 s |
| Latency | 99% of probe pings answered under 50 ms |
| Freshness | the game reported in the last 30 s, and every scrape target is `up` |

## Errors and limits
- Probe: deadlines per run, failures counted by result, and a probe that can't reach the game keeps serving its
  `/metrics` (the failures are the signal).
- Server: observer limit; an observer that goes silent is dropped by the same ping/timeout as a player.
- Stats service: a malformed `health` makes the batch invalid like any other malformed field (400).
- Memory caps on every new container; Prometheus retention bounds disk.

## Testing
- C++ (GoogleTest, real server on localhost): an observer gets `Joined` and ticks, isn't counted in the online
  count or the stats, doesn't keep an empty world alive; a ping gets a pong after the next tick, with the same id;
  a ping from a player is ignored; the observer limit; the batch carries `health`. Fixtures for the new messages.
- Go protocol: the new fixtures decode and re-encode; the malformed-input test's "unknown type" moves past the new
  values.
- Go probe: against `internal/fakeserver` (extended with observe and ping): an `ok` run records join, latency and
  jitter; a refusal, a stalled server and a dead address each end in their result within the deadline.
- Go stats: `/metrics` output checked with `prometheus/client_golang/prometheus/testutil`; an older retried batch
  doesn't overwrite newer health; a batch without `health` leaves the gauges.
- CI: `promtool check config` and `promtool test rules` (Docker image), and the Grafana files parse; the images job
  also builds the probe image.

## Docs
- ADR 0005: observability and the SLOs (why a hidden observer, why health goes through the stats service, why the
  thresholds).
- README: an Observability section with the Grafana link.
- `deploy/README.md`: `GRAFANA_ADMIN_PASSWORD` in `.env`, and the upgrade steps.

## Out of scope
Sending alerts anywhere, logs and traces (Loki, Tempo), probing from another region, snapshot joins.
