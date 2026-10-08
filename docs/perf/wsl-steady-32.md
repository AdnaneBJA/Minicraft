# Load test: 32 bots, steady mode

Environment: WSL2 Ubuntu (Windows 11, 24-thread desktop CPU); server pinned to 2 cores (`taskset -c 0,1`), bots on the same machine (localhost). Target `ws://localhost:7777`, ran 3m29s from 2026-10-08 23:09 UTC.

| | p50 | p95 | p99 | max | samples |
|---|---|---|---|---|---|
| Input latency | 8.6 ms | 17 ms | 17 ms | 21 ms | 4970 |
| Tick jitter | 0.10 ms | 0.38 ms | 0.65 ms | 9.7 ms | 373478 |
| Join time | 4.2 ms | 15 ms | 17 ms | 17 ms | 32 |

*Input latency: a key change sent → the first tick that carries it. Tick jitter: how far the gap between ticks strays from 16.7 ms.*

| Joins | Join refusals | Connections refused | Drops | Messages in | Data in | Superseded inputs |
|---|---|---|---|---|---|---|
| 32 | 0 | 0 | 0 | 378480 (1810/s) | 106.2 MB (520.0 KB/s) | 0 |

**Server process:** CPU avg 1.9%, max 4.0% of one core; memory avg 15.1 MB, max 23.1 MB (209 samples).

