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
| 2026-10-01 | Swimming, drowning, hold-to-punch | 2014 |
| 2026-10-01 | Classic world generation, zombies, day/night | 2886 |
| 2026-10-01 | Game menu, world saves | 3779 |
| 2026-10-01 | Crafting menu, workbench recipe | 3165 |
| 2026-10-01 | Zombie drops | 3185 |
| 2026-10-01 | Tree drops: acorns, apples | 3192 |
| 2026-10-01 | Merge: game menu/saves + crafting/drops | 4078 |
| 2026-10-01 | Held items, placeable workbench | 3450 |
| 2026-10-01 | Merge: crafting (menu/saves/drops) + placeable workbench | 5513 |

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
- **2026-10-01: Swimming, drowning and hold-to-punch.**
  - Water is no longer solid. When the tile under the player's centre is water, the player swims at half speed
    (30 px/s).
  - While swimming, the sprite sinks 4 px and only its top half (the head) is drawn, over Minicraft's water ripple
    (`hud.png` cells (5,0)/(5,1), alternating every 8 ticks, right half mirrored). The slash follows the head.
  - Holding **Space** works like Minicraft: the press punches once. After the key has been held for 0.5 s it unloads
    rapid punches every 3 ticks (20/s) until energy runs out.
  - Punching resets the energy recharge count, as Minicraft's attack does. No energy comes back while punching, and
    emptying the bar starts the 40-tick exhaustion pause with the bolts flashing white/empty.
  - Punches alternate hands. Right after each punch the sprite shows the punching hand: the mirrored frame for
    up/down, the other side frame for left/right. The full slash still shows.
  - Debug panel shows whether the player is swimming.
  - **Drowning** (Minicraft rules): energy doesn't recharge in water, and once a second the player loses a bolt, or a
    heart once energy is empty (10 s of energy + 10 s of health = drowned after 20 s).
  - Being hurt: a magenta damage number pops off the player, the sprite flashes white for 10 ticks, and there's a
    30-tick hurt cooldown.
  - At 0 health the player respawns at the spawn point with full health and energy and keeps the inventory (no death
    screen or death chest yet).
