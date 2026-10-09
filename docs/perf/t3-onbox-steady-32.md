# Load test: 32 bots, steady mode

Environment: AWS t3.micro (2 vCPU, 1 GB), us-east-1, Ubuntu 24.04, the production Docker image; bots on the same instance (localhost). Target `ws://localhost:7777`, ran 3m29s from 2026-10-08 23:55 UTC.

| | p50 | p95 | p99 | max | samples |
|---|---|---|---|---|---|
| Input latency | 9.7 ms | 18 ms | 20 ms | 56 ms | 4970 |
| Tick jitter | 0.26 ms | 1.4 ms | 6.2 ms | 67 ms | 373511 |
| Join time | 3.1 ms | 11 ms | 12 ms | 12 ms | 32 |

*Input latency: a key change sent → the first tick that carries it. Tick jitter: how far the gap between ticks strays from 16.7 ms.*

| Joins | Join refusals | Connections refused | Drops | Messages in | Data in | Superseded inputs |
|---|---|---|---|---|---|---|
| 32 | 0 | 0 | 0 | 378513 (1810/s) | 106.2 MB (520.0 KB/s) | 0 |

**Server process:** CPU avg 4.0%, max 13.0% of one core; memory avg 15.0 MB, max 22.9 MB (209 samples).

