# Load test: 40 bots, ramp mode

Environment: AWS t3.micro (2 vCPU, 1 GB), us-east-1, Ubuntu 24.04, the production Docker image; bots on the same instance (localhost). Target `ws://localhost:7777`, ran 2m15s from 2026-10-08 23:53 UTC.

| | p50 | p95 | p99 | max | samples |
|---|---|---|---|---|---|
| Input latency | 9.5 ms | 17 ms | 22 ms | 43 ms | 2116 |
| Tick jitter | 0.21 ms | 1.6 ms | 7.0 ms | 47 ms | 158363 |
| Join time | 12 ms | 38 ms | 49 ms | 49 ms | 32 |

*Input latency: a key change sent → the first tick that carries it. Tick jitter: how far the gap between ticks strays from 16.7 ms.*

| Joins | Join refusals | Connections refused | Drops | Messages in | Data in | Superseded inputs |
|---|---|---|---|---|---|---|
| 32 | 0 | 4 | 0 | 160224 (1187/s) | 45.7 MB (346.7 KB/s) | 0 |

**Server process:** CPU avg 2.9%, max 10.0% of one core; memory avg 13.2 MB, max 23.7 MB (135 samples).

## Ramp

| Bots | Input latency p50 | p99 | Refused | Dropped | Join refusals |
|---|---|---|---|---|---|
| 4 | 10 ms | 17 ms | 0 | 0 | 0 |
| 8 | 9.4 ms | 17 ms | 0 | 0 | 0 |
| 12 | 11 ms | 32 ms | 0 | 0 | 0 |
| 16 | 9.0 ms | 17 ms | 0 | 0 | 0 |
| 20 | 8.3 ms | 18 ms | 0 | 0 | 0 |
| 24 | 8.9 ms | 18 ms | 0 | 0 | 0 |
| 28 | 11 ms | 36 ms | 0 | 0 | 0 |
| 32 | 10 ms | 19 ms | 0 | 0 | 0 |
| 36 | 9.1 ms | 18 ms | 4 | 0 | 0 |

**Breaking point:** 36 bots (4 connections refused).

