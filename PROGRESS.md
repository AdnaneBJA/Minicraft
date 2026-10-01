# Progress

## Lines of code (`.cpp` + `.h`, excluding fetched dependencies)
Update after every task: `git ls-files -co --exclude-standard -- '*.cpp' '*.h' | xargs wc -l`

| Date | Milestone | LOC |
|---|---|---|
| 2026-10-01 | Tile map, camera, debug mode, movement fixes | 722 |
| 2026-10-01 | Collision | 829 |
| 2026-10-01 | Tile borders/transitions | 902 |
| 2026-10-01 | Diagonal movement tweaks | 902 |
| 2026-10-01 | Punch animation | 965 |
| 2026-10-01 | Punching breaks trees | 1127 |
| 2026-10-01 | Resource gathering, inventory, health/energy HUD | 1892 |
| 2026-10-01 | Swimming, hold-to-punch | 1936 |

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

- **2026-10-01: Tile borders/transitions.** Port of Minicraft+'s connected textures.
  - Each tile is drawn as four 8x8 quadrants on a dirt base. Each quadrant checks its two orthogonal neighbours and
    the diagonal, then picks an edge piece, the centre, or an inner corner from a 3x3 border sheet.
  - The transparent rims show dirt between different tile types.
  - Grass connects to trees; rock has dedicated inner-corner art.
  - Exactly one tree sprite per tree tile. Minicraft+'s forest-canopy merging (`oak_full`) was dropped because it
    drew extra crowns on the corners between trees, so tiles looked like they held several trees.
  - Tile interiors use the full speckled texture (and animated water) instead of the plain border centre, so large areas don't look flat.
- **2026-10-01: Diagonal movement tweaks.**
  - When moving diagonally the player faces up or down (vertical wins), not left or right.
  - Diagonal movement is no longer normalised: each axis moves at full speed, so diagonals are sqrt(2)x faster, like Minicraft.
  - The walk cycle advances by the larger axis only, so diagonals animate at the same rate as straight walking
    (7 sprite changes/s instead of 14).
- **2026-10-01: Punch animation (player interaction, step 1).**
  - **Space** punches in the facing direction and shows Minicraft's slash for 5 ticks (~83 ms).
  - The slash is two 8x8 halves from Minicraft+'s `hud.png`, placed and mirrored as in `Player.render`.
- **2026-10-01: Punching breaks trees (player interaction, step 2).**
  - A punch hits the tile 12 px in front of the player's centre (Minicraft's `INTERACT_DIST`) for 1-3 damage.
  - Trees break at 20 damage (about 10 punches) and become grass, which can then be walked on.
  - A hit shows Minicraft's X-shaped smash effect (`smash.png`, 10 ticks) instead of the slash; a miss still shows the slash.
  - New `Effects` class for short-lived world effects.
  - F3 debug: a "Punch target" outline, plus the target tile and tree damage in the Info section.
  - Not yet: wood drops, tree damage regenerating over time, damage numbers, sound.

- **2026-10-01: Resource gathering, inventory, health/energy HUD.**
  - Trees (20 health) and rocks (50 health) take 1-3 damage per punch. Each hit shows the smash X and a red damage
    number that pops out and bounces (Minicraft's TextParticle).
  - A broken tree drops 1-3 wood and becomes grass; a broken rock drops 1 stone and becomes the new **Dirt** tile.
  - Dropped items (`DroppedItems`, Minicraft's ItemEntity): tossed with a bounce and drawn with a shadow, picked up
    by touching the player's hitbox after 30 ticks, and they blink then despawn after ~10 s.
  - **Inventory** (`Inventory`, 27 slots; a stack uses one slot, and when it's full new item types stay on the ground)
    and inventory screen (**E**, `InventoryMenu`), drawn like Minicraft's:
    - Frame, with the title set into the top edge.
    - " count Name" entries with a `> <` cursor on the selected one.
    - Slots-used counter (green -> yellow -> red as it fills) with the capacity in grey, at the top right.
    - W/S or the arrows select; E or Esc closes. The player can't move or punch while it's open.
  - **HUD** (`Hud`): 10 hearts and 10 energy bolts at the bottom left from Minicraft's `hud.png`.
  - **Energy:** a punch needs and spends 1 bolt. It recharges at ~2 bolts/s (slower than Minicraft's ~5.5), with a
    40-tick blinking pause after running out; a full refill from empty takes ~5.7 s.
  - Assets: `hud.png` (also the slash; `slash.png` removed), `font.png`, `items.png`, `inventory_counter.png`. `texture.h/.cpp` is a shared
    PNG-to-texture loader.
  - Debug panel: health, energy, inventory counts, items on the ground, damage/max for any punchable target, and a
    "Refill health/energy" button.
  - Not yet: nothing damages the player (health is display-only), tile damage doesn't regenerate, no sound.
- **2026-10-01: Swimming and hold-to-punch.**
  - Water is no longer solid. When the tile under the player's centre is water, the player swims at half speed
    (30 px/s).
  - While swimming, the sprite sinks 4 px and only its top half (the head) is drawn, over Minicraft's water ripple
    (`hud.png` cells (5,0)/(5,1), alternating every 8 ticks, right half mirrored). The slash follows the head.
  - Holding **Space** keeps punching every 10 ticks (~6/s) until energy runs out; a fresh press always punches
    immediately.
  - Debug panel shows whether the player is swimming.
  - Not done (Minicraft has it): swimming draining energy / drowning, and no energy recharge while swimming.

## Next
- Crafting (workbench, wooden tools) using the gathered wood/stone.
