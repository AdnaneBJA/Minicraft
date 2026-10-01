#include "sdl_platform.hpp"

#include <stdexcept>

namespace mc::client {
namespace {

[[noreturn]] void throwSdlError(const std::string& what) {
    throw std::runtime_error(what + ": " + SDL_GetError());
}

}  // namespace

SdlContext::SdlContext(SDL_InitFlags flags) {
    if (!SDL_Init(flags)) {
        throwSdlError("SDL_Init failed");
    }
}

SdlContext::~SdlContext() { SDL_Quit(); }

Window::Window(const std::string& title, int width, int height)
    : window_(SDL_CreateWindow(title.c_str(), width, height, SDL_WINDOW_RESIZABLE)) {
    if (!window_) {
        throwSdlError("SDL_CreateWindow failed");
    }
}

Renderer::Renderer(const Window& window) : renderer_(SDL_CreateRenderer(window.handle(), nullptr)) {
    if (!renderer_) {
        throwSdlError("SDL_CreateRenderer failed");
    }
    // VSync is a nicety; carry on without it if the driver refuses.
    SDL_SetRenderVSync(renderer_.get(), 1);
}

void Renderer::clear(SDL_Color color) {
    SDL_SetRenderDrawColor(renderer_.get(), color.r, color.g, color.b, color.a);
    SDL_RenderClear(renderer_.get());
}

void Renderer::fillRect(const SDL_FRect& rect, SDL_Color color) {
    SDL_SetRenderDrawColor(renderer_.get(), color.r, color.g, color.b, color.a);
    SDL_RenderFillRect(renderer_.get(), &rect);
}

void Renderer::present() { SDL_RenderPresent(renderer_.get()); }

SDL_Point Renderer::outputSize() const {
    SDL_Point size{0, 0};
    SDL_GetCurrentRenderOutputSize(renderer_.get(), &size.x, &size.y);
    return size;
}

}  // namespace mc::client
