# Progress

## Done
- **2026-09-30: Bare-bones bootstrap.** A single `CMakeLists.txt` fetches SDL3 via FetchContent.
- **2026-10-01: Player + basic movement.** `client/player.{h,cpp}`: the `Player` class loads the Minicraft+
  "Paul" sprite (`assets/sprites/player.png`, loaded with SDL 3.4's built-in `SDL_LoadPNG`).
  - Movement: WASD/arrows at 60 px/s, diagonals normalised, clamped to the screen.
  - Animation: 4 facing directions with a 2-frame walk cycle (switches every 8 px), using Minicraft's mirror trick.
  - Rendering: the game draws at 240x135 and is scaled up by an integer factor (pixel-perfect).
  - Assets: CMake copies `assets/` next to the exe after each build.

## Next
- Tile map (grass/water/rock...) with tile sprites from Minicraft+, camera following the player.
- Collision with solid tiles.
