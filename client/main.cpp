#include "camera.h"
#include "debug_overlay.h"
#include "player.h"
#include "tile_map.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <algorithm>
#include <array>
#include <string>

class Game {
public:
    // Minimum view in world pixels; the window is scaled up by the largest whole factor that still fits it.
    static constexpr int kViewWidth = 240;
    static constexpr int kViewHeight = 135;
    static constexpr int kMapSize = 128;  // tiles
    static constexpr std::uint32_t kDefaultSeed = 1337;

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
        if (!player_.load(renderer, assets + "sprites/player.png") ||
            !map_.load(renderer, assets + "sprites/tiles.png")) {
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
        const SDL_FPoint spawn = map_.findSpawnPoint();
        player_.setPosition(spawn.x, spawn.y);
    }

    void handleEvents() {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT) {
                running_ = false;
            } else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                if (event.key.key == SDLK_ESCAPE) running_ = false;
                if (event.key.key == DebugOverlay::kToggleKey) debug_.toggle();
            }
        }
    }

    void update(float dt) {
        // While typing in the debug panel, keys must not move the player.
        static const std::array<bool, SDL_SCANCODE_COUNT> noKeys{};
        const bool* keys = ImGui::GetIO().WantCaptureKeyboard ? noKeys.data() : SDL_GetKeyboardState(nullptr);
        player_.update(dt, keys, map_.pixelWidth(), map_.pixelHeight());

        int outputWidth = 0;
        int outputHeight = 0;
        SDL_GetCurrentRenderOutputSize(renderer_.get(), &outputWidth, &outputHeight);
        scale_ = static_cast<float>(std::max(1, std::min(outputWidth / kViewWidth, outputHeight / kViewHeight)));
        camera_.setViewSize(static_cast<float>(outputWidth) / scale_, static_cast<float>(outputHeight) / scale_);
        const SDL_FRect bounds = player_.bounds();
        camera_.follow(bounds.x + bounds.w / 2.0f, bounds.y + bounds.h / 2.0f, map_.pixelWidth(),
                       map_.pixelHeight());
    }

    void draw() {
        SDL_Renderer* renderer = renderer_.get();
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        // World: drawn in world pixels, scaled up by a whole factor.
        SDL_SetRenderScale(renderer, scale_, scale_);
        map_.draw(renderer, camera_, time_);
        player_.draw(renderer, camera_);

        // Debug overlay and UI: drawn in screen pixels so lines stay thin.
        SDL_SetRenderScale(renderer, 1.0f, 1.0f);
        debug_.drawWorldOverlay(renderer, camera_, scale_, map_, player_);

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        if (const auto seed = debug_.drawPanel(camera_, scale_, map_, player_)) {
            newWorld(*seed);
        }
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
    Camera camera_;
    DebugOverlay debug_;
    float scale_ = 1.0f;
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
