#pragma once

#include "day_night.h"

#include <SDL3/SDL.h>

#include <cstdint>
#include <optional>

class Camera;
class Inventory;
class Zombies;
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
                          const Player& player, const Zombies& zombies) const;

    // What the panel asked the game to do this frame.
    struct PanelActions {
        std::optional<std::uint32_t> regenerateSeed;
        bool refillStats = false;
        bool spawnZombie = false;
        bool clearZombies = false;
        std::optional<DayNight::Time> setTime;
    };
    // Draws the ImGui panel.
    PanelActions drawPanel(const Camera& camera, float scale, const TileMap& map, const Player& player,
                           const Inventory& inventory, std::size_t droppedItemCount, Zombies& zombies,
                           const DayNight& dayNight);

private:
    bool enabled_ = false;
    bool showPlayerOutline_ = true;
    bool showHitbox_ = true;
    bool showSolidTiles_ = false;
    bool showPunchTarget_ = true;
    bool showTileGrid_ = true;
    bool showTileUnderPlayer_ = true;
    bool showInfo_ = true;
    int gridOpacity_ = 35;  // 0-255; kept low so the grid is easy on the eyes
    std::uint32_t seedInput_ = 0;
    bool seedInputInitialised_ = false;
};
