#include "application.hpp"

#include "mc/core/build_info.hpp"

namespace mc::client {
namespace {

constexpr int kInitialWidth = 960;
constexpr int kInitialHeight = 540;
constexpr float kSquareSize = 128.0F;

constexpr SDL_Color kBackground{24, 20, 37, 255};
constexpr SDL_Color kSquare{99, 199, 77, 255};

}  // namespace

Application::Application()
    : sdl_(SDL_INIT_VIDEO),
      window_("Minicraft " + core::BuildInfo::versionString(), kInitialWidth, kInitialHeight),
      renderer_(window_) {}

int Application::run() {
    while (running_) {
        handleEvents();
        render();
    }
    return 0;
}

void Application::handleEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_EVENT_QUIT:
                running_ = false;
                break;
            case SDL_EVENT_KEY_DOWN:
                if (event.key.key == SDLK_ESCAPE) {
                    running_ = false;
                }
                break;
            default:
                break;
        }
    }
}

void Application::render() {
    renderer_.clear(kBackground);

    const SDL_Point size = renderer_.outputSize();
    const SDL_FRect square{
        (static_cast<float>(size.x) - kSquareSize) / 2.0F,
        (static_cast<float>(size.y) - kSquareSize) / 2.0F,
        kSquareSize,
        kSquareSize,
    };
    renderer_.fillRect(square, kSquare);

    renderer_.present();
}

}  // namespace mc::client
