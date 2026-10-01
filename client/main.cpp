#include "camera.h"
#include "debug_overlay.h"
#include "dropped_items.h"
#include "effects.h"
#include "font.h"
#include "hud.h"
#include "inventory_menu.h"
#include "items.h"
#include "player.h"
#include "tile_map.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

class Game {
public:
    // Minimum view in world pixels; the window is scaled up by the largest whole factor that still fits it.
    static constexpr int kViewWidth = 240;
    static constexpr int kViewHeight = 135;
    static constexpr int kMapSize = 128;  // tiles
    static constexpr std::uint32_t kDefaultSeed = 1337;
    // Holding Space keeps punching at this interval (5 ticks, 12 punches/s; the same length as a punch, so they
    // chain back to back) until energy runs out.
    static constexpr float kPunchRepeatInterval = 5.0f / 60.0f;
    static constexpr SDL_Color kPlayerDamageColor{255, 0, 204, 255};  // Minicraft: Color.get(-1, 504)

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
            !inventoryMenu_.load(renderer, sprites + "inventory_counter.png")) {
            return false;
        }
        newWorld(kDefaultSeed);
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
    void newWorld(std::uint32_t seed) {
        map_.generate(seed, kMapSize, kMapSize);
        effects_.clear();
        droppedItems_.clear();
        const SDL_FPoint spawn = map_.findSpawnPoint();
        player_.setPosition(spawn.x, spawn.y);
    }

    // No death screen yet: back to the spawn point with full health and energy; the inventory is kept.
    void respawn() {
        const SDL_FPoint spawn = map_.findSpawnPoint();
        player_.setPosition(spawn.x, spawn.y);
        player_.refillStats();
    }

    void punch() {
        if (!player_.tryPunch()) return;  // out of energy
        const SDL_Point target = player_.interactionTile();
        const int damage = static_cast<int>(SDL_rand(3)) + 1;  // bare-hand punch: 1-3, like Minicraft
        const auto hit = map_.hurtTile(target.x, target.y, damage);
        if (!hit) {
            player_.showSlash();  // nothing to hit: just the slash
            return;
        }
        const float centerX = static_cast<float>(target.x * TileMap::kTileSize + TileMap::kTileSize / 2);
        const float centerY = static_cast<float>(target.y * TileMap::kTileSize + TileMap::kTileSize / 2);
        effects_.addSmash(target.x, target.y);
        effects_.addDamageNumber(damage, centerX, centerY);
        if (hit->broken) {
            // Minicraft drops: a tree gives 1-3 wood, a rock punched by hand gives 1 stone.
            if (hit->tile == Tile::Tree) droppedItems_.spawn(ItemType::Wood, 1 + static_cast<int>(SDL_rand(3)), centerX, centerY);
            if (hit->tile == Tile::Rock) droppedItems_.spawn(ItemType::Stone, 1, centerX, centerY);
        }
    }

    void handleEvents() {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT) {
                running_ = false;
            } else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                handleKey(event.key.key);
            }
        }
    }

    void handleKey(SDL_Keycode key) {
        if (key == DebugOverlay::kToggleKey) debug_.toggle();
        if (ImGui::GetIO().WantCaptureKeyboard) return;  // typing in the debug panel
        if (key == SDLK_ESCAPE) {
            if (inventoryMenu_.isOpen()) inventoryMenu_.close();
            else running_ = false;
            return;
        }
        if (key == InventoryMenu::kToggleKey) {
            inventoryMenu_.toggle();
            return;
        }
        if (inventoryMenu_.isOpen()) {
            inventoryMenu_.handleKey(key, inventory_);
            return;
        }
        if (key == SDLK_SPACE) {
            punch();  // a fresh press always punches right away
            punchRepeatTimer_ = kPunchRepeatInterval;
        }
    }

    void update(float dt) {
        // While a menu is open or typing in the debug panel, keys must not move the player.
        static const std::array<bool, SDL_SCANCODE_COUNT> noKeys{};
        const bool blockKeys = ImGui::GetIO().WantCaptureKeyboard || inventoryMenu_.isOpen();
        const bool* keys = blockKeys ? noKeys.data() : SDL_GetKeyboardState(nullptr);
        if (const int damageTaken = player_.update(dt, keys, map_); damageTaken > 0) {
            const SDL_FPoint middle = player_.center();
            effects_.addDamageNumber(damageTaken, middle.x, middle.y, kPlayerDamageColor);
        }
        if (player_.isDead()) respawn();
        // Held Space: repeat the punch every kPunchRepeatInterval (the first one came from the key press).
        if (keys[SDL_SCANCODE_SPACE]) {
            punchRepeatTimer_ -= dt;
            if (punchRepeatTimer_ <= 0.0f) {
                punch();
                punchRepeatTimer_ += kPunchRepeatInterval;
            }
        }
        droppedItems_.update(dt, map_, player_.hitbox(), inventory_);
        effects_.update(dt);

        int outputWidth = 0;
        int outputHeight = 0;
        SDL_GetCurrentRenderOutputSize(renderer_.get(), &outputWidth, &outputHeight);
        scale_ = static_cast<float>(std::max(1, std::min(outputWidth / kViewWidth, outputHeight / kViewHeight)));
        camera_.setView(static_cast<float>(outputWidth) / scale_, static_cast<float>(outputHeight) / scale_, scale_);
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
        map_.draw(renderer, camera_, time_);
        droppedItems_.draw(renderer, camera_, itemIcons_);
        player_.draw(renderer, camera_);
        effects_.draw(renderer, camera_, font_);

        // UI in view pixels (same scale, not moved by the camera).
        hud_.drawStatus(renderer, player_, std::floor(camera_.height()));
        inventoryMenu_.draw(renderer, hud_, font_, itemIcons_, inventory_);

        // Debug overlay and UI: drawn in screen pixels so lines stay thin.
        SDL_SetRenderScale(renderer, 1.0f, 1.0f);
        debug_.drawWorldOverlay(renderer, camera_, scale_, map_, player_);

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        const auto actions = debug_.drawPanel(camera_, scale_, map_, player_, inventory_, droppedItems_.size());
        if (actions.regenerateSeed) newWorld(*actions.regenerateSeed);
        if (actions.refillStats) player_.refillStats();
        ImGui::Render();
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);

        SDL_RenderPresent(renderer);
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
    DroppedItems droppedItems_;
    Inventory inventory_;
    InventoryMenu inventoryMenu_;
    Hud hud_;
    Font font_;
    ItemIcons itemIcons_;
    Camera camera_;
    DebugOverlay debug_;
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
