# Minicraft

A 2D top-down survival/crafting game with a shared persistent world, built as a distributed-systems
portfolio project: C++20 game core + authoritative UDP server, Java platform services
(Kafka, Redis, Postgres), and a Python/PyTorch ML layer.

> Status: **Phase 1, bootstrap.** An SDL3 window that renders a square. See [PROGRESS.md](PROGRESS.md).

## Building (C++)

Prerequisites: CMake ≥ 3.25, Ninja, a C++20 compiler (MinGW-w64 GCC 13 on Windows, GCC/Clang on Linux),
and [vcpkg](https://github.com/microsoft/vcpkg) with `VCPKG_ROOT` pointing at it.

```sh
cmake --preset mingw-debug          # or linux-debug; first run builds SDL3 + GoogleTest via vcpkg
cmake --build --preset mingw-debug
ctest --preset mingw-debug
./build/mingw-debug/client/minicraft-client
```

In CLion, enable the **MinGW Debug** CMake preset (Settings → Build, Execution, Deployment → CMake).

## Repository layout

| Path | Purpose |
|---|---|
| `game-core/` | Deterministic simulation library (no I/O) |
| `client/` | SDL3 game client |
| `net-common/`, `server/` | UDP transport + authoritative server (Phase 2) |
| `platform/` | Java services (Phase 3) |
| `ml/` | Python ML (Phase 5) |
| `docs/` | Architecture, protocol, [ADRs](docs/adr/) |

## Design decisions

- [ADR 0001: CMake presets, vcpkg, SDL3](docs/adr/0001-cpp-build-cmake-vcpkg-sdl3.md)
