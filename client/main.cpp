#include "audio.h"
#include "camera.h"
#include "collision.h"
#include "container_menu.h"
#include "crafting_menu.h"
#include "debug_overlay.h"
#include "effects.h"
#include "font.h"
#include "game_menu.h"
#include "hud.h"
#include "inventory_menu.h"
#include "item_icons.h"
#include "lighting.h"
#include "map_screen.h"
#include "recipe.h"
#include "simulation.h"
#include "sprite_renderer.h"
#include "tile_renderer.h"
#include "world_save.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// The client: window, input, menus, saves, sound and drawing around the Simulation, which it advances in fixed
// 60 Hz ticks and whose events it turns into sounds, effects and screens.
class Game {
public:
    // Minimum view in world pixels; the window is scaled up by the largest whole factor that still fits it.
    static constexpr int kViewWidth = 240;
    static constexpr int kViewHeight = 135;
    static constexpr SDL_Color kSavedColor{0, 255, 0, 255};
    static constexpr SDL_Color kErrorColor{255, 0, 0, 255};
    static constexpr float kTick = 1.0f / Simulation::kTicksPerSecond;
    static constexpr float kFadeSeconds = 0.5f;  // the black fade after taking the stairs
    static constexpr float kSleepFadeSeconds = 1.5f;
    static constexpr float kNoteSeconds = 2.5f;

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
        inventoryMenu_.setAudio(&audio_);
        craftingMenu_.setAudio(&audio_);
        containerMenu_.setAudio(&audio_);
        menu_.setSoundSettings(audio_.muted(), audio_.volume());
        if (!tiles_.load(renderer, sprites + "tiles.png") || !sprites_.load(renderer, sprites) ||
            !effects_.load(renderer, sprites + "smash.png") || !hud_.load(renderer, sprites + "hud.png") ||
            !font_.load(renderer, sprites + "font.png") || !itemIcons_.load(renderer, sprites + "items.png") ||
            !inventoryMenu_.load(renderer, sprites + "inventory_counter.png") ||
            !menu_.load(renderer, sprites + "title.png")) {
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

    Level& level() { return sim_.level(); }
    const Level& level() const { return sim_.level(); }

    // The client's side of a fresh session: effects, screens and timers.
    void resetView() {
        effects_.clear();
        closeMenus();
        fadeTimer_ = 0.0f;
        notes_.clear();
        tickAccumulator_ = 0.0f;
        attackPressed_ = false;
    }

    void enterWorld(std::string name) {
        worldName_ = std::move(name);
        inWorld_ = true;
        menu_.close();
    }

    void createWorld(std::string name, std::uint32_t seed) {
        sim_.startNewWorld(seed);
        resetView();
        enterWorld(std::move(name));
        saveWorld();  // so the new world is listed under Load World right away
    }

    bool loadWorld(std::string name) {
        auto data = saves_.load(name);
        if (!data) return false;
        resetView();
        sim_.resetSession(data->seed ^ static_cast<std::uint64_t>(data->secondsPlayed));
        World& world = sim_.world();
        if (data->levels.size() == World::kLevelCount) {
            for (int i = 0; i < World::kLevelCount; ++i) {
                auto& saved = data->levels[static_cast<std::size_t>(i)];
                world.restoreLevel(i, data->seed, std::move(saved.tiles), std::move(saved.data));
                for (auto& piece : saved.furniture) {
                    Furniture::Piece restored{piece.type, piece.x, piece.y, piece.deathChest};
                    for (const auto& stack : piece.contents) restored.contents.add(stack);
                    world.level(i).furniture.restore(std::move(restored));
                }
            }
            world.airWizardBeaten = data->airWizardBeaten;
            world.linkStairs();  // older saves have stairs down walled in by rock
        } else {
            // A save from before the caves: generate the other levels and fit the stairs into the saved surface.
            world.generate(data->seed, sim_.rng());
            world.restoreLevel(World::kSurfaceIndex, data->seed, std::move(data->levels[0].tiles),
                               std::move(data->levels[0].data));
            world.linkStairs();
            sim_.inventory().add(ItemType::PowerGlove);
        }
        world.setCurrent(data->currentLevel);
        world.spawnBoss(sim_.rng());
        Player& player = sim_.player();
        player.setPosition(data->playerX, data->playerY);
        player.restoreStats(data->health, data->energy, data->hunger, data->armor, data->armorPoints);
        sim_.dayNight().restore(data->dayTick, data->pastDay1);
        for (const auto& stack : data->inventory) sim_.inventory().add(stack);
        sim_.setSpawn(data->spawnLevel, {data->spawnX, data->spawnY});
        sim_.setTicksPlayed(data->secondsPlayed * Simulation::kTicksPerSecond);
        sim_.setOnStairs(true);  // don't take the stairs straight away if the save was made on them
        enterWorld(std::move(name));
        return true;
    }

    bool saveWorld() const {
        const World& world = sim_.world();
        const Player& player = sim_.player();
        const Rect bounds = player.bounds();
        WorldSaveData data;
        data.seed = world.seed();
        data.width = World::kSize;
        data.height = World::kSize;
        for (int i = 0; i < World::kLevelCount; ++i) {
            const Level& source = world.level(i);
            WorldSaveData::Level saved{source.map.tiles(), source.map.data(), {}};
            for (const auto& piece : source.furniture.all()) {
                saved.furniture.push_back({piece.type, piece.x, piece.y, piece.deathChest, piece.contents.stacks()});
            }
            data.levels.push_back(std::move(saved));
        }
        data.currentLevel = world.currentIndex();
        data.playerX = bounds.x;
        data.playerY = bounds.y;
        data.health = std::max(1, player.health());
        data.energy = player.energy();
        data.hunger = player.hunger();
        data.armor = player.armor();
        data.armorPoints = player.armorPoints();
        data.spawnLevel = sim_.spawnLevel();
        data.spawnX = sim_.spawnPoint().x;
        data.spawnY = sim_.spawnPoint().y;
        data.dayTick = sim_.dayNight().tick();
        data.pastDay1 = sim_.dayNight().pastDay1();
        data.airWizardBeaten = world.airWizardBeaten;
        data.secondsPlayed = sim_.secondsPlayed();
        // The item in hand isn't in the inventory; save it as part of it so it isn't lost.
        Inventory carried = sim_.inventory();
        if (const auto& held = player.heldItem()) carried.add(*held);
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
            case Kind::Resume:
                menu_.close();
                audio_.play(Sound::Craft);
                break;
            case Kind::Save:
                if (saveWorld()) menu_.showMessage("World saved!", kSavedColor);
                else menu_.showMessage("Could not save!", kErrorColor);
                break;
            case Kind::SaveAndQuit:
                if (sim_.player().isDead()) respawn();  // never save a dead player
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

    void respawn() {
        sim_.respawn();
        playEvents();
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

    // Turns what the simulation recorded into sounds, effects, notes, fades and screens.
    void playEvents() {
        for (const GameEvent& event : sim_.events().take()) {
            switch (event.kind) {
                case GameEvent::Kind::Sound: audio_.play(event.sound); break;
                case GameEvent::Kind::Smash: effects_.addSmash(event.tileX, event.tileY); break;
                case GameEvent::Kind::Number: effects_.addDamageNumber(event.value, event.x, event.y, event.style); break;
                case GameEvent::Kind::Notification: notify(event.text); break;
                case GameEvent::Kind::LevelChanged:
                    effects_.clear();
                    fadeTimer_ = kFadeSeconds;
                    fadeDuration_ = kFadeSeconds;
                    break;
                case GameEvent::Kind::Slept:
                    fadeTimer_ = kSleepFadeSeconds;
                    fadeDuration_ = kSleepFadeSeconds;
                    break;
                case GameEvent::Kind::PlayerDied:
                    closeMenus();
                    menu_.openDeath(event.value);
                    break;
                case GameEvent::Kind::BossDefeated:
                    closeMenus();
                    menu_.openWon(sim_.secondsPlayed());
                    break;
            }
        }
    }

    // Closing the inventory without choosing another item puts the item that was in hand back in hand.
    void reequipStowedItem() {
        const auto stowed = std::exchange(stowedHeld_, std::nullopt);
        if (!stowed || sim_.player().heldItem()) return;
        const auto& stacks = sim_.inventory().stacks();
        if (stowed->slot < static_cast<int>(stacks.size()) &&
            stacks[static_cast<std::size_t>(stowed->slot)].type == stowed->type) {
            sim_.holdSlot(stowed->slot);
        }
    }

    bool menuOpen() const {
        return inventoryMenu_.isOpen() || craftingMenu_.isOpen() || containerMenu_.isOpen() || mapScreen_.isOpen();
    }

    void closeMenus() {
        inventoryMenu_.close();
        craftingMenu_.close();
        containerMenu_.close();
        mapScreen_.close();
        stowedHeld_.reset();
    }

    // The player leaving a screen: Minicraft plays the craft sound whenever a display exits (Game.exitDisplay).
    void exitMenus() {
        if (!menuOpen()) return;
        if (inventoryMenu_.isOpen()) reequipStowedItem();
        closeMenus();
        audio_.play(Sound::Craft);
    }

    // E while facing furniture uses it (Furniture.use): a crafting station opens its recipes, a chest its
    // contents, a bed lets the player sleep. Returns false if there's no furniture to use there.
    bool useFurniture() {
        const Simulation::FurnitureUse use = sim_.useFurniture();
        using Kind = Simulation::FurnitureUse::Kind;
        switch (use.kind) {
            case Kind::None: return false;
            case Kind::Chest: containerMenu_.open(use.tile.x, use.tile.y, use.deathChest ? "Death Chest" : "Chest"); break;
            case Kind::Station: craftingMenu_.open(Recipe::stationRecipes(use.station), itemName(use.station)); break;
            case Kind::Slept:
            case Kind::CantSleep: break;
        }
        playEvents();
        return true;
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
            if (menuOpen()) exitMenus();
            else menu_.openPause();
            return;
        }
        if (mapScreen_.isOpen()) {
            if (key == MapScreen::kToggleKey) exitMenus();
            return;
        }
        if (key == MapScreen::kToggleKey && !menuOpen()) {
            mapScreen_.open(renderer_.get(), level());
            audio_.play(Sound::Select);
            return;
        }
        if (containerMenu_.isOpen()) {
            if (key == InventoryMenu::kToggleKey) {
                exitMenus();
                return;
            }
            const SDL_Point tile = containerMenu_.chestTile();
            const Furniture::Piece* chest = level().furniture.at(tile.x, tile.y);
            if (!chest) {
                containerMenu_.close();
            } else if (const auto move = containerMenu_.handleKey(key, chest->contents, sim_.inventory())) {
                if (!sim_.transfer({tile.x, tile.y}, move->fromChest, move->index)) containerMenu_.close();
                playEvents();
            }
            return;
        }
        if (craftingMenu_.isOpen()) {
            if (key == InventoryMenu::kToggleKey || key == CraftingMenu::kToggleKey) {
                exitMenus();
            } else if (const Recipe* recipe = craftingMenu_.handleKey(key, sim_.inventory())) {
                sim_.craft(*recipe);
                playEvents();
            }
            return;
        }
        if (inventoryMenu_.isOpen()) {
            if (key == InventoryMenu::kToggleKey) {
                exitMenus();
                return;
            }
            // Selecting a slot puts that whole stack in the player's hand and closes the inventory.
            if (const auto slot = inventoryMenu_.handleKey(key, sim_.inventory())) {
                stowedHeld_.reset();
                sim_.holdSlot(*slot);
                exitMenus();
            }
            return;
        }
        if (key == InventoryMenu::kToggleKey) {
            if (useFurniture()) return;
            // The held item shows in the inventory while it's open, and goes back in hand when it closes.
            const auto& held = sim_.player().heldItem();
            const ItemType heldType = held ? held->type : ItemType{};
            if (const auto slot = sim_.stowHeldItem()) stowedHeld_ = StowedItem{*slot, heldType};
            inventoryMenu_.toggle();
            return;
        }
        if (key == CraftingMenu::kToggleKey) {
            sim_.stowHeldItem();
            craftingMenu_.open(Recipe::personalRecipes(), "Crafting");
            return;
        }
        if (key == SDLK_SPACE) attackPressed_ = true;  // acts on the next tick
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

    // The keyboard as the simulation's input. While a menu is open or typing in the debug panel, keys don't move
    // the player.
    PlayerInput readInput() const {
        PlayerInput input;
        if (ImGui::GetIO().WantCaptureKeyboard || menuOpen()) return input;
        const bool* keys = SDL_GetKeyboardState(nullptr);
        if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP]) input.moveY -= 1;
        if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN]) input.moveY += 1;
        if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT]) input.moveX -= 1;
        if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) input.moveX += 1;
        input.attack = keys[SDL_SCANCODE_SPACE];
        input.attackPressed = attackPressed_;
        return input;
    }

    // How dark the current level is: the time of day on the surface, pitch black underground, bright in the sky.
    float darkness() const {
        if (debug_.fullBright() || level().isSky()) return 0.0f;
        if (level().isUnderground()) return 1.0f;
        return sim_.dayNight().darkness();
    }

    void updateWorld(float dt) {
        fadeTimer_ = std::max(0.0f, fadeTimer_ - dt);
        for (auto& note : notes_) note.second -= dt;
        std::erase_if(notes_, [](const auto& note) { return note.second <= 0.0f; });

        // The simulation runs in fixed 60 Hz ticks, however fast the screen refreshes.
        tickAccumulator_ += dt;
        while (tickAccumulator_ >= kTick && !menu_.isOpen()) {
            tickAccumulator_ -= kTick;
            sim_.tick(readInput());
            attackPressed_ = false;
            playEvents();
        }
        effects_.update(dt);
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
        const Rect bounds = sim_.player().bounds();
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
        if (inWorld_) debug_.drawWorldOverlay(renderer, camera_, scale_, level().map, sim_.player(), level().mobs);

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
        const Player& player = sim_.player();
        tiles_.draw(renderer, camera_, here.map, time_);
        sprites_.drawDrops(renderer, camera_, here.drops, itemIcons_);
        const float playerY = player.center().y;
        sprites_.drawFurniture(renderer, camera_, here.furniture, playerY, true);
        sprites_.drawMobs(renderer, camera_, here.mobs, playerY, true);
        sprites_.drawPlayer(renderer, camera_, player);
        if (player.isCarryingFurniture()) {
            const Vec2 carried = player.carriedFurniturePosition();
            sprites_.drawFurnitureSprite(renderer, camera_, player.heldItem()->type, carried.x, carried.y);
        }
        sprites_.drawFurniture(renderer, camera_, here.furniture, playerY, false);
        sprites_.drawMobs(renderer, camera_, here.mobs, playerY, false);
        sprites_.drawProjectiles(renderer, camera_, here.projectiles);
        effects_.draw(renderer, camera_, font_);

        // Darkness (night on the surface, always underground) with circles of light around the player, lanterns,
        // torches and lava (Minicraft's LightOverlay).
        lighting_.draw(renderer, camera_, darkness(), sim_.lights());

        // UI in view pixels (same scale, not moved by the camera).
        const float viewWidth = std::floor(camera_.width());
        const float viewHeight = std::floor(camera_.height());
        const Inventory& inventory = sim_.inventory();
        hud_.drawStatus(renderer, itemIcons_, player, viewWidth, viewHeight);
        if (const auto& held = player.heldItem()) {
            hud_.drawHeldItem(renderer, font_, itemIcons_, *held, viewHeight);
            if (isTool(held->type)) hud_.drawToolDurability(renderer, font_, *held, viewWidth, viewHeight);
            if (toolInfo(held->type).type == ToolType::Bow) {
                hud_.drawArrowCount(renderer, font_, itemIcons_, inventory.count(ItemType::Arrow), viewHeight);
            }
        }
        if (const Mob* boss = here.mobs.boss()) {
            hud_.drawBossBar(renderer, font_, boss->health() * 100 / AirWizard::kMaxHealth, "Air Wizard", viewWidth);
        }
        drawNotes(renderer, viewWidth);
        inventoryMenu_.draw(renderer, hud_, font_, itemIcons_, inventory, viewHeight);
        craftingMenu_.draw(renderer, hud_, font_, itemIcons_, inventory, viewHeight);
        if (containerMenu_.isOpen()) {
            const SDL_Point tile = containerMenu_.chestTile();
            if (const Furniture::Piece* chest = here.furniture.at(tile.x, tile.y)) {
                containerMenu_.draw(renderer, hud_, font_, itemIcons_, chest->contents, inventory, viewHeight);
            }
        }

        mapScreen_.draw(renderer, hud_, font_, here, player, viewWidth, viewHeight);

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
        const auto actions = debug_.drawPanel(camera_, scale_, here.map, sim_.player(), sim_.inventory(),
                                              here.drops.size(), here.mobs, sim_.dayNight(), here.name(),
                                              sim_.world().currentIndex());
        if (actions.regenerateSeed) {
            sim_.regenerate(*actions.regenerateSeed);
            effects_.clear();
            closeMenus();
        }
        if (actions.refillStats) sim_.player().refillStats();
        if (actions.clearInventory) sim_.inventory().clear();
        for (const Inventory::Stack& stack : actions.giveItems) sim_.giveItems(stack.type, stack.count);
        if (actions.spawnMob) {
            const Vec2 p = sim_.player().center();
            here.mobs.spawnNear(actions.spawnMob->first, actions.spawnMob->second, here.map, here.furniture.hitboxes(),
                                p.x, p.y, 3, 6, sim_.rng());
        }
        if (actions.clearMobs) here.mobs.clear();
        if (actions.setTime) sim_.dayNight().setTime(*actions.setTime);
        if (actions.gotoLevel) {
            sim_.changeLevel(*actions.gotoLevel, false);
            // Clear a little room if the player landed inside rock (or over the edge of the sky).
            TileMap& map = level().map;
            const Vec2 c = sim_.player().center();
            const int tx = collision::tileIndex(c.x);
            const int ty = collision::tileIndex(c.y);
            for (int y = ty - 1; y <= ty + 1; ++y) {
                for (int x = tx - 1; x <= tx + 1; ++x) {
                    if (map.inBounds(x, y) && map.blocksMobsAt(x, y)) {
                        map.setTile(x, y, level().isSky() ? Tile::Cloud : Tile::Dirt);
                    }
                }
            }
            playEvents();
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
    Simulation sim_;
    TileRenderer tiles_;
    SpriteRenderer sprites_;
    Effects effects_;
    Lighting lighting_;
    InventoryMenu inventoryMenu_;
    CraftingMenu craftingMenu_;
    ContainerMenu containerMenu_;
    MapScreen mapScreen_;
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
    float time_ = 0.0f;
    float tickAccumulator_ = 0.0f;
    bool attackPressed_ = false;  // Space went down since the last tick
    float fadeTimer_ = 0.0f;
    float fadeDuration_ = kFadeSeconds;
    // Where the held item went when the inventory opened, to put it back in hand when the inventory closes.
    struct StowedItem {
        int slot;
        ItemType type;
    };
    std::optional<StowedItem> stowedHeld_;
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
