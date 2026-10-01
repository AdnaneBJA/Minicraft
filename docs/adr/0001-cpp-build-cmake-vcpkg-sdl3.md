# ADR 0001: C++ build with CMake presets, vcpkg manifest mode, and SDL3

**Status:** Accepted, 2026-09-30

## Context
The C++ side (game-core, client, server, Python bindings) must build natively on Windows and Linux,
for WASM, and in CI with as few manual steps as possible. The client needs windowing, input,
2D rendering, and audio.

## Decision
- **CMake ≥ 3.25 with `CMakePresets.json`** as the single build entry point for CLion, the CLI, and CI.
- **vcpkg in manifest mode** (`vcpkg.json`) for third-party libraries; located through `$VCPKG_ROOT`.
- **SDL3** for client windowing, input, rendering, and audio (it replaces raylib, which earlier drafts mentioned).
- On Windows the default toolchain is MinGW-w64 GCC (bundled with CLion), using an overlay triplet
  `cmake/triplets/x64-mingw-static.cmake`. The stock triplet only finds `x86_64-w64-mingw32-gcc`, while
  CLion ships plain `gcc`/`g++`. Dependencies are linked statically, so the client is a single exe.

## Consequences
- A fresh clone needs `VCPKG_ROOT` set; the first configure builds SDL3 and GoogleTest (~2-3 min, then cached).
- Sanitizers (ASan/UBSan/TSan) are not available with MinGW and will run in Linux CI instead.
- The WASM build will use Emscripten's toolchain together with vcpkg's `wasm32-emscripten` triplet. That gets added in a later Phase 1 task.
