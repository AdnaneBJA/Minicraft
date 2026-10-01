#include "audio.h"
#include "camera.h"
#include "collision.h"
#include "container_menu.h"
#include "crafting_menu.h"
#include "day_night.h"
#include "debug_overlay.h"
#include "effects.h"
#include "font.h"
#include "game_menu.h"
#include "hud.h"
#include "inventory_menu.h"
#include "items.h"
#include "player.h"
#include "player_actions.h"
#include "world.h"
#include "world_save.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

class Game {
public:
    // Minimum view in world pixels; the window is scaled up by the largest whole factor that still fits it.
    static constexpr int kViewWidth = 240;
    static constexpr int kViewHeight = 135;
    // Holding Space works like Minicraft: the press punches once, and only once the key has been held for a moment
    // (Minicraft waits for the OS key repeat to make the key "sticky") does it unload rapid punches until energy
    // runs out.
    static constexpr float kPunchHoldDelay = 0.5f;
    static constexpr float kRapidPunchInterval = 3.0f / 60.0f;  // 20 punches/s
    static constexpr SDL_Color kPlayerDamageColor{255, 0, 204, 255};  // Minicraft: Color.get(-1, 504)
    static constexpr SDL_Color kSavedColor{0, 255, 0, 255};
    static constexpr SDL_Color kErrorColor{255, 0, 0, 255};
    static constexpr float kTick = 1.0f / 60.0f;
    static constexpr float kFadeSeconds = 0.5f;   // the black fade after taking the stairs
    static constexpr float kSleepFadeSeconds = 1.5f;
    static constexpr float kNoteSeconds = 2.5f;
    static constexpr int kLightScanTiles = 24;    // torches and lava this far from the player give light

    bool init() {
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            SDL_Log("SDL_Init failed: %s", SDL_GetError());
            return false;
        }
        SDL_Window* window = nullptr;
        SDL_Renderer* renderer = nullptr;
        if (!SDL_CreateWindowAndRenderer("Minicraft", kViewWidth * 4, kViewHeight * 4, SDL_WINDOW_RESIZABLE,
                                         &window, &renderer)) {
            SDL_Log("Window creation failed: %s", SDL_GetError());
            return false;
        }
        window_.reset(window);
        renderer_.reset(renderer);
        SDL_SetRenderVSync(renderer, 1);
        imgui_.init(window, renderer);

        const char* basePath = SDL_GetBasePath();
        const std::string assets = std::string(basePath ? basePath : "") + "assets/";
        const std::string sprites = assets + "sprites/";
        audio_.init(assets + "audio/");  // without a sound device the game just stays silent
        menu_.setAudio(&audio_);
        menu_.setSoundSettings(audio_.muted(), audio_.volume());
        furnitureSheet_ = loadTexture(renderer, sprites + "furniture.png");
        projectileSheet_ = loadTexture(renderer, sprites + "projectiles.png");
        if (!player_.load(renderer, sprites + "player.png", sprites + "hud.png") ||
            !world_.load(renderer, sprites + "tiles.png") || !effects_.load(renderer, sprites + "smash.png") ||
            !hud_.load(renderer, sprites + "hud.png") || !font_.load(renderer, sprites + "font.png") ||
            !itemIcons_.load(renderer, sprites + "items.png") ||
            !inventoryMenu_.load(renderer, sprites + "inventory_counter.png") || !furnitureSheet_ ||
            !projectileSheet_ || !mobSprites_.load(renderer, sprites) || !menu_.load(renderer, sprites + "title.png")) {
            return false;
        }
        menu_.openTitle(saves_.list());
        return true;
    }

    void run() {
        Uint64 previous = SDL_GetTicksNS();
        while (running_) {
            const Uint64 now = SDL_GetTicksNS();
            // Clamp so a stall (window drag, breakpoint) doesn't teleport the player.
            const float dt = std::min(static_cast<float>(now - previous) / 1e9f, 0.1f);
            previous = now;
            time_ += dt;

            handleEvents();
            update(dt);
            draw();
        }
    }

