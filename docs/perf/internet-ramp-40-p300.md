# Load test: 40 bots, ramp mode

Environment: server on an AWS t3.micro in us-east-1 (production image); 32 bots on one home PC over a residential connection (TCP round trip 20-34 ms, median 27 ms). Target `ws://44.220.70.127:7777`, ran 2m0s from 2026-10-08 23:50 UTC.

| | p50 | p95 | p99 | max | samples |
|---|---|---|---|---|---|
| Input latency | 33 ms | 59 ms | 277 ms | 496 ms | 1720 |
| Tick jitter | 0.37 ms | 2.9 ms | 17 ms | 345 ms | 129305 |
| Join time | 236 ms | 562 ms | 619 ms | 619 ms | 32 |

*Input latency: a key change sent → the first tick that carries it. Tick jitter: how far the gap between ticks strays from 16.7 ms.*

| Joins | Join refusals | Connections refused | Drops | Messages in | Data in | Superseded inputs |
|---|---|---|---|---|---|---|
| 32 | 0 | 0 | 0 | 130941 (1091/s) | 37.5 MB (319.8 KB/s) | 0 |

## Ramp

| Bots | Input latency p50 | p99 | Refused | Dropped | Join refusals |
|---|---|---|---|---|---|
| 4 | 38 ms | 51 ms | 0 | 0 | 0 |
| 8 | 35 ms | 52 ms | 0 | 0 | 0 |
| 12 | 33 ms | 125 ms | 0 | 0 | 0 |
| 16 | 32 ms | 51 ms | 0 | 0 | 0 |
| 20 | 34 ms | 264 ms | 0 | 0 | 0 |
| 24 | 33 ms | 216 ms | 0 | 0 | 0 |
| 28 | 30 ms | 297 ms | 0 | 0 | 0 |
| 32 | 33 ms | 429 ms | 0 | 0 | 0 |

**Breaking point:** 32 bots (p99 input latency 429ms over 300ms).

