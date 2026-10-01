#include "camera.h"
#include "crafting_menu.h"
#include "day_night.h"
#include "debug_overlay.h"
#include "dropped_items.h"
#include "effects.h"
#include "font.h"
#include "furniture.h"
#include "game_menu.h"
#include "hud.h"
#include "inventory_menu.h"
#include "items.h"
#include "player.h"
#include "player_actions.h"
#include "tile_map.h"
#include "world_save.h"
#include "mobs.h"

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
    static constexpr int kMapSize = 256;  // tiles (power of two, required by the generator)
    // Holding Space works like Minicraft: the press punches once, and only once the key has been held for a moment
    // (Minicraft waits for the OS key repeat to make the key "sticky") does it unload rapid punches until energy
    // runs out.
    static constexpr float kPunchHoldDelay = 0.5f;
    static constexpr float kRapidPunchInterval = 3.0f / 60.0f;  // 20 punches/s
    static constexpr SDL_Color kPlayerDamageColor{255, 0, 204, 255};  // Minicraft: Color.get(-1, 504)
    static constexpr float kPlayerLightRadius = 40.0f;
    static constexpr SDL_Color kSavedColor{0, 255, 0, 255};
    static constexpr SDL_Color kErrorColor{255, 0, 0, 255};

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
        if (!player_.load(renderer, sprites + "player.png", sprites + "hud.png") ||
            !map_.load(renderer, sprites + "tiles.png") || !effects_.load(renderer, sprites + "smash.png") ||
            !hud_.load(renderer, sprites + "hud.png") || !font_.load(renderer, sprites + "font.png") ||
            !itemIcons_.load(renderer, sprites + "items.png") ||
            !inventoryMenu_.load(renderer, sprites + "inventory_counter.png") ||
            !furniture_.load(renderer, sprites + "furniture.png") ||
            !mobs_.load(renderer, sprites) || !menu_.load(renderer, sprites + "title.png")) {
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

    // Everything that belongs to one play session goes back to a fresh start (the map is set separately).
    void resetWorldState() {
        effects_.clear();
        droppedItems_.clear();
        mobs_.clear();
        furniture_.clear();
        inventory_.clear();
        inventoryMenu_.close();
        craftingMenu_.close();
        workbenchMenu_.close();
        player_.setHeldItem(std::nullopt);
        player_.refillStats();
        dayNight_ = DayNight{};
        punchRepeatTimer_ = 0.0f;
    }

    void enterWorld(std::string name) {
        worldName_ = std::move(name);
        inWorld_ = true;
        menu_.close();
    }

    void createWorld(std::string name, std::uint32_t seed) {
        map_.generate(seed, kMapSize, kMapSize);
        resetWorldState();
        const SDL_FPoint spawn = map_.findSpawnPoint();
        player_.setPosition(spawn.x, spawn.y);
        enterWorld(std::move(name));
        saveWorld();  // so the new world is listed under Load World right away
    }

    bool loadWorld(std::string name) {
        auto data = saves_.load(name);
        if (!data) return false;
        map_.restore(data->seed, data->width, data->height, std::move(data->tiles), std::move(data->damage));
        resetWorldState();
        player_.setPosition(data->playerX, data->playerY);
        player_.restoreStats(data->health, data->energy);
        dayNight_.restore(data->dayTick, data->pastDay1);
        for (const auto& stack : data->inventory) inventory_.add(stack);
        enterWorld(std::move(name));
        return true;
    }

    bool saveWorld() const {
        const SDL_FRect bounds = player_.bounds();
        WorldSaveData data;
        data.seed = map_.seed();
        data.width = map_.width();
        data.height = map_.height();
        data.tiles = map_.tiles();
        data.damage = map_.damage();
        data.playerX = bounds.x;
        data.playerY = bounds.y;
        data.health = player_.health();
        data.energy = player_.energy();
        data.dayTick = dayNight_.tick();
        data.pastDay1 = dayNight_.pastDay1();
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
                if (!saveWorld()) {
                    menu_.showMessage("Could not save!", kErrorColor);
                    break;
                }
                inWorld_ = false;
                menu_.openTitle(saves_.list());
                break;
            case Kind::Quit: running_ = false; break;
            case Kind::None: break;
        }
    }

    void newWorld(std::uint32_t seed) {
        map_.generate(seed, kMapSize, kMapSize);
        effects_.clear();
        droppedItems_.clear();
        mobs_.clear();
        furniture_.clear();
        const SDL_FPoint spawn = map_.findSpawnPoint();
        player_.setPosition(spawn.x, spawn.y);
    }

    // No death screen yet: back to the spawn point with full health and energy; the inventory is kept.
    void respawn() {
        const SDL_FPoint spawn = map_.findSpawnPoint();
        player_.setPosition(spawn.x, spawn.y);
        player_.refillStats();
    }

    PlayerActions actions() { return PlayerActions(map_, player_, mobs_, effects_, droppedItems_); }

    // Space: with furniture in hand, try to place it on the tile in front (FurnitureItem.interactOn); with a tool,
    // use or swing it; with any other item in hand nothing happens (Minicraft items that can't attack don't punch);
    // with an empty hand, punch.
    void useOrPunch() {
        const auto& held = player_.heldItem();
        if (!held) {
            actions().punch();
            return;
        }
        if (isTool(held->type)) {
            actions().swingTool();
            return;
        }
        if (!isFurniture(held->type)) return;
        const SDL_Point target = player_.interactionTile();
        if (furniture_.place(held->type, target.x, target.y, map_, mobs_.hitboxes())) {
            player_.setHeldItem(std::nullopt);
        }
    }

    // Opening a menu puts the held item back in the inventory, or drops it if there's no room (Minicraft's
    // tryAddToInvOrDrop).
    void stowHeldItem() {
        const auto held = player_.heldItem();
        if (!held) return;
        player_.setHeldItem(std::nullopt);
        if (const int leftover = inventory_.add(*held); leftover > 0) {
            const SDL_FPoint middle = player_.center();
            droppedItems_.spawn(held->type, leftover, middle.x, middle.y, held->durability);
        }
    }

    // Products that don't fit in the inventory are dropped at the player's feet, like Minicraft.
    void craft(const Recipe& recipe) {
        const int leftover = recipe.craft(inventory_);
        if (leftover <= 0) return;
        const SDL_FPoint middle = player_.center();
        droppedItems_.spawn(recipe.product(), leftover, middle.x, middle.y);
    }

    bool menuOpen() const { return inventoryMenu_.isOpen() || craftingMenu_.isOpen() || workbenchMenu_.isOpen(); }

    // E while facing a placed workbench opens its recipes (Minicraft: using a Crafter opens its CraftingDisplay).
    bool facingWorkbench() const {
        const SDL_Point target = player_.interactionTile();
        return furniture_.at(target.x, target.y) == ItemType::Workbench;
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
        if (key == SDLK_ESCAPE) {
            if (menuOpen()) {
                inventoryMenu_.close();
                craftingMenu_.close();
                workbenchMenu_.close();
            } else {
                menu_.openPause();
            }
            return;
        }
        if (workbenchMenu_.isOpen()) {
            if (key == InventoryMenu::kToggleKey) {
                workbenchMenu_.close();
            } else if (const Recipe* recipe = workbenchMenu_.handleKey(key, inventory_)) {
                craft(*recipe);
            }
            return;
        }
        if (key == InventoryMenu::kToggleKey && !menuOpen() && facingWorkbench()) {
            stowHeldItem();
            workbenchMenu_.toggle();
            return;
        }
        // Each menu's key opens it or closes it, but doesn't open one menu on top of the other.
        if (key == InventoryMenu::kToggleKey && !craftingMenu_.isOpen()) {
            stowHeldItem();
            inventoryMenu_.toggle();
            return;
        }
        if (key == CraftingMenu::kToggleKey && !inventoryMenu_.isOpen()) {
            stowHeldItem();
            craftingMenu_.toggle();
            return;
        }
        if (inventoryMenu_.isOpen()) {
            // Selecting a slot puts that whole stack in the player's hand and closes the inventory.
            if (const auto slot = inventoryMenu_.handleKey(key, inventory_)) {
                player_.setHeldItem(inventory_.take(*slot));
                inventoryMenu_.close();
            }
            return;
        }
        if (craftingMenu_.isOpen()) {
            if (const Recipe* recipe = craftingMenu_.handleKey(key, inventory_)) craft(*recipe);
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

    void updateWorld(float dt) {
        // While a menu is open or typing in the debug panel, keys must not move the player.
        static const std::array<bool, SDL_SCANCODE_COUNT> noKeys{};
        const bool blockKeys = ImGui::GetIO().WantCaptureKeyboard || menuOpen();
        const bool* keys = blockKeys ? noKeys.data() : SDL_GetKeyboardState(nullptr);
        const std::vector<SDL_FRect> obstacles = furniture_.hitboxes();
        if (const int damageTaken = player_.update(dt, keys, map_, obstacles); damageTaken > 0) {
            const SDL_FPoint middle = player_.center();
            effects_.addDamageNumber(damageTaken, middle.x, middle.y, kPlayerDamageColor);
        }
        // Held Space: after kPunchHoldDelay, punch every kRapidPunchInterval (the first came from the key press).
        if (keys[SDL_SCANCODE_SPACE]) {
            punchRepeatTimer_ -= dt;
            if (punchRepeatTimer_ <= 0.0f) {
                useOrPunch();
                punchRepeatTimer_ += kRapidPunchInterval;
            }
        }
        dayNight_.update(dt);
        mobs_.update(dt, map_, player_, effects_, droppedItems_, dayNight_.time() == DayNight::Time::Night,
                        obstacles);
        if (player_.isDead()) respawn();
        droppedItems_.update(dt, map_, player_.hitbox(), inventory_);
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
        const SDL_FRect bounds = player_.bounds();
        camera_.follow(camera_.snap(bounds.x) + bounds.w / 2.0f, camera_.snap(bounds.y) + bounds.h / 2.0f,
                       map_.pixelWidth(), map_.pixelHeight());
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
        if (inWorld_) debug_.drawWorldOverlay(renderer, camera_, scale_, map_, player_, mobs_);

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
        map_.draw(renderer, camera_, time_);
        droppedItems_.draw(renderer, camera_, itemIcons_);
        const float playerY = player_.center().y;
        furniture_.draw(renderer, camera_, playerY, true);
        mobs_.draw(renderer, camera_, playerY, true);
        player_.draw(renderer, camera_);
        if (player_.isCarryingFurniture()) {
            const SDL_FPoint carried = player_.carriedFurniturePosition();
            furniture_.drawSprite(renderer, camera_, player_.heldItem()->type, carried.x, carried.y);
        }
        furniture_.draw(renderer, camera_, playerY, false);
        mobs_.draw(renderer, camera_, playerY, false);
        effects_.draw(renderer, camera_, font_);

        // Night: darken everything except a circle of light around the player (Minicraft: radius 5 * 8 px, centred
        // on the entity position moved up-left by (1, 4)).
        const SDL_FPoint middle = player_.center();
        lighting_.draw(renderer, camera_, dayNight_.darkness(), {{middle.x - 1.0f, middle.y - 4.0f, kPlayerLightRadius}});

        // UI in view pixels (same scale, not moved by the camera).
        hud_.drawStatus(renderer, player_, std::floor(camera_.height()));
        if (const auto& held = player_.heldItem()) {
            hud_.drawHeldItem(renderer, font_, itemIcons_, *held, std::floor(camera_.height()));
            if (isTool(held->type)) hud_.drawToolDurability(renderer, font_, *held, std::floor(camera_.height()));
        }
        inventoryMenu_.draw(renderer, hud_, font_, itemIcons_, inventory_);
        craftingMenu_.draw(renderer, hud_, font_, itemIcons_, inventory_);
        workbenchMenu_.draw(renderer, hud_, font_, itemIcons_, inventory_);
    }

    void drawDebugPanel() {
        const auto actions =
            debug_.drawPanel(camera_, scale_, map_, player_, inventory_, droppedItems_.size(), mobs_, dayNight_);
        if (actions.regenerateSeed) newWorld(*actions.regenerateSeed);
        if (actions.refillStats) player_.refillStats();
        if (actions.spawnZombie) {
            const SDL_FPoint p = player_.center();
            mobs_.spawnNear(MobKind::Zombie, map_, furniture_.hitboxes(), p.x, p.y, 3, 6);
        }
        if (actions.spawnAnimal) {
            const SDL_FPoint p = player_.center();
            const auto kind = static_cast<MobKind>(1 + static_cast<int>(SDL_rand(3)));  // cow, pig or sheep
            mobs_.spawnNear(kind, map_, furniture_.hitboxes(), p.x, p.y, 3, 6);
        }
        if (actions.clearMobs) mobs_.clear();
        if (actions.setTime) dayNight_.setTime(*actions.setTime);
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

    // Members are destroyed in reverse order: textures and ImGui first, then renderer, window, and SDL_Quit.
    SdlQuit sdlQuit_;
    std::unique_ptr<SDL_Window, WindowDeleter> window_;
    std::unique_ptr<SDL_Renderer, RendererDeleter> renderer_;
    ImGuiContextGuard imgui_;
    TileMap map_;
    Player player_;
    Effects effects_;
    Mobs mobs_;
    Furniture furniture_;
    DayNight dayNight_;
    Lighting lighting_;
    DroppedItems droppedItems_;
    Inventory inventory_;
    InventoryMenu inventoryMenu_;
    CraftingMenu craftingMenu_{Recipe::personalRecipes(), "Crafting"};
    CraftingMenu workbenchMenu_{Recipe::workbenchRecipes(), "Workbench"};
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
    float time_ = 0.0f;
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
