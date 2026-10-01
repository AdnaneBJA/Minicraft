# Progress

## Lines of code (`.cpp` + `.h`, excluding fetched dependencies)
Update after every task: `git ls-files -co --exclude-standard -- '*.cpp' '*.h' | xargs wc -l`

| Date | Milestone | LOC |
|---|---|---|
| 2026-10-01 | Tile map, camera, debug mode, movement fixes | 722 |
| 2026-10-01 | Collision | 829 |

## Done
- **2026-09-30: Bare-bones bootstrap.** A single `CMakeLists.txt` fetches SDL3 via FetchContent.
- **2026-10-01: Player + basic movement.** `client/player.{h,cpp}`: the `Player` class loads the Minicraft+
  "Paul" sprite (`assets/sprites/player.png`, loaded with SDL 3.4's built-in `SDL_LoadPNG`).
  - Movement: WASD/arrows at 60 px/s, diagonals normalised, clamped to the screen.
  - Animation: 4 facing directions with a 2-frame walk cycle (switches every 8 px), using Minicraft's mirror trick.
  - Rendering: the game draws at 240x135 and is scaled up by an integer factor (pixel-perfect).
  - Assets: CMake copies `assets/` next to the exe after each build.

- **2026-10-01: Tile map, camera, debug mode.**
  - `TileMap` (`client/tile_map.{h,cpp}`) generates a 128x128 island from a seed using value noise, with
    water, sand, grass, oak trees and rock. Tiles come from Minicraft+ (`assets/sprites/tiles.png`) and
    water is animated. Only visible tiles are drawn.
  - `Camera` (`client/camera.{h,cpp}`) centres on the player, is clamped to the map edges, and snaps to whole pixels.
  - Scaling: the world is drawn at the largest whole-number scale where at least 240x135 world pixels fit;
    a bigger window shows more of the world.
  - Debug mode, **F3** (`client/debug_overlay.{h,cpp}`): a faint tile grid with adjustable opacity, the
    player outline, and the tile under the player. A Dear ImGui panel toggles each overlay and shows
    FPS, position, tile and camera info, and has seed input + Regenerate / Random seed buttons.
  - Dear ImGui v1.92.9b is fetched by CMake (SDL3 + SDLRenderer3 backends).

- **2026-10-01: Smooth camera.** The camera and player now snap to screen pixels (1/scale of a world pixel)
  instead of whole world pixels. Before, the view moved in 4-screen-pixel jumps at 4x zoom and stood still on
  ~70% of frames, which made the screen shake when moving diagonally. It also fixes vertical jitter caused
  by the half-pixel view height (135 / 2 = 67.5).

- **2026-10-01: Collision.** Water, rock and trees are solid (`isSolid(Tile)`), and so is the area outside the map.
  - The player collides with an 8x6 hitbox at their feet (Minicraft-style), so their head can overlap a tree above.
  - X and Y are resolved separately: the player stops flush against a wall and slides along it when moving diagonally.
  - Debug panel: new "Player hitbox" and "Solid tiles" (faint red tint) toggles.

## Next
- Tile borders/transitions (Minicraft+ has `*_border.png` connected textures).
