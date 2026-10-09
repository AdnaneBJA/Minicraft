# Load test: 32 bots, steady mode

Environment: server on an AWS t3.micro in us-east-1 (production image); 32 bots on one home PC over a residential connection (TCP round trip 20-34 ms, median 27 ms). Target `ws://44.220.70.127:7777`, ran 3m29s from 2026-10-08 23:43 UTC.

| | p50 | p95 | p99 | max | samples |
|---|---|---|---|---|---|
| Input latency | 32 ms | 49 ms | 73 ms | 220 ms | 4970 |
| Tick jitter | 0.36 ms | 2.0 ms | 9.0 ms | 60 ms | 373258 |
| Join time | 183 ms | 229 ms | 234 ms | 234 ms | 32 |

*Input latency: a key change sent → the first tick that carries it. Tick jitter: how far the gap between ticks strays from 16.7 ms.*

| Joins | Join refusals | Connections refused | Drops | Messages in | Data in | Superseded inputs |
|---|---|---|---|---|---|---|
| 32 | 0 | 0 | 0 | 378292 (1809/s) | 106.1 MB (519.7 KB/s) | 0 |

