# Architecture

See `CLAUDE.md` section 3 for the target architecture. This file tracks what actually exists.

## Current state (Phase 1 bootstrap)

```
client/ (SDL3 window) ──links──► game-core/ (static lib, no I/O)
```

- `game-core` — simulation library. Currently only `mc::core::BuildInfo` (version + compatibility
  check used later in the handshake). Must stay free of I/O, rendering and networking.
- `client` — SDL3 application. RAII wrappers (`SdlContext`, `Window`, `Renderer`) in
  `client/src/sdl_platform.*`; main loop in `Application`.
- Every other top-level directory is an empty placeholder for a later phase.
