#include "audio.h"
#include "camera.h"
#include "chat_box.h"
#include "collision.h"
#include "container_menu.h"
#include "crafting_menu.h"
#include "debug_overlay.h"
#include "effects.h"
#include "enet_util.h"
#include "font.h"
#include "game_menu.h"
#include "hud.h"
#include "inventory_menu.h"
#include "item_icons.h"
#include "lighting.h"
#include "map_screen.h"
#include "network_client.h"
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
#include <chrono>
#include <cmath>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// The client: window, input, menus, saves, sound and drawing around the Simulation.
//
// Single-player and multiplayer run the game the same way: the world only changes through ticks (TickInput), each
// holding what every player did. Offline, the game makes a tick itself 60 times a second from the keyboard.
// Online, the server makes them from everyone's keys and sends them; the game sends its keys and commands to the
// server and simulates the ticks as they arrive. Either way, what the simulation reports (events) becomes sounds,
// effects and screens.
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
    static constexpr std::size_t kCatchUpTicks = 30;  // this far behind the server (just joined): fast-forward

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
        if (!enet_.ok()) SDL_Log("ENet failed to start: multiplayer won't work");
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
        net_.disconnect();
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

    // This client's player, or null while joining a multiplayer world (before the Join tick arrives).
    Player* me() { return sim_.findPlayer(localId_); }
    const Player* me() const { return sim_.findPlayer(localId_); }

    // Something the player did once (from a menu). Offline it goes into the next tick; online, to the server,
    // which puts it into the next tick for everyone.
    void send(const PlayerCommand& command) {
        if (online_) net_.sendCommand(command);
        else localCommands_.push_back(command);
    }

    // The client's side of a fresh world: effects, screens and timers.
    void resetView() {
        effects_.clear();
        closeMenus();
        chat_.clear();
        fadeTimer_ = 0.0f;
        notes_.clear();
        tickAccumulator_ = 0.0f;
        attackPressed_ = false;
        localCommands_.clear();
        lastSentInput_ = {};
    }

    // ---------------------------------------------------------------------------------------------------------
    // Single-player worlds (saved to files)

    void createWorld(std::string name, std::uint32_t seed) {
        resetView();
        sim_.startNewWorld(seed);
        sim_.singlePlayer = true;
        localId_ = 0;
        sim_.addPlayer(localId_, "");
        enterWorld(std::move(name), false);
        saveWorld();  // so the new world is listed under Load World right away
    }

    bool loadWorld(std::string name) {
        auto data = saves_.load(name);
        if (!data) return false;
        resetView();
        sim_.resetSession(data->seed ^ static_cast<std::uint64_t>(data->secondsPlayed));
        sim_.singlePlayer = true;
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
        }
        localId_ = 0;
        Player& player = sim_.addPlayer(localId_, "");
        // Older saves had no power glove: keep the one a new player gets. Newer ones carry their own.
        if (data->levels.size() == World::kLevelCount) player.inventory().clear();
        for (const auto& stack : data->inventory) player.inventory().add(stack);
        player.setLevel(data->currentLevel);
        player.setPosition(data->playerX, data->playerY);
        player.restoreStats(data->health, data->energy, data->hunger, data->armor, data->armorPoints);
        player.setSpawn(data->spawnLevel, {data->spawnX, data->spawnY});
        player.setOnStairs(true);  // don't take the stairs straight away if the save was made on them
        world.spawnBoss(sim_.rng());
        sim_.dayNight().restore(data->dayTick, data->pastDay1);
        sim_.setTickCount(data->secondsPlayed * Simulation::kTicksPerSecond);
        enterWorld(std::move(name), false);
        return true;
    }

    bool saveWorld() {
        Player* player = me();
        if (online_ || !player) return false;
        // Never save a dead player: respawn them first.
        if (player->waitingToRespawn()) sim_.tick({sim_.tickCount() + 1, {{localId_, {}, {PlayerCommand::respawn()}}}});
        const World& world = sim_.world();
        const Rect bounds = player->bounds();
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
        data.currentLevel = player->level();
        data.playerX = bounds.x;
        data.playerY = bounds.y;
        data.health = std::max(1, player->health());
        data.energy = player->energy();
        data.hunger = player->hunger();
        data.armor = player->armor();
        data.armorPoints = player->armorPoints();
        data.spawnLevel = player->spawnLevel();
        data.spawnX = player->spawnPoint().x;
        data.spawnY = player->spawnPoint().y;
        data.dayTick = sim_.dayNight().tick();
        data.pastDay1 = sim_.dayNight().pastDay1();
        data.airWizardBeaten = world.airWizardBeaten;
        data.secondsPlayed = sim_.secondsPlayed();
        // The item in hand isn't in the inventory; save it as part of it so it isn't lost.
        Inventory carried = player->inventory();
        if (const auto& held = player->heldItem()) carried.add(*held);
        data.inventory = carried.stacks();
        return saves_.save(worldName_, data);
    }

    void enterWorld(std::string name, bool online) {
        worldName_ = std::move(name);
        inWorld_ = true;
        online_ = online;
        menu_.setOnline(online);
        menu_.close();
    }

    void leaveWorld() {
        inWorld_ = false;
        online_ = false;
        menu_.setOnline(false);
        resetView();
    }

    // ---------------------------------------------------------------------------------------------------------
    // Multiplayer

    // What the server sent since the last frame: the lobby list, entering a world, chat, errors, a lost connection.
    void pollNetwork() {
        net_.poll();
        if (net_.takeConnectionLost()) {
            const bool wasPlaying = inWorld_ && online_;
            if (wasPlaying) leaveWorld();
            menu_.openConnect();
            menu_.showMessage(wasPlaying ? "Lost connection to the server" : "Could not connect", kErrorColor);
            return;
        }
        if (auto lobbies = net_.takeLobbies()) {
            std::vector<GameMenu::LobbyEntry> entries;
            for (const auto& lobby : *lobbies) {
                entries.push_back({lobby.id, lobby.name + " (" + std::to_string(lobby.players) + ")"});
            }
            menu_.setLobbies(std::move(entries));
            if (!inWorld_ && menu_.screen() == GameMenu::Screen::Connecting) menu_.openLobbies();
        }
        if (auto joined = net_.takeJoined()) {
            // A new world: generated from the lobby's seed, then the history replayed to catch up with everyone.
            resetView();
            sim_.startNewWorld(joined->seed);
            sim_.singlePlayer = false;
            localId_ = net_.playerId();
            auto& ticks = net_.ticks();
            ticks.insert(ticks.begin(), joined->history.begin(), joined->history.end());
            enterWorld(joined->lobbyName, true);
        }
        for (const auto& line : net_.takeChat()) chat_.add(line.from, line.text);
        if (auto error = net_.takeError()) menu_.showMessage(*error, kErrorColor);
    }

    // Simulates the ticks the server sent. Just after joining there's the whole history to replay: then it
    // fast-forwards (as many ticks as fit in a few milliseconds per frame, without their sounds and effects).
    void updateOnline() {
        // The keys go to the server whenever they change; a fresh press always goes.
        const PlayerInput input = readInput();
        const bool changed = input.moveX != lastSentInput_.moveX || input.moveY != lastSentInput_.moveY ||
                             input.attack != lastSentInput_.attack;
        if (changed || input.attackPressed) {
            net_.sendInput(input);
            lastSentInput_ = input;
        }
        attackPressed_ = false;

        auto& ticks = net_.ticks();
        const bool catchingUp = ticks.size() > kCatchUpTicks;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(12);
        while (!ticks.empty() && (!catchingUp || std::chrono::steady_clock::now() < deadline)) {
            sim_.tick(ticks.front());
            ticks.pop_front();
            if (catchingUp) {
                sim_.events().take();  // old news
                continue;
            }
            playEvents();
            // Once a second, the world's fingerprint goes to the server, which compares everyone's.
            if (sim_.tickCount() % Simulation::kTicksPerSecond == 0) {
                net_.sendStateHash(sim_.tickCount(), sim_.stateHash());
            }
        }
    }

    // ---------------------------------------------------------------------------------------------------------
    // Playing

    // Offline: a tick every 1/60 s from this player's keys and commands.
    void updateOffline(float dt) {
        tickAccumulator_ += dt;
        while (tickAccumulator_ >= kTick) {
            tickAccumulator_ -= kTick;
            sim_.tick({sim_.tickCount() + 1, {{localId_, readInput(), std::exchange(localCommands_, {})}}});
            attackPressed_ = false;
            playEvents();
        }
    }

    // The keyboard as the simulation's input. While a menu or the chat is open, or typing in the debug panel, keys
    // don't move the player.
    PlayerInput readInput() const {
        PlayerInput input;
        if (ImGui::GetIO().WantCaptureKeyboard || menuOpen() || menu_.isOpen() || chat_.isOpen()) return input;
        const bool* keys = SDL_GetKeyboardState(nullptr);
        if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP]) input.moveY -= 1;
        if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN]) input.moveY += 1;
        if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT]) input.moveX -= 1;
        if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) input.moveX += 1;
        input.attack = keys[SDL_SCANCODE_SPACE];
        input.attackPressed = attackPressed_;
        return input;
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

    // Turns what the simulation recorded into sounds, effects, notes, fades and screens. Only what concerns this
    // player: things happening on their level, and the personal events addressed to them.
    void playEvents() {
        const Player* player = me();
        const int myLevel = player ? player->level() : -1;
        for (const GameEvent& event : sim_.events().take()) {
            const bool forSomeoneElse = event.player != -1 && event.player != localId_;
            if (forSomeoneElse) {
                if (event.kind == GameEvent::Kind::PlayerDied) {
                    if (const Player* other = sim_.findPlayer(event.player)) notify(other->name() + " died");
                }
                continue;
            }
            if (event.level != myLevel && event.kind != GameEvent::Kind::BossDefeated) continue;
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

    // ---------------------------------------------------------------------------------------------------------
    // Menus

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
                if (!saveWorld()) {
                    menu_.showMessage("Could not save!", kErrorColor);
                    break;
                }
                leaveWorld();
                menu_.openTitle(saves_.list());
                break;
            case Kind::Respawn:
                send(PlayerCommand::respawn());
                menu_.close();
                break;
            case Kind::Connect:
                if (net_.connect(action.address, action.playerName)) menu_.openConnecting();
                else menu_.showMessage("Unknown server address", kErrorColor);
                break;
            case Kind::Disconnect:
                net_.disconnect();
                menu_.openTitle(saves_.list());
                break;
            case Kind::CreateLobby: net_.createLobby(); break;
            case Kind::JoinLobby: net_.joinLobby(action.lobbyId); break;
            case Kind::LeaveGame:
                net_.leaveLobby();
                leaveWorld();
                menu_.openLobbies();  // the server sends the lobby list again
                break;
            case Kind::ToggleSound: audio_.toggleMuted(); break;
            case Kind::VolumeDown: audio_.setVolume(audio_.volume() - 1); break;
            case Kind::VolumeUp: audio_.setVolume(audio_.volume() + 1); break;
            case Kind::Quit: running_ = false; break;
            case Kind::None: break;
        }
        menu_.setSoundSettings(audio_.muted(), audio_.volume());
    }

    bool menuOpen() const {
        return inventoryMenu_.isOpen() || craftingMenu_.isOpen() || containerMenu_.isOpen() || mapScreen_.isOpen();
    }

    void closeMenus() {
        inventoryMenu_.close();
        craftingMenu_.close();
        containerMenu_.close();
        mapScreen_.close();
    }

    // The player leaving a screen: Minicraft plays the craft sound whenever a display exits (Game.exitDisplay).
    // Closing the inventory without choosing an item puts the item that was in hand back in hand.
    void exitMenus() {
        if (!menuOpen()) return;
        if (inventoryMenu_.isOpen()) send(PlayerCommand::reequipHeld());
        closeMenus();
        audio_.play(Sound::Craft);
    }

    // E while facing furniture uses it (Furniture.use): a crafting station opens its recipes, a chest its
    // contents, a bed lets the player sleep. The menu opens right away; what changes in the world (the held item
    // put away, the night skipped) happens in the next tick. Returns false if there's no furniture to use.
    bool useFurniture() {
        const Player* player = me();
        if (!player) return false;
        const Point target = player->interactionTile();
        const Furniture::Piece* piece = sim_.levelOf(*player).furniture.at(target.x, target.y);
        if (!piece) return false;
        if (piece->isContainer()) {
            containerMenu_.open(target.x, target.y, piece->deathChest ? "Death Chest" : "Chest");
        } else if (piece->type != ItemType::Bed) {
            std::vector<Recipe> recipes = Recipe::stationRecipes(piece->type);
            if (recipes.empty()) return false;  // a lantern: nothing to use
            craftingStation_ = static_cast<int>(piece->type);
            craftingMenu_.open(std::move(recipes), itemName(piece->type));
        }
        send(PlayerCommand::useFurniture());
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
                if (chat_.isOpen()) chat_.handleText(event.text.text);
                else menu_.handleText(event.text.text);
            }
        }
    }

    void handleKey(SDL_Keycode key, bool repeat) {
        if (chat_.isOpen()) {
            if (const auto line = chat_.handleKey(key)) net_.sendChat(*line);
            return;
        }
        if (menu_.isOpen()) {
            if (!ImGui::GetIO().WantCaptureKeyboard) handleMenuAction(menu_.handleKey(key, repeat));
            return;
        }
        if (repeat) return;
        if (key == DebugOverlay::kToggleKey) debug_.toggle();
        if (ImGui::GetIO().WantCaptureKeyboard) return;  // typing in the debug panel
        const Player* player = me();
        if (!player) return;  // still joining
        if (key == SDLK_M) {
            audio_.toggleMuted();
            menu_.setSoundSettings(audio_.muted(), audio_.volume());
            notify(audio_.muted() ? "Sound off" : "Sound on");
            return;
        }
        if (key == SDLK_ESCAPE) {
            if (menuOpen()) exitMenus();
            else menu_.openPause();  // online, the world keeps going behind it
            return;
        }
        if (online_ && key == ChatBox::kOpenKey && !menuOpen()) {
            chat_.open();
            return;
        }
        if (mapScreen_.isOpen()) {
            if (key == MapScreen::kToggleKey) exitMenus();
            return;
        }
        if (key == MapScreen::kToggleKey && !menuOpen()) {
            mapScreen_.open(renderer_.get(), sim_.levelOf(*player));
            audio_.play(Sound::Select);
            return;
        }
        if (containerMenu_.isOpen()) {
            if (key == InventoryMenu::kToggleKey) {
                exitMenus();
                return;
            }
            const SDL_Point tile = containerMenu_.chestTile();
            const Furniture::Piece* chest = sim_.levelOf(*player).furniture.at(tile.x, tile.y);
            if (!chest) {
                containerMenu_.close();
            } else if (const auto move = containerMenu_.handleKey(key, chest->contents, player->inventory())) {
                send(PlayerCommand::transfer(tile.x, tile.y, move->fromChest, move->index));
            }
            return;
        }
        if (craftingMenu_.isOpen()) {
            if (key == InventoryMenu::kToggleKey || key == CraftingMenu::kToggleKey) {
                exitMenus();
            } else if (craftingMenu_.handleKey(key, player->inventory())) {
                send(PlayerCommand::craft(craftingStation_, craftingMenu_.selectedIndex()));
            }
            return;
        }
        if (inventoryMenu_.isOpen()) {
            if (key == InventoryMenu::kToggleKey) {
                exitMenus();
                return;
            }
            // Selecting a slot puts that whole stack in the player's hand and closes the inventory.
            if (const auto slot = inventoryMenu_.handleKey(key, player->inventory())) {
                send(PlayerCommand::holdSlot(*slot));
                closeMenus();
                audio_.play(Sound::Craft);
            }
            return;
        }
        if (key == InventoryMenu::kToggleKey) {
            if (useFurniture()) return;
            // The held item shows in the inventory while it's open, and goes back in hand when it closes.
            send(PlayerCommand::stowHeld());
            inventoryMenu_.toggle();
            return;
        }
        if (key == CraftingMenu::kToggleKey) {
            send(PlayerCommand::stowHeld());
            craftingStation_ = -1;  // by hand
            craftingMenu_.open(Recipe::personalRecipes(), "Crafting");
            return;
        }
        if (key == SDLK_SPACE) attackPressed_ = true;  // acts on the next tick
    }

    void update(float dt) {
        menu_.update(dt);
        chat_.update(dt);
        pollNetwork();
        // SDL text input (and the IME) is only on while a text field or the chat is in use.
        SDL_Window* window = window_.get();
        const bool typing = menu_.wantsTextInput() || chat_.isOpen();
        if (typing != SDL_TextInputActive(window)) {
            if (typing) SDL_StartTextInput(window);
            else SDL_StopTextInput(window);
        }
        // Offline the world pauses behind any menu; online it never stops (the others keep playing).
        if (inWorld_ && (online_ || !menu_.isOpen())) {
            fadeTimer_ = std::max(0.0f, fadeTimer_ - dt);
            for (auto& note : notes_) note.second -= dt;
            std::erase_if(notes_, [](const auto& note) { return note.second <= 0.0f; });
            if (online_) updateOnline();
            else updateOffline(dt);
            effects_.update(dt);
        }
        updateView();
    }

    // How dark a level is: the time of day on the surface, pitch black underground, bright in the sky.
    float darkness(const Level& level) const {
        if (debug_.fullBright() || level.isSky()) return 0.0f;
        if (level.isUnderground()) return 1.0f;
        return sim_.dayNight().darkness();
    }

    // Picks the render scale for the window size and points the camera at the player.
    void updateView() {
        int outputWidth = 0;
        int outputHeight = 0;
        SDL_GetCurrentRenderOutputSize(renderer_.get(), &outputWidth, &outputHeight);
        scale_ = static_cast<float>(std::max(1, std::min(outputWidth / kViewWidth, outputHeight / kViewHeight)));
        camera_.setView(static_cast<float>(outputWidth) / scale_, static_cast<float>(outputHeight) / scale_, scale_);
        const Player* player = me();
        if (!inWorld_ || !player) return;
        // Follow the position the player is actually drawn at (snapped), so the two never disagree by a pixel.
        const Rect bounds = player->bounds();
        const Level& level = sim_.levelOf(*player);
        camera_.follow(camera_.snap(bounds.x) + bounds.w / 2.0f, camera_.snap(bounds.y) + bounds.h / 2.0f,
                       level.map.pixelWidth(), level.map.pixelHeight());
    }

    // ---------------------------------------------------------------------------------------------------------
    // Drawing

    void draw() {
        SDL_Renderer* renderer = renderer_.get();
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        // World: drawn in world pixels, scaled up by a whole factor.
        SDL_SetRenderScale(renderer, scale_, scale_);
        if (inWorld_) {
            if (me()) drawWorld(renderer);
            else drawJoining(renderer);
        }
        menu_.draw(renderer, hud_, font_, camera_.width(), camera_.height());

        // Debug overlay and UI: drawn in screen pixels so lines stay thin.
        SDL_SetRenderScale(renderer, 1.0f, 1.0f);
        if (inWorld_ && me()) {
            debug_.drawWorldOverlay(renderer, camera_, scale_, sim_.levelOf(*me()).map, *me(), sim_.levelOf(*me()).mobs);
        }

        // ImGui runs every frame (even without the debug panel) so its keyboard capture state stays current.
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        if (inWorld_ && me()) drawDebugPanel();
        ImGui::Render();
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);

        SDL_RenderPresent(renderer);
    }

    // While joining a multiplayer world: replaying what happened before we arrived.
    void drawJoining(SDL_Renderer* renderer) const {
        const std::string title = "Joining " + worldName_ + "...";
        const std::string behind = std::to_string(net_.ticksWaiting()) + " ticks to catch up";
        const float width = camera_.width();
        const float middle = std::floor(camera_.height() / 2.0f);
        font_.draw(renderer, title, std::floor((width - Font::textWidth(title)) / 2.0f), middle - 8.0f,
                   SDL_Color{255, 255, 255, 255});
        font_.draw(renderer, behind, std::floor((width - Font::textWidth(behind)) / 2.0f), middle + 4.0f,
                   SDL_Color{153, 153, 153, 255});
    }

    void drawWorld(SDL_Renderer* renderer) {
        const Player& player = *me();
        const Level& here = sim_.levelOf(player);
        tiles_.draw(renderer, camera_, here.map, time_);
        sprites_.drawDrops(renderer, camera_, here.drops, itemIcons_);
        const float playerY = player.center().y;
        sprites_.drawFurniture(renderer, camera_, here.furniture, playerY, true);
        sprites_.drawMobs(renderer, camera_, here.mobs, playerY, true);
        // Everyone on this level (other players first, so this player is drawn on top).
        for (const auto& other : sim_.players()) {
            if (other.get() != &player && other->level() == player.level() && !other->waitingToRespawn()) {
                drawPlayer(renderer, *other);
            }
        }
        drawPlayer(renderer, player);
        sprites_.drawFurniture(renderer, camera_, here.furniture, playerY, false);
        sprites_.drawMobs(renderer, camera_, here.mobs, playerY, false);
        sprites_.drawProjectiles(renderer, camera_, here.projectiles);
        effects_.draw(renderer, camera_, font_);

        // Darkness (night on the surface, always underground) with circles of light around the players, lanterns,
        // torches and lava (Minicraft's LightOverlay).
        lighting_.draw(renderer, camera_, darkness(here), sim_.lights(player.level()));

        // Other players' names, above the darkness so they can always be found. Small text (see textScale).
        SDL_SetRenderScale(renderer, textScale(), textScale());
        for (const auto& other : sim_.players()) {
            if (other.get() != &player && other->level() == player.level() && !other->waitingToRespawn()) {
                drawNameTag(renderer, *other);
            }
        }
        SDL_SetRenderScale(renderer, scale_, scale_);

        // UI in view pixels (same scale, not moved by the camera).
        const float viewWidth = std::floor(camera_.width());
        const float viewHeight = std::floor(camera_.height());
        const Inventory& inventory = player.inventory();
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
        drawNotes(renderer, viewWidth, here);
        if (online_) {
            // The chat in small text too, just above the hearts and bolts (the bottom 24 view pixels).
            const float toText = scale_ / textScale();
            SDL_SetRenderScale(renderer, textScale(), textScale());
            chat_.draw(renderer, font_, (viewHeight - 24.0f) * toText);
            SDL_SetRenderScale(renderer, scale_, scale_);
        }
        inventoryMenu_.draw(renderer, hud_, font_, itemIcons_, inventory, viewHeight);
        craftingMenu_.draw(renderer, hud_, font_, itemIcons_, inventory, viewHeight);
        if (containerMenu_.isOpen()) {
            const SDL_Point tile = containerMenu_.chestTile();
            if (const Furniture::Piece* chest = here.furniture.at(tile.x, tile.y)) {
                containerMenu_.draw(renderer, hud_, font_, itemIcons_, chest->contents, inventory, viewHeight);
            } else {
                containerMenu_.close();  // emptied death chest, or someone picked the chest up
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

    void drawPlayer(SDL_Renderer* renderer, const Player& player) {
        sprites_.drawPlayer(renderer, camera_, player);
        if (player.isCarryingFurniture()) {
            const Vec2 carried = player.carriedFurniturePosition();
            sprites_.drawFurnitureSprite(renderer, camera_, player.heldItem()->type, carried.x, carried.y);
        }
    }

    // Names and chat use half the world's pixel size, so they don't cover the game. The 8x8 font is drawn at a
    // whole render scale (at least 1), so its letters stay sharp.
    float textScale() const { return std::max(1.0f, std::floor(scale_ / 2.0f)); }

    // A player's name centred just above their head (higher if they carry furniture over it). Called with the
    // render scale at textScale(): view pixels are multiplied by `toText` to get there.
    void drawNameTag(SDL_Renderer* renderer, const Player& player) const {
        const float toText = scale_ / textScale();
        const Rect bounds = player.bounds();
        const float centerX = (camera_.snap(bounds.x) + bounds.w / 2.0f - camera_.x()) * toText;
        const float top = (camera_.snap(bounds.y) - camera_.y() - (player.isCarryingFurniture() ? 13.0f : 1.0f)) * toText;
        const float x = std::floor(centerX - Font::textWidth(player.name()) / 2.0f);
        const float y = std::floor(top - Font::kGlyphSize - 1.0f);
        font_.drawShadowed(renderer, player.name(), x, y, SDL_Color{255, 255, 255, 255});
    }

    // Minicraft+'s notifications: short lines centred near the top, on a black background.
    void drawNotes(SDL_Renderer* renderer, float viewWidth, const Level& level) const {
        float y = level.mobs.boss() ? 24.0f : 4.0f;
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
        Player& player = *me();
        Level& here = sim_.levelOf(player);
        const auto actions = debug_.drawPanel(camera_, scale_, here.map, player, player.inventory(), here.drops.size(),
                                              here.mobs, sim_.dayNight(), here.name(), player.level());
        // In multiplayer every client must run exactly the same simulation, so the debug panel only looks.
        if (online_) return;
        if (actions.regenerateSeed) {
            sim_.regenerate(*actions.regenerateSeed);
            effects_.clear();
            closeMenus();
        }
        if (actions.refillStats) player.refillStats();
        if (actions.clearInventory) player.inventory().clear();
        for (const Inventory::Stack& stack : actions.giveItems) sim_.giveItems(player, stack.type, stack.count);
        if (actions.spawnMob) {
            const Vec2 p = player.center();
            here.mobs.spawnNear(actions.spawnMob->first, actions.spawnMob->second, here.map, here.furniture.hitboxes(),
                                p.x, p.y, 3, 6, sim_.rng());
        }
        if (actions.clearMobs) here.mobs.clear();
        if (actions.setTime) sim_.dayNight().setTime(*actions.setTime);
        if (actions.gotoLevel) {
            sim_.changeLevel(player, *actions.gotoLevel, false);
            // Clear a little room if the player landed inside rock (or over the edge of the sky).
            Level& level = sim_.levelOf(player);
            const Vec2 c = player.center();
            const int tx = collision::tileIndex(c.x);
            const int ty = collision::tileIndex(c.y);
            for (int y = ty - 1; y <= ty + 1; ++y) {
                for (int x = tx - 1; x <= tx + 1; ++x) {
                    if (level.map.inBounds(x, y) && level.map.blocksMobsAt(x, y)) {
                        level.map.setTile(x, y, level.isSky() ? Tile::Cloud : Tile::Dirt);
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
    EnetLibrary enet_;
    Audio audio_;
    std::unique_ptr<SDL_Window, WindowDeleter> window_;
    std::unique_ptr<SDL_Renderer, RendererDeleter> renderer_;
    ImGuiContextGuard imgui_;
    Simulation sim_;
    NetworkClient net_;
    TileRenderer tiles_;
    SpriteRenderer sprites_;
    Effects effects_;
    Lighting lighting_;
    InventoryMenu inventoryMenu_;
    CraftingMenu craftingMenu_;
    ContainerMenu containerMenu_;
    MapScreen mapScreen_;
    ChatBox chat_;
    Hud hud_;
    Font font_;
    ItemIcons itemIcons_;
    Camera camera_;
    DebugOverlay debug_;
    GameMenu menu_;
    WorldSaves saves_{savesDirectory()};
    std::string worldName_;     // the save's name, or the lobby's
    bool inWorld_ = false;      // a world is loaded (playing or paused); false on the title screens
    bool online_ = false;       // that world is a multiplayer lobby
    int localId_ = 0;           // this client's player in the simulation (0 offline; the server's id online)
    std::vector<PlayerCommand> localCommands_;  // offline: what goes into the next tick
    PlayerInput lastSentInput_;                 // online: the keys the server last heard about
    int craftingStation_ = -1;  // whose recipes the crafting menu shows (an ItemType, -1 = by hand)
    float scale_ = 1.0f;
    float time_ = 0.0f;
    float tickAccumulator_ = 0.0f;
    bool attackPressed_ = false;  // Space went down since the last tick
    float fadeTimer_ = 0.0f;
    float fadeDuration_ = kFadeSeconds;
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