private:
    // Saves live in the per-user data folder (e.g. %APPDATA%/Minicraft/Minicraft/saves on Windows), like
    // Minicraft's game directory.
    static std::filesystem::path savesDirectory() {
        char* prefPath = SDL_GetPrefPath("Minicraft", "Minicraft");
        std::filesystem::path path;
        if (prefPath) path = std::filesystem::path(reinterpret_cast<const char8_t*>(prefPath));
        SDL_free(prefPath);
        return path / "saves";
    }

    Level& level() { return world_.current(); }
    const Level& level() const { return world_.current(); }

    // Everything that belongs to one play session goes back to a fresh start (the world is set separately).
    void resetWorldState() {
        effects_.clear();
        inventory_.clear();
        closeMenus();
        player_.setHeldItem(std::nullopt);
        player_.refillStats();
        player_.removeArmor();
        dayNight_ = DayNight{};
        punchRepeatTimer_ = 0.0f;
        spawnLevel_ = -1;
        playSeconds_ = 0.0f;
        onStairs_ = false;
        fadeTimer_ = 0.0f;
        notes_.clear();
    }

    void enterWorld(std::string name) {
        worldName_ = std::move(name);
        inWorld_ = true;
        menu_.close();
    }

    SDL_FPoint surfaceSpawn() const { return world_.level(World::kSurfaceIndex).map.findSpawnPoint(); }

    void createWorld(std::string name, std::uint32_t seed) {
        world_.generate(seed);
        resetWorldState();
        const SDL_FPoint spawn = surfaceSpawn();
        player_.setPosition(spawn.x, spawn.y);
        inventory_.add(ItemType::PowerGlove);  // like the original Minicraft, the player starts with the glove
        enterWorld(std::move(name));
        saveWorld();  // so the new world is listed under Load World right away
    }

    bool loadWorld(std::string name) {
        auto data = saves_.load(name);
        if (!data) return false;
        resetWorldState();
        if (data->levels.size() == World::kLevelCount) {
            for (int i = 0; i < World::kLevelCount; ++i) {
                auto& saved = data->levels[static_cast<std::size_t>(i)];
                world_.restoreLevel(i, data->seed, std::move(saved.tiles), std::move(saved.data));
                for (auto& piece : saved.furniture) {
                    Furniture::Piece restored{piece.type, piece.x, piece.y, piece.deathChest};
                    for (const auto& stack : piece.contents) restored.contents.add(stack);
                    world_.level(i).furniture.restore(std::move(restored));
                }
            }
            world_.airWizardBeaten = data->airWizardBeaten;
        } else {
            // A save from before the caves: generate the other levels and fit the stairs into the saved surface.
            world_.generate(data->seed);
            world_.restoreLevel(World::kSurfaceIndex, data->seed, std::move(data->levels[0].tiles),
                                std::move(data->levels[0].data));
            world_.linkStairs();
            inventory_.add(ItemType::PowerGlove);
        }
        world_.setCurrent(data->currentLevel);
        world_.spawnBoss();
        player_.setPosition(data->playerX, data->playerY);
        player_.restoreStats(data->health, data->energy, data->hunger, data->armor, data->armorPoints);
        dayNight_.restore(data->dayTick, data->pastDay1);
        for (const auto& stack : data->inventory) inventory_.add(stack);
        spawnLevel_ = data->spawnLevel;
        spawnPoint_ = {data->spawnX, data->spawnY};
        playSeconds_ = static_cast<float>(data->secondsPlayed);
        onStairs_ = true;  // don't take the stairs straight away if the save was made on them
        enterWorld(std::move(name));
        return true;
    }

    bool saveWorld() const {
        const SDL_FRect bounds = player_.bounds();
        WorldSaveData data;
        data.seed = world_.seed();
        data.width = World::kSize;
        data.height = World::kSize;
        for (int i = 0; i < World::kLevelCount; ++i) {
            const Level& source = world_.level(i);
            WorldSaveData::Level saved{source.map.tiles(), source.map.data(), {}};
            for (const auto& piece : source.furniture.all()) {
                saved.furniture.push_back({piece.type, piece.x, piece.y, piece.deathChest, piece.contents.stacks()});
            }
            data.levels.push_back(std::move(saved));
        }
        data.currentLevel = world_.currentIndex();
        data.playerX = bounds.x;
        data.playerY = bounds.y;
        data.health = std::max(1, player_.health());
        data.energy = player_.energy();
        data.hunger = player_.hunger();
        data.armor = player_.armor();
        data.armorPoints = player_.armorPoints();
        data.spawnLevel = spawnLevel_;
        data.spawnX = spawnPoint_.x;
        data.spawnY = spawnPoint_.y;
        data.dayTick = dayNight_.tick();
        data.pastDay1 = dayNight_.pastDay1();
        data.airWizardBeaten = world_.airWizardBeaten;
        data.secondsPlayed = static_cast<int>(playSeconds_);
        // The item in hand isn't in the inventory; save it as part of it so it isn't lost.
        Inventory carried = inventory_;
        if (const auto& held = player_.heldItem()) carried.add(*held);
        data.inventory = carried.stacks();
        return saves_.save(worldName_, data);
    }

    void handleMenuAction(const GameMenu::Action& action) {
        using Kind = GameMenu::Action::Kind;
        switch (action.kind) {
            case Kind::CreateWorld: createWorld(action.worldName, action.seed); break;
            case Kind::LoadWorld:
                if (!loadWorld(action.worldName)) menu_.showMessage("Could not load world", kErrorColor);
                break;
            case Kind::Resume: menu_.close(); break;
            case Kind::Save:
                if (saveWorld()) menu_.showMessage("World saved!", kSavedColor);
                else menu_.showMessage("Could not save!", kErrorColor);
                break;
            case Kind::SaveAndQuit:
                if (player_.isDead()) respawn();  // never save a dead player
                if (!saveWorld()) {
                    menu_.showMessage("Could not save!", kErrorColor);
                    break;
                }
                inWorld_ = false;
                menu_.openTitle(saves_.list());
                break;
            case Kind::Respawn:
                respawn();
                menu_.close();
                break;
            case Kind::ToggleSound: audio_.toggleMuted(); break;
            case Kind::VolumeDown: audio_.setVolume(audio_.volume() - 1); break;
            case Kind::VolumeUp: audio_.setVolume(audio_.volume() + 1); break;
            case Kind::Quit: running_ = false; break;
            case Kind::None: break;
        }
        menu_.setSoundSettings(audio_.muted(), audio_.volume());
    }

    // Debug "Regenerate": a whole new world from `seed`, keeping the player's things.
    void newWorld(std::uint32_t seed) {
        world_.generate(seed);
        effects_.clear();
        closeMenus();
        const SDL_FPoint spawn = surfaceSpawn();
        player_.setPosition(spawn.x, spawn.y);
        onStairs_ = false;
    }

    // Player.die: everything the player carried goes into a death chest where they fell, then the death screen.
    void die() {
        Inventory carried = inventory_;
        if (const auto& held = player_.heldItem()) carried.add(*held);
        if (const auto& armor = player_.armor()) carried.add(*armor, 1);
        if (!carried.empty()) {
            const SDL_FPoint c = player_.center();
            level().furniture.addDeathChest(c.x, c.y, std::move(carried));
        }
        inventory_.clear();
        player_.setHeldItem(std::nullopt);
        player_.removeArmor();
        closeMenus();
        audio_.play(Sound::Death);
        menu_.openDeath(static_cast<int>(playSeconds_));
    }

    // Back at the bed the player last slept in, or the surface spawn point, with full stats.
    void respawn() {
        int index = World::kSurfaceIndex;
        SDL_FPoint spawn = surfaceSpawn();
        if (spawnLevel_ >= 0) {
            index = spawnLevel_;
            spawn = spawnPoint_;
        }
        changeLevel(index, false);
        player_.setPosition(spawn.x, spawn.y);
        player_.refillStats();
        onStairs_ = false;
    }

    // Moves the player to another level at the same position (the stairs line up), with Minicraft's short fade.
    void changeLevel(int index, bool viaStairs) {
        if (index == world_.currentIndex()) return;
        world_.setCurrent(index);
        if (level().isSky()) world_.spawnBoss();
        effects_.clear();
        onStairs_ = viaStairs;
        fadeTimer_ = kFadeSeconds;
        fadeDuration_ = kFadeSeconds;
        notify(level().name());
    }

    void notify(std::string text) {
        // Repeats of the note already showing just keep it up.
        if (!notes_.empty() && notes_.back().first == text) {
            notes_.back().second = kNoteSeconds;
            return;
        }
        notes_.emplace_back(std::move(text), kNoteSeconds);
        if (notes_.size() > 3) notes_.erase(notes_.begin());
    }

    // Space: punch, swing, use or place what's in hand.
    void useOrPunch() {
        PlayerActions actions(level(), player_, inventory_, effects_, audio_);
        actions.useOrPunch();
        for (const auto& message : actions.messages()) notify(message);
    }

    // Opening a menu puts the held item back in the inventory, or drops it if there's no room (Minicraft's
    // tryAddToInvOrDrop).
    void stowHeldItem() {
        const auto held = player_.heldItem();
        if (!held) return;
        player_.setHeldItem(std::nullopt);
        if (const int leftover = inventory_.add(*held); leftover > 0) {
            const SDL_FPoint middle = player_.center();
            level().drops.spawn(held->type, leftover, middle.x, middle.y, held->durability);
        }
    }

    // Products that don't fit in the inventory are dropped at the player's feet, like Minicraft.
    void craft(const Recipe& recipe) {
        const int leftover = recipe.craft(inventory_);
        if (leftover < 0) return;
        audio_.play(Sound::Craft);
        if (leftover == 0) return;
        const SDL_FPoint middle = player_.center();
        level().drops.spawn(recipe.product(), leftover, middle.x, middle.y);
    }

    bool menuOpen() const { return inventoryMenu_.isOpen() || craftingMenu_.isOpen() || containerMenu_.isOpen(); }

    void closeMenus() {
        inventoryMenu_.close();
        craftingMenu_.close();
        containerMenu_.close();
    }

    // E while facing furniture uses it (Furniture.use): a crafting station opens its recipes, a chest its
    // contents, a bed lets the player sleep. Returns false if there's no furniture to use there.
    bool useFurniture() {
        const SDL_Point target = player_.interactionTile();
        Furniture::Piece* piece = level().furniture.at(target.x, target.y);
        if (!piece) return false;
        if (piece->isContainer()) {
            stowHeldItem();
            containerMenu_.open(target.x, target.y, piece->deathChest ? "Death Chest" : "Chest");
            return true;
        }
        if (piece->type == ItemType::Bed) {
            sleep(*piece);
            return true;
        }
        std::vector<Recipe> recipes = Recipe::stationRecipes(piece->type);
        if (recipes.empty()) return false;  // a lantern: nothing to use
        stowHeldItem();
        craftingMenu_.open(std::move(recipes), itemName(piece->type));
        return true;
    }

    // Bed.use: only in the evening or at night. Sleeping skips to the morning and makes the bed the respawn point.
    void sleep(const Furniture::Piece& bed) {
        const DayNight::Time time = dayNight_.time();
        if (time != DayNight::Time::Evening && time != DayNight::Time::Night) {
            notify("Can't sleep! Wait for the evening");
            return;
        }
        spawnLevel_ = world_.currentIndex();
        // Wake up just below the bed (or on it, if that tile is blocked).
        const int tx = collision::tileIndex(bed.x);
        const int ty = collision::tileIndex(bed.y);
        const bool below = !level().map.isSolidAt(tx, ty + 1) && !level().furniture.at(tx, ty + 1);
        spawnPoint_ = {static_cast<float>(tx * TileMap::kTileSize),
                       static_cast<float>((below ? ty + 1 : ty) * TileMap::kTileSize) - 3.0f};
        dayNight_.setTime(DayNight::Time::Morning);
        player_.setPosition(spawnPoint_.x, spawnPoint_.y);
        level().mobs.clearEnemies();  // the night's monsters are gone by morning
        fadeTimer_ = kSleepFadeSeconds;
        fadeDuration_ = kSleepFadeSeconds;
        notify("You slept until morning");
    }

    // Moves the stack picked in the chest screen to the other side. What doesn't fit stays where it was.
    void transfer(const ContainerMenu::Transfer& move) {
        const SDL_Point tile = containerMenu_.chestTile();
        Furniture::Piece* chest = level().furniture.at(tile.x, tile.y);
        if (!chest) {
            containerMenu_.close();
            return;
        }
        Inventory& from = move.fromChest ? chest->contents : inventory_;
        Inventory& to = move.fromChest ? inventory_ : chest->contents;
        const Inventory::Stack stack = from.take(move.index);
        if (const int leftover = to.add(stack); leftover > 0) {
            Inventory::Stack back = stack;
            back.count = leftover;
            from.add(back);
        }
        audio_.play(Sound::Pickup);
        // An emptied death chest disappears (DeathChest).
        if (chest->deathChest && chest->contents.empty()) {
            level().furniture.remove(chest);
            containerMenu_.close();
        }
    }

    void handleEvents() {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT) {
                running_ = false;
            } else if (event.type == SDL_EVENT_KEY_DOWN) {
                handleKey(event.key.key, event.key.repeat);
            } else if (event.type == SDL_EVENT_TEXT_INPUT) {
                menu_.handleText(event.text.text);
            }
        }
    }

    void handleKey(SDL_Keycode key, bool repeat) {
        if (menu_.isOpen()) {
            if (!ImGui::GetIO().WantCaptureKeyboard) handleMenuAction(menu_.handleKey(key, repeat));
            return;
        }
        if (repeat) return;
        if (key == DebugOverlay::kToggleKey) debug_.toggle();
        if (ImGui::GetIO().WantCaptureKeyboard) return;  // typing in the debug panel
        if (key == SDLK_M) {
            audio_.toggleMuted();
            menu_.setSoundSettings(audio_.muted(), audio_.volume());
            notify(audio_.muted() ? "Sound off" : "Sound on");
            return;
        }
        if (key == SDLK_ESCAPE) {
            if (menuOpen()) closeMenus();
            else menu_.openPause();
            return;
        }
        if (containerMenu_.isOpen()) {
            if (key == InventoryMenu::kToggleKey) {
                containerMenu_.close();
                return;
            }
            const SDL_Point tile = containerMenu_.chestTile();
            if (const Furniture::Piece* chest = level().furniture.at(tile.x, tile.y)) {
                if (const auto move = containerMenu_.handleKey(key, chest->contents, inventory_)) transfer(*move);
            } else {
                containerMenu_.close();
            }
            return;
        }
        if (craftingMenu_.isOpen()) {
            if (key == InventoryMenu::kToggleKey || key == CraftingMenu::kToggleKey) {
                craftingMenu_.close();
            } else if (const Recipe* recipe = craftingMenu_.handleKey(key, inventory_)) {
                craft(*recipe);
            }
            return;
        }
        if (inventoryMenu_.isOpen()) {
            if (key == InventoryMenu::kToggleKey) {
                inventoryMenu_.close();
                return;
            }
            // Selecting a slot puts that whole stack in the player's hand and closes the inventory.
            if (const auto slot = inventoryMenu_.handleKey(key, inventory_)) {
                player_.setHeldItem(inventory_.take(*slot));
                inventoryMenu_.close();
            }
            return;
        }
        if (key == InventoryMenu::kToggleKey) {
            if (useFurniture()) return;
            stowHeldItem();
            inventoryMenu_.toggle();
            return;
        }
        if (key == CraftingMenu::kToggleKey) {
            stowHeldItem();
            craftingMenu_.open(Recipe::personalRecipes(), "Crafting");
            return;
        }
        if (key == SDLK_SPACE) {
            useOrPunch();  // a fresh press always acts right away
            punchRepeatTimer_ = kPunchHoldDelay;
        }
    }

    void update(float dt) {
        menu_.update(dt);
        // SDL text input (and the IME) is only on while a menu text field is selected.
        SDL_Window* window = window_.get();
        if (menu_.wantsTextInput() != SDL_TextInputActive(window)) {
            if (menu_.wantsTextInput()) SDL_StartTextInput(window);
            else SDL_StopTextInput(window);
        }
        if (inWorld_ && !menu_.isOpen()) updateWorld(dt);  // not on the title screens, nor paused
        updateView();
    }

    // The light sources around the player: the player, placed lanterns, and torches and lava within reach.
    std::vector<Lighting::Light> collectLights() const {
        std::vector<Lighting::Light> lights = level().furniture.lights();
        const SDL_FPoint p = player_.center();
        lights.push_back({p.x - 1.0f, p.y - 4.0f, player_.lightRadius()});
        const TileMap& map = level().map;
        const int px = collision::tileIndex(p.x);
        const int py = collision::tileIndex(p.y);
        for (int ty = py - kLightScanTiles; ty <= py + kLightScanTiles; ++ty) {
            for (int tx = px - kLightScanTiles; tx <= px + kLightScanTiles; ++tx) {
                if (!map.inBounds(tx, ty)) continue;
                const Tile tile = map.tileAt(tx, ty);
                // TorchTile: 5, LavaTile: 6 (x 8 px), centred on the tile.
                const int radius = tile == Tile::Torch ? 5 : tile == Tile::Lava ? 6 : 0;
                if (radius == 0) continue;
                lights.push_back({static_cast<float>(tx * TileMap::kTileSize + 8),
                                  static_cast<float>(ty * TileMap::kTileSize + 8), static_cast<float>(radius * 8)});
            }
        }
        return lights;
    }

    // How dark the current level is: the time of day on the surface, pitch black underground, bright in the sky.
    float darkness() const {
        if (debug_.fullBright() || level().isSky()) return 0.0f;
        if (level().isUnderground()) return 1.0f;
        return dayNight_.darkness();
    }

    void updateWorld(float dt) {
        playSeconds_ += dt;
        fadeTimer_ = std::max(0.0f, fadeTimer_ - dt);
        for (auto& note : notes_) note.second -= dt;
        std::erase_if(notes_, [](const auto& note) { return note.second <= 0.0f; });

        // While a menu is open or typing in the debug panel, keys must not move the player.
        static const std::array<bool, SDL_SCANCODE_COUNT> noKeys{};
        const bool blockKeys = ImGui::GetIO().WantCaptureKeyboard || menuOpen();
        const bool* keys = blockKeys ? noKeys.data() : SDL_GetKeyboardState(nullptr);
        Level& here = level();
        const std::vector<SDL_FRect> obstacles = here.furniture.hitboxes();
        if (const int damageTaken = player_.update(dt, keys, here.map, obstacles); damageTaken > 0) {
            const SDL_FPoint middle = player_.center();
            effects_.addDamageNumber(damageTaken, middle.x, middle.y, kPlayerDamageColor);
            audio_.play(Sound::PlayerHurt);
        }
        // Held Space: after kPunchHoldDelay, punch every kRapidPunchInterval (the first came from the key press).
        if (keys[SDL_SCANCODE_SPACE]) {
            punchRepeatTimer_ -= dt;
            if (punchRepeatTimer_ <= 0.0f) {
                useOrPunch();
                punchRepeatTimer_ += kRapidPunchInterval;
            }
        }

        // Stepping onto stairs takes them (Player.tick's onStairDelay): arriving on the other end doesn't send the
        // player straight back, since they have to step off the stairs first.
        const SDL_FPoint c = player_.center();
        const int tx = collision::tileIndex(c.x);
        const int ty = collision::tileIndex(c.y);
        const Tile under = here.map.inBounds(tx, ty) ? here.map.tileAt(tx, ty) : Tile::Rock;
        const bool stairs = under == Tile::StairsDown || under == Tile::StairsUp;
        if (stairs && !onStairs_) {
            const int next = world_.currentIndex() + (under == Tile::StairsDown ? 1 : -1);
            if (next >= 0 && next < World::kLevelCount) {
                changeLevel(next, true);
                return;
            }
        }
        onStairs_ = stairs;

        dayNight_.update(dt);
        const std::vector<Lighting::Light> lights = collectLights();
        const Mobs::Context context{here.map,         player_, effects_, here.drops, here.projectiles,
                                    audio_,           obstacles};
        const Mobs::SpawnRules rules{here.depth(), dayNight_.time() == DayNight::Time::Night, lights};
        here.mobs.update(dt, context, rules);

        // Level.tick at 60 Hz: arrows and sparks, and Minicraft's random tile ticks (crops grow, grass spreads,
        // liquids flow into holes).
        levelTickAccumulator_ += dt;
        while (levelTickAccumulator_ >= kTick) {
            levelTickAccumulator_ -= kTick;
            here.projectiles.tick(here.map, player_, here.mobs, effects_, audio_);
            here.map.tickRandomTiles(World::kSize / 2, World::kSize / 2, World::kSize / 2,
                                     World::kSize * World::kSize / 50);
        }

        if (here.drops.update(dt, here.map, player_.hitbox(), inventory_) > 0) audio_.play(Sound::Pickup);
        effects_.update(dt);

        if (here.mobs.takeBossDefeated()) {
            world_.airWizardBeaten = true;
            closeMenus();
            menu_.openWon(static_cast<int>(playSeconds_));
        }
        if (player_.isDead()) die();
    }

    // Picks the render scale for the window size and points the camera at the player.
    void updateView() {
        int outputWidth = 0;
        int outputHeight = 0;
        SDL_GetCurrentRenderOutputSize(renderer_.get(), &outputWidth, &outputHeight);
        scale_ = static_cast<float>(std::max(1, std::min(outputWidth / kViewWidth, outputHeight / kViewHeight)));
        camera_.setView(static_cast<float>(outputWidth) / scale_, static_cast<float>(outputHeight) / scale_, scale_);
        if (!inWorld_) return;
        // Follow the position the player is actually drawn at (snapped), so the two never disagree by a pixel.
        const SDL_FRect bounds = player_.bounds();
        camera_.follow(camera_.snap(bounds.x) + bounds.w / 2.0f, camera_.snap(bounds.y) + bounds.h / 2.0f,
                       level().map.pixelWidth(), level().map.pixelHeight());
    }

    void draw() {
        SDL_Renderer* renderer = renderer_.get();
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        // World: drawn in world pixels, scaled up by a whole factor.
        SDL_SetRenderScale(renderer, scale_, scale_);
        if (inWorld_) drawWorld(renderer);
        menu_.draw(renderer, hud_, font_, camera_.width(), camera_.height());

        // Debug overlay and UI: drawn in screen pixels so lines stay thin.
        SDL_SetRenderScale(renderer, 1.0f, 1.0f);
        if (inWorld_) debug_.drawWorldOverlay(renderer, camera_, scale_, level().map, player_, level().mobs);

        // ImGui runs every frame (even without the debug panel) so its keyboard capture state stays current.
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        if (inWorld_) drawDebugPanel();
        ImGui::Render();
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);

        SDL_RenderPresent(renderer);
    }

    void drawWorld(SDL_Renderer* renderer) {
        const Level& here = level();
        SDL_Texture* furniture = furnitureSheet_.get();
        here.map.draw(renderer, camera_, time_);
        here.drops.draw(renderer, camera_, itemIcons_);
        const float playerY = player_.center().y;
        here.furniture.draw(renderer, camera_, furniture, playerY, true);
        here.mobs.draw(renderer, camera_, mobSprites_, playerY, true);
        player_.draw(renderer, camera_);
        if (player_.isCarryingFurniture()) {
            const SDL_FPoint carried = player_.carriedFurniturePosition();
            Furniture::drawSprite(renderer, camera_, furniture, player_.heldItem()->type, carried.x, carried.y);
        }
        here.furniture.draw(renderer, camera_, furniture, playerY, false);
        here.mobs.draw(renderer, camera_, mobSprites_, playerY, false);
        here.projectiles.draw(renderer, camera_, projectileSheet_.get());
        effects_.draw(renderer, camera_, font_);

        // Darkness (night on the surface, always underground) with circles of light around the player, lanterns,
        // torches and lava (Minicraft's LightOverlay).
        lighting_.draw(renderer, camera_, darkness(), collectLights());

        // UI in view pixels (same scale, not moved by the camera).
        const float viewWidth = std::floor(camera_.width());
        const float viewHeight = std::floor(camera_.height());
        hud_.drawStatus(renderer, itemIcons_, player_, viewWidth, viewHeight);
        if (const auto& held = player_.heldItem()) {
            hud_.drawHeldItem(renderer, font_, itemIcons_, *held, viewHeight);
            if (isTool(held->type)) hud_.drawToolDurability(renderer, font_, *held, viewWidth, viewHeight);
            if (toolInfo(held->type).type == ToolType::Bow) {
                hud_.drawArrowCount(renderer, font_, itemIcons_, inventory_.count(ItemType::Arrow), viewHeight);
            }
        }
        if (const Mob* boss = here.mobs.boss()) {
            hud_.drawBossBar(renderer, font_, boss->health() * 100 / AirWizard::kMaxHealth, "Air Wizard", viewWidth);
        }
        drawNotes(renderer, viewWidth);
        inventoryMenu_.draw(renderer, hud_, font_, itemIcons_, inventory_, viewHeight);
        craftingMenu_.draw(renderer, hud_, font_, itemIcons_, inventory_, viewHeight);
        if (containerMenu_.isOpen()) {
            const SDL_Point tile = containerMenu_.chestTile();
            if (const Furniture::Piece* chest = here.furniture.at(tile.x, tile.y)) {
                containerMenu_.draw(renderer, hud_, font_, itemIcons_, chest->contents, inventory_, viewHeight);
            }
        }

        // The fade after changing level or sleeping.
        if (fadeTimer_ > 0.0f) {
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, static_cast<Uint8>(255.0f * fadeTimer_ / fadeDuration_));
            const SDL_FRect all{0.0f, 0.0f, camera_.width(), camera_.height()};
            SDL_RenderFillRect(renderer, &all);
        }
    }

    // Minicraft+'s notifications: short lines centred near the top, on a black background.
    void drawNotes(SDL_Renderer* renderer, float viewWidth) const {
        float y = level().mobs.boss() ? 24.0f : 4.0f;
        for (const auto& [text, timeLeft] : notes_) {
            const float x = std::floor((viewWidth - Font::textWidth(text)) / 2.0f);
            const SDL_FRect background{x - 1.0f, y - 1.0f, Font::textWidth(text) + 2.0f, 10.0f};
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_RenderFillRect(renderer, &background);
            font_.draw(renderer, text, x, y, SDL_Color{255, 255, 255, 255});
            y += 10.0f;
        }
    }

    void drawDebugPanel() {
        Level& here = level();
        const auto actions = debug_.drawPanel(camera_, scale_, here.map, player_, inventory_, here.drops.size(),
                                              here.mobs, dayNight_, here.name(), world_.currentIndex());
        if (actions.regenerateSeed) newWorld(*actions.regenerateSeed);
        if (actions.refillStats) player_.refillStats();
        if (actions.clearInventory) inventory_.clear();
        for (const Inventory::Stack& stack : actions.giveItems) {
            if (const int leftover = inventory_.add(stack.type, stack.count); leftover > 0) {
                const SDL_FPoint middle = player_.center();
                level().drops.spawn(stack.type, leftover, middle.x, middle.y);
            }
        }
        if (actions.spawnMob) {
            const SDL_FPoint p = player_.center();
            level().mobs.spawnNear(actions.spawnMob->first, actions.spawnMob->second, level().map,
                                   level().furniture.hitboxes(), p.x, p.y, 3, 6);
        }
        if (actions.clearMobs) level().mobs.clear();
        if (actions.setTime) dayNight_.setTime(*actions.setTime);
        if (actions.gotoLevel) {
            changeLevel(*actions.gotoLevel, false);
            // Clear a little room if the player landed inside rock (or over the edge of the sky).
            TileMap& map = level().map;
            const SDL_FPoint c = player_.center();
            const int tx = collision::tileIndex(c.x);
            const int ty = collision::tileIndex(c.y);
            for (int y = ty - 1; y <= ty + 1; ++y) {
                for (int x = tx - 1; x <= tx + 1; ++x) {
                    if (map.inBounds(x, y) && map.blocksMobsAt(x, y)) {
                        map.setTile(x, y, level().isSky() ? Tile::Cloud : Tile::Dirt);
                    }
                }
            }
        }
    }

    struct SdlQuit {
        ~SdlQuit() { SDL_Quit(); }
    };
    struct WindowDeleter {
        void operator()(SDL_Window* window) const { SDL_DestroyWindow(window); }
    };
    struct RendererDeleter {
        void operator()(SDL_Renderer* renderer) const { SDL_DestroyRenderer(renderer); }
    };
    // Owns the ImGui context; must be shut down before the renderer is destroyed.
    struct ImGuiContextGuard {
        bool initialised = false;
        void init(SDL_Window* window, SDL_Renderer* renderer) {
            ImGui::CreateContext();
            ImGui::GetIO().IniFilename = nullptr;  // don't write imgui.ini
            ImGui::StyleColorsDark();
            ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
            ImGui_ImplSDLRenderer3_Init(renderer);
            initialised = true;
        }
        ~ImGuiContextGuard() {
            if (!initialised) return;
            ImGui_ImplSDLRenderer3_Shutdown();
            ImGui_ImplSDL3_Shutdown();
            ImGui::DestroyContext();
        }
    };

    // Members are destroyed in reverse order: textures, ImGui and audio first, then renderer, window, and SDL_Quit.
    SdlQuit sdlQuit_;
    Audio audio_;
    std::unique_ptr<SDL_Window, WindowDeleter> window_;
    std::unique_ptr<SDL_Renderer, RendererDeleter> renderer_;
    ImGuiContextGuard imgui_;
    World world_;
    Player player_;
    Effects effects_;
    MobSprites mobSprites_;
    TexturePtr furnitureSheet_;
    TexturePtr projectileSheet_;
    DayNight dayNight_;
    Lighting lighting_;
    Inventory inventory_;
    InventoryMenu inventoryMenu_;
    CraftingMenu craftingMenu_;
    ContainerMenu containerMenu_;
    Hud hud_;
    Font font_;
    ItemIcons itemIcons_;
    Camera camera_;
    DebugOverlay debug_;
    GameMenu menu_;
    WorldSaves saves_{savesDirectory()};
    std::string worldName_;
    bool inWorld_ = false;  // a world is loaded (playing or paused); false on the title screens
    float scale_ = 1.0f;
    float punchRepeatTimer_ = 0.0f;
    float levelTickAccumulator_ = 0.0f;
    float time_ = 0.0f;
    float playSeconds_ = 0.0f;
    float fadeTimer_ = 0.0f;
    float fadeDuration_ = kFadeSeconds;
    bool onStairs_ = false;  // standing on stairs (stepping onto them takes them)
    int spawnLevel_ = -1;    // the bed the player respawns at (World index), or -1 for the surface spawn
    SDL_FPoint spawnPoint_{0.0f, 0.0f};
    std::vector<std::pair<std::string, float>> notes_;  // notifications and the seconds they have left
    bool running_ = true;
};

int main(int, char*[]) {
    Game game;
    if (!game.init()) {
        return 1;
    }
    game.run();
    return 0;
}
