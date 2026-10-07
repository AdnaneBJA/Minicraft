#include "persist.h"

#ifdef __EMSCRIPTEN__

#include <emscripten.h>

void persistMount() {
    // Without IndexedDB (some private windows) the folder still works for this visit; it just isn't kept.
    EM_ASM({
        Module.persistReady = 0;
        FS.mkdir('/persist');
        FS.mount(IDBFS, {}, '/persist');
        FS.syncfs(true, function(error) {
            if (error) console.warn('Could not load saved files:', error);
            Module.persistReady = 1;
        });
    });
}

bool persistReady() { return EM_ASM_INT({ return Module.persistReady; }) != 0; }

void persistFlush() {
    EM_ASM({
        FS.syncfs(false, function(error) {
            if (error) console.warn('Could not store saved files:', error);
        });
    });
}

std::filesystem::path persistDirectory() { return "/persist"; }

#else

#include <SDL3/SDL.h>

void persistMount() {}
bool persistReady() { return true; }
void persistFlush() {}

std::filesystem::path persistDirectory() {
    char* prefPath = SDL_GetPrefPath("Minicraft", "Minicraft");
    std::filesystem::path path;
    if (prefPath) path = std::filesystem::path(reinterpret_cast<const char8_t*>(prefPath));
    SDL_free(prefPath);
    return path;
}

#endif
