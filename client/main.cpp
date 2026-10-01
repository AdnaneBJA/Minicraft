#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

class Game {
public:
    bool init() {
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            SDL_Log("SDL_Init failed: %s", SDL_GetError());
            return false;
        }
        if (!SDL_CreateWindowAndRenderer("Minicraft", 960, 540, SDL_WINDOW_RESIZABLE, &window_, &renderer_)) {
            SDL_Log("Window creation failed: %s", SDL_GetError());
            return false;
        }
        SDL_SetRenderVSync(renderer_, 1);
        return true;
    }

    void run() {
        while (running_) {
            handleEvents();
            draw();
        }
    }

    ~Game() {
        SDL_DestroyRenderer(renderer_);
        SDL_DestroyWindow(window_);
        SDL_Quit();
    }

private:
    void handleEvents() {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)) {
                running_ = false;
            }
        }
    }

    void draw() {
        SDL_SetRenderDrawColor(renderer_, 24, 20, 37, 255);
        SDL_RenderClear(renderer_);

        int width = 0;
        int height = 0;
        SDL_GetCurrentRenderOutputSize(renderer_, &width, &height);
        const float size = 128.0f;
        const SDL_FRect square{(width - size) / 2.0f, (height - size) / 2.0f, size, size};

        SDL_SetRenderDrawColor(renderer_, 99, 199, 77, 255);
        SDL_RenderFillRect(renderer_, &square);

        SDL_RenderPresent(renderer_);
    }

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
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
