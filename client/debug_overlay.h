#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <optional>

class Camera;
class Player;
class TileMap;

// Debug mode, toggled with F3: outlines drawn over the world plus an ImGui panel to pick what is shown.
class DebugOverlay {
public:
    static constexpr SDL_Keycode kToggleKey = SDLK_F3;

    void toggle() { enabled_ = !enabled_; }
    bool enabled() const { return enabled_; }

    // Draws outlines in screen pixels. `scale` is how many screen pixels one world pixel covers.
    void drawWorldOverlay(SDL_Renderer* renderer, const Camera& camera, float scale, const TileMap& map,
                          const Player& player) const;

    // Draws the ImGui panel. Returns a seed when the user asks to regenerate the world.
    std::optional<std::uint32_t> drawPanel(const Camera& camera, float scale, const TileMap& map,
                                           const Player& player);

private:
    bool enabled_ = false;
    bool showPlayerOutline_ = true;
    bool showTileGrid_ = true;
    bool showTileUnderPlayer_ = true;
    bool showInfo_ = true;
    int gridOpacity_ = 35;  // 0-255; kept low so the grid is easy on the eyes
    std::uint32_t seedInput_ = 0;
    bool seedInputInitialised_ = false;
};
