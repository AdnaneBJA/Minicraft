#include "application.hpp"

#include <SDL3/SDL_main.h>

#include <exception>

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
    try {
        mc::client::Application app;
        return app.run();
    } catch (const std::exception& e) {
        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "Fatal: %s", e.what());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Minicraft", e.what(), nullptr);
        return 1;
    }
}