- **2026-10-01: Classic world generation, zombies and day/night.**
  - **World generation** (`world_gen.h/.cpp`): based on the original Minicraft `LevelGen.createTopMap`.
    - Midpoint-displacement noise maps and `java.util.Random` (ported).
    - Rocky mountains from Minicraft's `mval` rule, sand patches, and forest clumps.
    - Re-rolls worlds that lack enough rock, sand, grass or trees, like `createAndValidateTopMap`.
    - Changes from the original:
      - The island falloff is radial, and the coast uses one smooth noise, so the ocean stays at the edges with a
        clear, ragged coastline (the original also carves long water channels through the island).
      - A 2-tile sand beach is added between the land and the ocean (water connected to the map edge).
      - Lakes come from a separate noise with a strict threshold: only a few small ones inland.
    - 256x256, ~15 ms to generate. (The earlier Minicraft+ biome port and its simplex noise were removed.)
  - **Zombies** (`zombie.h/.cpp`, port of Minicraft's Zombie/EnemyMob/MobAi, level 1 on normal difficulty):
    - 10 health; walks at 20 px/s (1 px every 3 ticks; slower than Minicraft's 30); chases the player within
      100 px, otherwise random walks.
    - Can't swim (water blocks it) and doesn't overlap other zombies.
    - Bumping into the player is its punch: 1 damage plus knockback, magenta number, white flash, 0.5 s cooldown.
    - Punches hit zombies in Minicraft's attack box (20 px reach) for 1-2 damage: red number, white flash,
      knockback. A zombie dies at 0 health (no drops yet).
    - `Zombies` keeps up to 8 alive: it spawns one every second on open ground 10-20 tiles from the player and
      removes those more than 48 tiles away.
    - Zombies are drawn behind or in front of the player depending on who is lower on screen.
  - Player: knockback when hit; `collision.h` holds the tile collision shared by the player and zombies.
  - **Day/night** (`day_night.h/.cpp`, Minicraft's Updater + LightOverlay):
    - A day is 64800 ticks (18 min): morning, day, evening, night.
    - The surface darkens through the evening to 80% at night and brightens again in the morning (the first morning
      stays bright).
    - At night the player carries a 40 px circle of light. Its edge uses Minicraft's 4x4 ordered dither, and
      everything outside it is dark.
    - Zombies only spawn at night (Minicraft skips the first night; here it doesn't). During the day, zombies out of
      view despawn.
  - Debug panel: time of day with Morning/Day/Evening/Night buttons; zombie count, an "auto spawn" toggle, "Spawn one nearby" and "Remove all";
    hitbox mode also outlines zombie hitboxes and the punch's attack box.
  - Not yet: zombie drops, the player blocking zombies from walking through them, light sources other than the player.
- **2026-10-01: Game menu and world saves** (`game_menu.h/.cpp`, `world_save.h/.cpp`, modelled on Minicraft+'s
  TitleDisplay, WorldSelectDisplay, WorldGenDisplay and PauseDisplay).
  - **Title screen:** the MINICRAFT logo (`title.png`, Minicraft+'s logo without the "+"), a random splash that
    pulses in brightness, Play / Quit, and control hints. Up/Down or W/S select, Enter accepts.
  - **Play:** Load World / New World. With no saved worlds yet, Play goes straight to New World (as in Minicraft+).
  - **New World** ("World Gen Options"): name and seed fields typed with SDL text input (blinking caret), then
    Create World. Enter creates from any row; Up/Down move between rows (W/S type letters here).
    - Names are lower case, up to 16 of `a-z 0-9 space - _`. The field turns red with a reason when the name is
      empty, invalid, or already taken.
    - Seed: empty = random, a number is used as is, and other text is hashed like Minicraft+'s `getSeed`.
    - A new world is saved right away, so it shows up under Load World.
  - **Load World** ("Select World"): saved worlds, most recently saved first, 5 at a time with scrolling. A damaged
    save shows "Could not load world" instead of loading.
  - **Pause menu** (Esc in game; Esc again or Return to Game resumes): a framed "Paused" menu over the frozen world
    with Return to Game, Save Game ("World saved!") and Save and Quit (back to the title screen).
    The game no longer quits on Esc. Closing the window quits without saving, like Minicraft.
  - **Save files:** one binary file per world, `<name>.sav`, in SDL's per-user folder
    (`%APPDATA%/Minicraft/Minicraft/saves/` on Windows). Holds the seed, every tile and its damage, player position,
    health, energy, inventory and time of day. Zombies, dropped items and effects are not saved.
    - Little-endian, with a magic number and a version. Written to a `.tmp` file first, then renamed over the old save.
    - Loading checks every field (map size, tile and item values, position inside the map, stat ranges, no trailing
      bytes) and refuses the file otherwise. World names are restricted so they can't escape the saves folder.
  - Not yet: deleting/renaming worlds from the menu, autosave, an options screen.

- **2026-10-01: Crafting (personal crafting menu).**
  - **Z** opens the crafting screen (`CraftingMenu`, Minicraft's CraftingDisplay); Z or Esc closes it. Only one
    menu is open at a time (E does nothing while crafting is open, and vice versa).
  - Recipe list in the same frame as the inventory, titled "Crafting": " Workbench" with its icon, white when the
    player can afford it and grey otherwise, with the `> <` cursor (W/S or arrows).
  - Right of the list: a **Have:** box (product icon + how many the player owns) and a **Cost:** box (icon +
    "owned/needed" per ingredient), titles at the top left like Minicraft. The Cost box sits under Have (Minicraft
    bottom-aligns it with the recipe list, which would overlap Have while the list has one entry).
  - Space or Enter crafts the selected recipe if affordable: takes the costs and adds the product; products that
    don't fit are dropped at the player's feet.
  - `Recipe` (`recipe.h/.cpp`): product, amount and costs; one recipe so far, **Workbench = 10 wood**.
  - Inventory: `remove()`, and non-stackable items (the workbench, like Minicraft's furniture) take one slot each
    and show as " Workbench" without a count. `items.png` gains the workbench icon.
  - `Hud::drawTitle` now draws menu titles for both menus.
  - Not yet: placing the workbench, workbench recipes (tools), crafting sound.
- **2026-10-01: Zombie drops.**
  - A zombie that dies drops Minicraft's normal-difficulty loot: 1-3 **cloth**, a 1 in 60 chance of **iron** and a
    4% chance of a **potato**. Loot is tossed out like other drops and picked up by walking over it.
  - New stackable items Cloth, Iron and Potato (`items.png` gains Minicraft+'s `cloth`, `iron_ingot` and `potato`
    icons).
  - Not yet: Minicraft's 1 in 40 coloured clothes (armour isn't in the game yet).
- **2026-10-01: Tree drops: acorns and apples** (Minicraft's `TreeTile.hurt`).
  - Every punch on a tree has a 1 in 100 chance to drop an **apple**.
  - A broken tree drops 0-2 **acorns** along with its 1-3 wood.
  - New stackable items Acorn and Apple with Minicraft+'s icons. They can't be eaten or planted yet.

- **2026-10-01: Held items and a placeable workbench.**
  - **Holding items** (Minicraft's activeItem): Space or Enter on an inventory slot takes that whole stack out of
    the inventory, puts it in the player's hand and closes the inventory. The held item shows on the energy row right
    of the bolts (icon + name on black, Minicraft's `renderHUD`). Opening the inventory or crafting puts it back
    (dropped at the feet if there's no room).
  - **Carrying furniture:** while holding the workbench the player uses Minicraft's carry frames (second row of
    `player.png`, arms raised) and the workbench sprite is drawn 12 px above the sprite, over the head (sinks with
    the player in water).
  - **Placing:** Space with furniture in hand places it centred on the tile in front (`interactionTile`), like
    `FurnitureItem.interactOn`: only on grass, sand or dirt, not on a tile that already has furniture or a zombie.
    The hand is then empty. Holding any other item, Space does nothing (Minicraft items that can't attack don't
    punch); with an empty hand it punches as before.
  - **`Furniture`** (`furniture.h/.cpp`): placed pieces with Minicraft's workbench box (6x4 at the tile centre).
    They block the player (`collision::clampMoveX/Y`, which never traps a box already overlapping) and zombies, and
    zombies don't spawn on them. Drawn behind or in front of the player by y.
  - Assets: `player.png` now has the carry row; new `furniture.png` (workbench).
  - Not yet: picking furniture back up (power glove), using the workbench (its recipe list), crafting sound.
- **Known gap after merging the game menu/saves with the placeable workbench:** placed furniture isn't saved yet,
  so a workbench placed in the world is gone after reloading (the held item is saved as part of the inventory).

## Next
- Use a placed workbench (facing it + Space with an empty hand) to open its recipe list (wooden tools).
- Pick furniture back up (Minicraft's power glove).
