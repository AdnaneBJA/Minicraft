# ADR 0001: Multiplayer as lockstep through a relay server, over ENet

> **Transport superseded by [ADR 0002](0002-websocket-transport.md):** the game now talks WebSockets so it can run in a browser. Everything else here still holds.

## Context
- `game-core` is deterministic: the same seed and the same inputs give the same world, tick for tick (tested by `stateHash()`).
- The world is large: 5 levels of 256x256 tiles, with mobs, drops and chests. Sending its state to every client would need a serializer for every object and constant snapshot traffic.
- We want working multiplayer with lobbies, chat and PvP, in code that is easy to read, before investing in a custom UDP stack.

## Decision
- **Lockstep:** every client runs the full `Simulation`. The server (`minicraft-server`) runs no game: per lobby, at 60 Hz, it collects what each player did (held keys plus one-off commands) into a `TickInput` and sends the same one to every member, in player-id order.
- **Changes only as inputs:** joining, leaving, crafting, chest transfers, holding items and respawning are all `PlayerCommand`s inside ticks, so every client applies them on the same tick. Single-player builds its ticks locally and uses the same path.
- **Late join:** the server keeps each lobby's tick history. A joiner receives it with the seed and replays it, fast-forwarding at about 11k ticks/s in a Debug build.
- **Desync detection:** clients report `stateHash()` every 60 ticks, and the server announces the first mismatch in chat.
- **Transport:** ENet, with one reliable, ordered channel for everything. Ticks must never be lost or reordered in lockstep, and ENet already does that.
- **Out of the simulation:** chat goes through the server only.

## Consequences
- **Good:**
  - Tiny bandwidth: about 10 bytes per player per tick.
  - No state serialization, and one code path for single-player and multiplayer.
  - Late join and desync detection come almost for free.
  - A client can only send inputs, which every simulation validates.
- **Input latency:** a key press acts after a round trip to the server. There is no client-side prediction yet, which is fine on a LAN but noticeable over the internet.
- **Same build everywhere:** every client must run the same build. Floating point must match bit for bit; a different compiler or flags could desync, and the hash check would report it.
- **Shared slowdowns:** if one client stutters, it only falls behind; the server never waits. But every client simulates every occupied level, so CPU cost grows with the number of levels in use.
- **No secrets:** every client knows the whole world, including other players' inventories. Fine for this game, wrong for competitive games.
- **History grows:** a lobby's history grows for as long as it lives (about 600 KB per hour per player). Snapshots on join would bound it.
- **Not yet:** saving multiplayer worlds, reconnecting into your old character, and client prediction.
