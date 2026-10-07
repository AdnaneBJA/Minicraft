#pragma once

#include "day_night.h"
#include "items.h"
#include "mob.h"

#include <SDL3/SDL.h>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class Camera;
class Inventory;
class Mobs;
class Player;
class TileMap;

// Debug mode, toggled with F3: outlines drawn over the world plus an ImGui panel to pick what is shown.
class DebugOverlay {
public:
    static constexpr SDL_Keycode kToggleKey = SDLK_F3;

    void toggle() { enabled_ = !enabled_; }
    bool enabled() const { return enabled_; }
    // "Full bright": no darkness at night or underground, to look around the caves.
    bool fullBright() const { return enabled_ && fullBright_; }

    // Draws outlines in screen pixels. `scale` is how many screen pixels one world pixel covers.
    void drawWorldOverlay(SDL_Renderer* renderer, const Camera& camera, float scale, const TileMap& map,
                          const Player& player, const Mobs& mobs) const;

    // What the panel asked the game to do this frame.
    struct PanelActions {
        std::optional<std::uint32_t> regenerateSeed;
        bool refillStats = false;
        std::optional<std::pair<MobKind, int>> spawnMob;  // kind and level, near the player
        std::optional<int> gotoLevel;                     // World level index to jump to
        bool clearMobs = false;
        std::optional<DayNight::Time> setTime;
        std::vector<Inventory::Stack> giveItems;  // put in the inventory (whatever doesn't fit drops at the player)
        bool clearInventory = false;
    };
    // Draws the ImGui panel.
    PanelActions drawPanel(const Camera& camera, float scale, const TileMap& map, const Player& player,
                           const Inventory& inventory, std::size_t droppedItemCount, const Mobs& mobs,
                           const DayNight& dayNight, const std::string& levelName, int levelIndex);

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
    int giveItem_ = 0;     // index into ItemType for the "Give items" picker
    int giveAmount_ = 10;
    int spawnKind_ = 0;   // MobKind for "Spawn"
    int spawnLevel_ = 1;
    bool fullBright_ = false;
};
