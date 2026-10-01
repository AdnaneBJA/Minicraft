#pragma once

#include <SDL3/SDL.h>

#include <memory>
#include <string>

namespace mc::client {

// RAII owner of SDL subsystem initialisation. Must outlive every Window/Renderer.
class SdlContext {
public:
    explicit SdlContext(SDL_InitFlags flags);
    ~SdlContext();

    SdlContext(const SdlContext&) = delete;
    SdlContext& operator=(const SdlContext&) = delete;
    SdlContext(SdlContext&&) = delete;
    SdlContext& operator=(SdlContext&&) = delete;
};

class Window {
public:
    Window(const std::string& title, int width, int height);

    [[nodiscard]] SDL_Window* handle() const noexcept { return window_.get(); }

private:
    struct Deleter {
        void operator()(SDL_Window* window) const noexcept { SDL_DestroyWindow(window); }
    };
    std::unique_ptr<SDL_Window, Deleter> window_;
};

class Renderer {
public:
    explicit Renderer(const Window& window);

    void clear(SDL_Color color);
    void fillRect(const SDL_FRect& rect, SDL_Color color);
    void present();

    // Size of the render target in pixels (may differ from window size on high-DPI displays).
    [[nodiscard]] SDL_Point outputSize() const;

private:
    struct Deleter {
        void operator()(SDL_Renderer* renderer) const noexcept { SDL_DestroyRenderer(renderer); }
    };
    std::unique_ptr<SDL_Renderer, Deleter> renderer_;
};

}  // namespace mc::client
