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
| 2026-10-01 | Merge: crafting (menu/saves/drops) + placeable workbench | 4349 |
| 2026-10-01 | Workbench recipes, flowers, animals | 4708 |
| 2026-10-01 | Tools: durability and uses | 4984 |
| 2026-10-01 | Debug panel item giver | 5022 |
| 2026-10-01 | Gameplay loop: caves, sky, boss, audio, stations, farming, saves v3 | 7880 |
| 2026-10-01 | Hit and menu sounds; no gem pickaxe hint while tired | 7917 |
| 2026-10-01 | World map (Tab) | 8131 |

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

- **2026-10-01: Workbench recipes, flowers and animals.**
  - **Workbench crafting:** facing a placed workbench and pressing **E** opens its recipe list (a second
    `CraftingMenu`, titled "Workbench"); E or Esc closes it. Recipes are Minicraft's `workbenchRecipes`:
    - Wood sword / axe / hoe / pickaxe / shovel: 5 wood each.
    - Rock sword / axe / hoe / pickaxe / shovel: 5 wood + 5 stone each.
    - Wood bow: 5 wood + 2 string. Rock bow: 5 wood + 5 stone + 2 string.
    - Arrow x3: 2 wood + 2 stone.
  - New items with Minicraft+ icons: the 12 tools (they don't stack, like `ToolItem`), Arrow, String, Leather,
    Raw Beef, Raw Pork, White Wool and 8 flowers. Tools are craftable but don't do anything special yet (no extra
    damage, no tilling/digging). Nothing drops string yet, so the bows can't be crafted until a source exists.
  - **Flowers** (new `Tile::Flower`, Minicraft+ `FlowerTile`):
    - Generated in patches on grass by the classic `createTopMap` flower pass.
    - 8 kinds (dandelion, poppy, oxeye daisy, cornflower, allium, blue orchid, rose, iris). The kind comes from the
      seed and the tile's 8x8 region, so saves don't change.
    - Walkable, drawn over connected grass. Punching one picks it: its flower item drops and grass is left.
  - **Animals** (cow, pig, sheep; Minicraft's `PassiveMob`):
    - Random walks of 45 ticks, starting 1 in 40 ticks, with each axis often zero, so they often stand still.
    - Walk at 30 px/s (MobAi's walkTime 2); can't swim.
    - Health: cow 10, pig and sheep 8. Punching hurts them (red number, white flash, knockback).
    - On death they drop Minicraft's normal loot: cow 1-2 leather + raw beef, pig 1-2 raw pork, sheep 1-2 white
      wool and 1-2 raw beef.
    - Up to 10 are kept alive, spawning on grass or flowers 10-20 tiles from the player at any time of day.
  - **Mobs refactor** (`mob.h/.cpp`, `mobs.h/.cpp` replace `zombie.h/.cpp`):
    - The `Mob` base class holds MobAi's shared tick: walking and collision, hurt, knockback, flash and drawing.
    - `Zombie` (chase + contact punch, unchanged behaviour) and `Animal` (passive wandering) are subclasses.
    - `Mobs` owns, spawns, despawns, punches and draws them all.
  - Debug panel: zombie and animal counts, separate spawn toggles, "Spawn zombie nearby" / "Spawn animal nearby"
    and "Remove all mobs".
- **2026-10-01: Tools work like Minicraft's** (`player_actions.h/.cpp`, Minicraft's `Player.attack` + `Tile.interact`).
  - **Durability:** tool type durability x (level + 1): shovel 34, hoe 30, sword 52, pickaxe 38, axe 34, bow 30 (rock
    tools double). It's stored per tool (`Inventory::Stack::durability`), kept when dropped, stowed or saved (save
    format v2; v1 saves still load with fresh tools). At 0 a tool breaks and leaves the hand. The HUD shows the held
    tool's durability % (red to green) right of the hearts.
  - **Swinging a tool** costs the usual 1 energy, then:
    - **Axe on a tree / pickaxe on rock:** pays extra energy (axe 4 - level, pickaxe on rock 5 - level) and 1
      durability, and deals level * 5 + 10 + 0-4 damage. A wood axe fells a tree in 2 swings (5 energy each) instead
      of ~10 punches; a wood pickaxe breaks rock in ~5 swings (6 energy each) instead of 25. Rock mined with a
      pickaxe drops 2-4 stone and 2 coal.
    - **Shovel:** grass -> dirt; sand / dirt -> hole, dropping sand / dirt. **Hoe:** grass / dirt -> farmland.
      **Pickaxe on grass:** path. Each costs 4 - level energy and 1 durability.
    - **Otherwise it attacks:** mobs in reach take 1-2 plus the tool's bonus (sword wood 3-4 / rock 6-8, axe wood 2-5,
      pickaxe 1-2, others 1; 1 durability), the tile takes a normal 1-3 hit, and 1 more durability if anything was
      hit. With too little energy for a tool's use, the swing falls back to this.
  - New tiles Farmland, Path and Hole (holes block mobs and furniture like water, the player can walk in them) and
    items Sand, Dirt and Coal, with Minicraft+ sprites.
  - Not yet: shooting bows (no string to craft them), seeds from shovel/hoe, farming, water filling holes.

