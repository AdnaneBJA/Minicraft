# Load test: 40 bots, ramp mode

Environment: WSL2 Ubuntu (Windows 11, 24-thread desktop CPU); server pinned to 2 cores (`taskset -c 0,1`), bots on the same machine (localhost). Target `ws://localhost:7777`, ran 2m15s from 2026-10-08 23:13 UTC.

| | p50 | p95 | p99 | max | samples |
|---|---|---|---|---|---|
| Input latency | 9.1 ms | 17 ms | 17 ms | 17 ms | 2117 |
| Tick jitter | 0.08 ms | 0.34 ms | 0.59 ms | 6.5 ms | 158376 |
| Join time | 5.4 ms | 16 ms | 19 ms | 19 ms | 32 |

*Input latency: a key change sent → the first tick that carries it. Tick jitter: how far the gap between ticks strays from 16.7 ms.*

| Joins | Join refusals | Connections refused | Drops | Messages in | Data in | Superseded inputs |
|---|---|---|---|---|---|---|
| 32 | 0 | 4 | 0 | 160238 (1187/s) | 45.7 MB (346.7 KB/s) | 0 |

**Server process:** CPU avg 1.3%, max 4.0% of one core; memory avg 12.9 MB, max 22.8 MB (135 samples).

## Ramp

| Bots | Input latency p50 | p99 | Refused | Dropped | Join refusals |
|---|---|---|---|---|---|
| 4 | 8.3 ms | 17 ms | 0 | 0 | 0 |
| 8 | 8.5 ms | 17 ms | 0 | 0 | 0 |
| 12 | 10 ms | 17 ms | 0 | 0 | 0 |
| 16 | 9.2 ms | 17 ms | 0 | 0 | 0 |
| 20 | 9.3 ms | 17 ms | 0 | 0 | 0 |
| 24 | 8.5 ms | 17 ms | 0 | 0 | 0 |
| 28 | 8.5 ms | 17 ms | 0 | 0 | 0 |
| 32 | 8.6 ms | 17 ms | 0 | 0 | 0 |
| 36 | 8.7 ms | 17 ms | 4 | 0 | 0 |

**Breaking point:** 36 bots (4 connections refused).

