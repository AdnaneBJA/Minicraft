# loadtest

Bot players for load-testing a `minicraft-server`. Each bot speaks the game's real binary protocol over WebSockets:
- it joins with a name;
- it walks, attacks and chats like a person, sending only key changes, as the game does;
- it measures what a player would feel.

| Metric | What it is |
|---|---|
| **Input latency** | A key change sent → the first tick that carries it: how long a key press takes to show up in the world. |
| **Tick jitter** | How far the gap between two ticks strays from 16.7 ms (60 Hz). |
| **Join time** | Connecting → having the world (its seed and the whole tick history). |
| **Throughput** | Messages and bytes received. |
| **Failures** | Connections refused (a full server), joins refused (a name in use), connections dropped. |
| **Server CPU and memory** | With `--server-pid`, when the server runs on the same machine. |

**Only point it at servers you own.** It refuses any address that isn't on this machine or a private network, unless
you pass `--i-know-this-is-not-local` (a throwaway cloud instance, say). Never point it at the live game: the bots
would fill the world and the public stats.

## Running it

```sh
cd loadtest
go run ./cmd/loadtest --url ws://localhost:7777 --bots 32 --ramp 30s --duration 3m --server-pid <pid>
```

It prints a line a second (`t=00:42 bots=32/32 lat p50=8ms p99=17ms jitter p99=0.2ms rx=150.3 KB/s drops=0`), then
the report, and writes `report.md` and `report.json` (`--out` changes the name). Ctrl+C stops early and still writes
the report.

### Modes

| Mode | What it does | Example |
|---|---|---|
| `steady` (default) | Adds `--bots` over `--ramp`, holds for `--duration`. | `--bots 32 --ramp 30s --duration 3m` |
| `ramp` | Adds `--step` bots every `--every` until something gives: p99 input latency over `--max-p99`, or connections refused, dropped or turned away. The report names the breaking point. | `--mode ramp --bots 48 --step 4 --every 15s` |
| `soak` | Steady, and every `--join-probe-every` a fresh player joins and times it, to show joining slow down as the world's history grows. | `--mode soak --bots 16 --duration 30m --join-probe-every 1m` |

For CI, `--fail-on-p99 250ms --fail-on-errors` exits with 1 when the run isn't healthy.

### A realistic environment

Measure on Linux, where production runs. On Windows the server's tick timer is coarse (about 15 ms), so its ticks
come in bursts and the numbers aren't representative.

**On a throwaway EC2 instance, like production:**
1. Launch a `t3.micro` (Ubuntu 24.04) and allow TCP 7777 from your IP only.
2. On it: install Docker, clone the repo, then
   `docker build -f server/Dockerfile -t minicraft-server . && docker run -d -p 7777:7777 minicraft-server`.
3. From your machine:
   `go run ./cmd/loadtest --url ws://<public-ip>:7777 --bots 32 --duration 3m --i-know-this-is-not-local`.
   That measures the server and your connection to it.
4. **Terminate the instance** when you're done.

**In WSL or on a Linux box,** next to the server, to measure the server alone:
`GOOS=linux go build -o loadtest ./cmd/loadtest`, start `minicraft-server`, then
`./loadtest --server-pid $(pgrep minicraft-server) ...`.

## How it's built

| Package | Does |
|---|---|
| `protocol` | The wire format, mirroring `net-common/protocol.*`. It's checked against byte fixtures the C++ side writes (`server/tests/protocol_fixtures.cpp`), so a change on either side fails a test. It's fuzzed, and it never keeps a long history in memory. |
| `bot` | One bot: a reader goroutine (ticks, latency, jitter) and an actor goroutine (walking, attacking, chatting), with write timeouts, so a slow server can't stall the reading. |
| `metrics` | Log-bucket histograms (constant memory, percentiles within about 1%) and atomic counters. |
| `procstat` | Samples the server process's CPU and memory (gopsutil). |
| `run` | The modes, the live line and the safety check. |
| `report` | JSON and Markdown reports, and the CI thresholds. |
| `internal/fakeserver` | A stand-in server for the tests: 60 Hz ticks, an adjustable echo delay, refusals and drops. |

Tests: `go test -race ./...`.