- **2026-10-01: Debug item giver.** The F3 debug panel has a "Give items" section: pick any item and an amount
  (1-999) and press Give, or use the shortcuts "+50 wood & stone", "All tools" (every wood and rock tool) and "Clear
  inventory". What doesn't fit in the inventory is dropped at the player's feet.

- **2026-10-01: The Minicraft gameplay loop** (branch `gameplay-loop`). Ported from the original Minicraft and
  Minicraft+ (`Level`, `LevelGen`, the tile, mob, furniture and item classes, `Recipes`, `Sound`).
  - **Levels** (`world.h/.cpp`): the sky, the surface and three caves, 256x256 each, generated from the world seed
    (`world_gen.cpp`: `createUndergroundMap` and `createSkyMap`). Stairs down are cut into 3x3 patches of rock (4 per
    level) and lead to stairs up at the same spot below, in a small dirt room; the surface's stairs up to the sky
    sit in a ring of hard rock that only a gem pickaxe breaks. Stepping onto stairs changes level with a short fade.
    Caves are pitch black except around light: the player (40 px), torches, lanterns and lava.
  - **Caves**: dirt tunnels through rock, iron ore (B1), gold (B2) and gems (B3), water pools on B2 and lava on B3
    (the original's flooding is lowered to lakes, `kCaveLiquidBelow`). Ores need a pickaxe (6 - level energy) and
    drop ore on every hit; lava burns (4 damage) and lights its surroundings.
  - **Mobs** (`mob.h/.cpp`, `mobs.h/.cpp`): every enemy has a level (sprite row, health = base x level^2 x 2, contact
    damage = level): zombies, skeletons (shoot arrows), slimes (hop), creepers (a 1 s fuse, then a blast that hurts
    everything around and digs holes) and snakes (deepest caves). Enemies spawn at night on the surface and anytime
    in the dark below (never in torch or lantern light); the sky has level-4 slimes and zombies.
  - **The Air Wizard** (2000 health) waits in the middle of the sky: it keeps its distance, teleports back when the
    player runs off, and casts spirals of sparks that speed up as it weakens. A boss bar shows its health; beating
    it opens the "You won!" screen.
  - **Crafting stations** (`recipe.cpp`): workbench (tools, torches, planks, bricks, walls, doors, furnace, oven,
    chest, anvil, loom, lantern, armour), furnace (iron, gold, glass), oven (cooked pork, steak, bread, baked potato),
    anvil (iron/gold/gem tools and armour, gold apple), loom (string, wool, bed). E on a station opens its recipes;
    long lists scroll.
  - **Furniture** (`furniture.h/.cpp`): chests with their own inventory (`container_menu.h/.cpp`: Left/Right switch
    lists, Enter moves a stack), lanterns (light radius 72 px), beds (sleep through the evening or night to the
    morning; the bed becomes the respawn point), and the power glove (new worlds start with it) to pick furniture
    back up (chests only when empty).
  - **Player**: hunger (Minicraft+ normal difficulty: time, walking and low energy drain it; above half it heals,
    at zero it starves down to 3 hearts), food (2 energy to eat), armour (leather to gem: soaks hits and every
    level + 1 points cost a heart), bows (shoot arrows from the inventory; the HUD shows how many are left), and
    death: everything carried goes into a death chest where the player fell, then "You died!" with Respawn.
  - **World ticks**: grass spreads onto dirt, acorns grow into trees and cacti from cactus, wheat grows on farmland
    (hoe or shovel grass for seeds; harvest at full age for 2-4 wheat), water and lava flow into holes, damaged
    trees and rock heal.
  - **Placing tiles**: dirt and sand fill holes, water and lava; clouds fill the sky's edge; torches, plank and
    stone brick floors, walls and doors (punch to open/close, axe or pickaxe to take back).
  - **Audio** (`audio.h/.cpp`): Minicraft+'s sound effects through SDL3 audio streams (4 voices per sound). M mutes;
    the Options screen (title and pause menus) toggles the sound and sets the volume. Every hit on a tile (trees,
    rock, ore, cacti, walls) plays the hit sound; moving the cursor in any menu plays "select" and closing a screen
    (inventory, crafting, chest, pause) plays the craft sound, like Minicraft+'s Menu and Game.exitDisplay.
  - **Saves v3**: every level's tiles and tile data, furniture with chest contents, the current level, hunger,
    armour, the bed spawn point, the boss flag and the time played. Version 1 and 2 saves still load: the other
    levels are generated from the seed and stairs are fitted into the saved surface.
  - **Debug panel**: hunger/armour/level info, spawn any mob at any level, jump to any level, full-bright, and
    "Cave kit" / "Boss kit" item shortcuts.
  - Not ported: the dungeon (obsidian knight, keys), potions and the enchanter, dyes and coloured wool/beds, boats,
    fishing, TNT, signs, quests and achievements, knights.

- **2026-10-01: World map** (`map_screen.h/.cpp`). Tab shows the whole level the player is on, one pixel per tile
  (shrunk to fit the view), with markers: the player (blinking), stairs down, and on the surface the stairs up to the
  sky where the Air Wizard waits (stairs up in the caves; the Air Wizard himself in the sky), plus a legend. Tab or
  Esc closes it; the world keeps running behind it like the other screens.

## Next
- Phase 1 leftovers: move the simulation into `game-core`, recipes/tiles/mobs as data, a WASM build.
