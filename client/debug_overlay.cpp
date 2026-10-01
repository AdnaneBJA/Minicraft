#include "debug_overlay.h"

#include "camera.h"
#include "player.h"
#include "tile_map.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>

void DebugOverlay::drawWorldOverlay(SDL_Renderer* renderer, const Camera& camera, float scale, const TileMap& map,
                                    const Player& player) const {
    if (!enabled_) return;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    const auto toScreenX = [&](float worldX) { return std::round((worldX - camera.x()) * scale); };
    const auto toScreenY = [&](float worldY) { return std::round((worldY - camera.y()) * scale); };
    constexpr float tileSize = static_cast<float>(TileMap::kTileSize);

    if (showTileGrid_) {
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, static_cast<Uint8>(gridOpacity_));
        const int firstX = std::max(0, static_cast<int>(camera.x() / tileSize));
        const int lastX = std::min(map.width(), static_cast<int>((camera.x() + camera.width()) / tileSize) + 1);
        const int firstY = std::max(0, static_cast<int>(camera.y() / tileSize));
        const int lastY = std::min(map.height(), static_cast<int>((camera.y() + camera.height()) / tileSize) + 1);
        const float top = toScreenY(static_cast<float>(firstY) * tileSize);
        const float bottom = toScreenY(static_cast<float>(lastY) * tileSize);
        const float left = toScreenX(static_cast<float>(firstX) * tileSize);
        const float right = toScreenX(static_cast<float>(lastX) * tileSize);
        for (int tx = firstX; tx <= lastX; ++tx) {
            const float x = toScreenX(static_cast<float>(tx) * tileSize);
            SDL_RenderLine(renderer, x, top, x, bottom);
        }
        for (int ty = firstY; ty <= lastY; ++ty) {
            const float y = toScreenY(static_cast<float>(ty) * tileSize);
            SDL_RenderLine(renderer, left, y, right, y);
        }
    }

    const SDL_FRect bounds = player.bounds();
    if (showTileUnderPlayer_) {
        const int tx = static_cast<int>((bounds.x + bounds.w / 2.0f) / tileSize);
        const int ty = static_cast<int>((bounds.y + bounds.h / 2.0f) / tileSize);
        const SDL_FRect tileRect{toScreenX(static_cast<float>(tx) * tileSize),
                                 toScreenY(static_cast<float>(ty) * tileSize), tileSize * scale, tileSize * scale};
        SDL_SetRenderDrawColor(renderer, 80, 220, 255, 140);
        SDL_RenderRect(renderer, &tileRect);
    }

    if (showPlayerOutline_) {
        const SDL_FRect playerRect{toScreenX(camera.snap(bounds.x)), toScreenY(camera.snap(bounds.y)),
                                   bounds.w * scale, bounds.h * scale};
        SDL_SetRenderDrawColor(renderer, 255, 220, 40, 230);
        SDL_RenderRect(renderer, &playerRect);
    }
}

std::optional<std::uint32_t> DebugOverlay::drawPanel(const Camera& camera, float scale, const TileMap& map,
                                                     const Player& player) {
    if (!enabled_) return std::nullopt;
    if (!seedInputInitialised_) {
        seedInput_ = map.seed();
        seedInputInitialised_ = true;
    }

    std::optional<std::uint32_t> regenerateSeed;
    ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.85f);
    if (ImGui::Begin("Debug (F3)", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::SeparatorText("Show");
        ImGui::Checkbox("Player outline", &showPlayerOutline_);
        ImGui::Checkbox("Tile grid", &showTileGrid_);
        if (showTileGrid_) {
            ImGui::SliderInt("Grid opacity", &gridOpacity_, 5, 255);
        }
        ImGui::Checkbox("Tile under player", &showTileUnderPlayer_);
        ImGui::Checkbox("Info", &showInfo_);

        if (showInfo_) {
            const SDL_FRect bounds = player.bounds();
            const float centerX = bounds.x + bounds.w / 2.0f;
            const float centerY = bounds.y + bounds.h / 2.0f;
            const int tx = static_cast<int>(centerX) / TileMap::kTileSize;
            const int ty = static_cast<int>(centerY) / TileMap::kTileSize;
            ImGui::SeparatorText("Info");
            ImGui::Text("FPS: %.0f", static_cast<double>(ImGui::GetIO().Framerate));
            ImGui::Text("Player: %.1f, %.1f px", static_cast<double>(bounds.x), static_cast<double>(bounds.y));
            ImGui::Text("Tile: %d, %d (%s)", tx, ty, map.inBounds(tx, ty) ? tileName(map.tileAt(tx, ty)) : "-");
            ImGui::Text("Camera: %.0f, %.0f  view %.0fx%.0f", static_cast<double>(camera.x()),
                        static_cast<double>(camera.y()), static_cast<double>(camera.width()),
                        static_cast<double>(camera.height()));
            ImGui::Text("Scale: %.0fx  Map: %dx%d tiles", static_cast<double>(scale), map.width(), map.height());
        }

        ImGui::SeparatorText("World");
        ImGui::InputScalar("Seed", ImGuiDataType_U32, &seedInput_);
        if (ImGui::Button("Regenerate")) {
            regenerateSeed = seedInput_;
        }
        ImGui::SameLine();
        if (ImGui::Button("Random seed")) {
            seedInput_ = static_cast<std::uint32_t>(SDL_rand_bits());
            regenerateSeed = seedInput_;
        }
    }
    ImGui::End();
    return regenerateSeed;
}
