#pragma once

#include "sdl_platform.hpp"

namespace mc::client {

class Application {
public:
    Application();

    // Runs the main loop until the window is closed. Returns the process exit code.
    int run();

private:
    void handleEvents();
    void render();

    SdlContext sdl_;
    Window window_;
    Renderer renderer_;
    bool running_ = true;
};

}  // namespace mc::client
