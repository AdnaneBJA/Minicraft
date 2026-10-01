# Progress

## Phase 1: Single-player core

### Done
- **2026-09-30: Repo bootstrap.** Directory layout from CLAUDE.md §4. Top-level CMake with presets
  plus vcpkg manifest (SDL3, GoogleTest) and warnings-as-errors. `game-core` static lib (`BuildInfo`) with
  GoogleTest unit tests. The SDL3 client opens a resizable window and draws a centered square (Esc or close quits).
  ADR 0001 records the build/toolchain decision.

### Next
- Tile map + chunk structure in `game-core`, rendered by the client.
- Fixed-timestep loop and player movement/collision.
- WASM build (Emscripten + vcpkg `wasm32-emscripten`).
- Linux CI workflow (build, tests, ASan/UBSan).

### Known gaps
- No CI yet; the Linux preset is untested.
- clang-format/clang-tidy configs exist but nothing enforces them yet.
