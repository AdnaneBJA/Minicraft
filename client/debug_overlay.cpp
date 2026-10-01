#include "debug_overlay.h"

#include "camera.h"
#include "items.h"
#include "player.h"
#include "tile_map.h"
#include "world_gen.h"
#include "zombie.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>

void DebugOverlay::drawWorldOverlay(SDL_Renderer* renderer, const Camera& camera, float scale, const TileMap& map,
                                    const Player& player, const Zombies& zombies) const {
    if (!enabled_) return;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    const auto toScreenX = [&](float worldX) { return std::round((worldX - camera.x()) * scale); };
    const auto toScreenY = [&](float worldY) { return std::round((worldY - camera.y()) * scale); };
    constexpr float tileSize = static_cast<float>(TileMap::kTileSize);

    if (showSolidTiles_) {
        SDL_SetRenderDrawColor(renderer, 255, 60, 60, 45);
        const int firstX = std::max(0, static_cast<int>(camera.x() / tileSize));
        const int lastX = std::min(map.width() - 1, static_cast<int>((camera.x() + camera.width()) / tileSize));
        const int firstY = std::max(0, static_cast<int>(camera.y() / tileSize));
        const int lastY = std::min(map.height() - 1, static_cast<int>((camera.y() + camera.height()) / tileSize));
        for (int ty = firstY; ty <= lastY; ++ty) {
            for (int tx = firstX; tx <= lastX; ++tx) {
                if (!map.isSolidAt(tx, ty)) continue;
                const SDL_FRect rect{toScreenX(static_cast<float>(tx) * tileSize),
                                     toScreenY(static_cast<float>(ty) * tileSize), tileSize * scale,
                                     tileSize * scale};
                SDL_RenderFillRect(renderer, &rect);
            }
        }
    }

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

    if (showPunchTarget_) {
        const SDL_Point target = player.interactionTile();
        const SDL_FRect targetRect{toScreenX(static_cast<float>(target.x) * tileSize),
                                   toScreenY(static_cast<float>(target.y) * tileSize), tileSize * scale,
                                   tileSize * scale};
        SDL_SetRenderDrawColor(renderer, 255, 140, 0, 200);
        SDL_RenderRect(renderer, &targetRect);
    }

    if (showPlayerOutline_) {
        const SDL_FRect playerRect{toScreenX(camera.snap(bounds.x)), toScreenY(camera.snap(bounds.y)),
                                   bounds.w * scale, bounds.h * scale};
        SDL_SetRenderDrawColor(renderer, 255, 220, 40, 230);
        SDL_RenderRect(renderer, &playerRect);
    }

    if (showHitbox_) {
        // Same offset as the sprite so the box lines up with what is drawn.
        const SDL_FRect hitbox = player.hitbox();
        const float offsetX = camera.snap(bounds.x) - bounds.x;
        const float offsetY = camera.snap(bounds.y) - bounds.y;
        const SDL_FRect hitboxRect{toScreenX(hitbox.x + offsetX), toScreenY(hitbox.y + offsetY), hitbox.w * scale,
                                   hitbox.h * scale};
        SDL_SetRenderDrawColor(renderer, 255, 70, 200, 255);
        SDL_RenderRect(renderer, &hitboxRect);
    }

    if (showHitbox_) {
        // Zombie hitboxes, and the area the next punch reaches for mobs.
        const auto worldRect = [&](const SDL_FRect& r) {
            return SDL_FRect{toScreenX(r.x), toScreenY(r.y), r.w * scale, r.h * scale};
        };
        SDL_SetRenderDrawColor(renderer, 255, 70, 200, 255);
        for (const Zombie& zombie : zombies.all()) {
            const SDL_FRect rect = worldRect(zombie.hitbox());
            SDL_RenderRect(renderer, &rect);
        }
        SDL_SetRenderDrawColor(renderer, 255, 140, 0, 120);
        const SDL_FRect attack = worldRect(player.attackBox());
        SDL_RenderRect(renderer, &attack);
    }
}

DebugOverlay::PanelActions DebugOverlay::drawPanel(const Camera& camera, float scale, const TileMap& map,
                                                   const Player& player, const Inventory& inventory,
                                                   std::size_t droppedItemCount, Zombies& zombies) {
    if (!enabled_) return {};
    if (!seedInputInitialised_) {
        seedInput_ = map.seed();
        seedInputInitialised_ = true;
    }

    PanelActions actions;
    ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.85f);
    if (ImGui::Begin("Debug (F3)", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::SeparatorText("Show");
        ImGui::Checkbox("Player outline", &showPlayerOutline_);
        ImGui::Checkbox("Player hitbox", &showHitbox_);
        ImGui::Checkbox("Solid tiles", &showSolidTiles_);
        ImGui::Checkbox("Punch target", &showPunchTarget_);
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
            ImGui::Text("Biome: %s", biomeName(WorldGenerator(map.seed()).biomeAt(tx, ty)));
            const SDL_Point target = player.interactionTile();
            if (map.inBounds(target.x, target.y)) {
                const Tile targetTile = map.tileAt(target.x, target.y);
                if (maxHealth(targetTile) > 0) {
                    ImGui::Text("Punch target: %d, %d (%s, %d/%d damage)", target.x, target.y, tileName(targetTile),
                                map.damageAt(target.x, target.y), maxHealth(targetTile));
                } else {
                    ImGui::Text("Punch target: %d, %d (%s)", target.x, target.y, tileName(targetTile));
                }
            }
            ImGui::Text("Camera: %.0f, %.0f  view %.0fx%.0f", static_cast<double>(camera.x()),
                        static_cast<double>(camera.y()), static_cast<double>(camera.width()),
                        static_cast<double>(camera.height()));
            ImGui::Text("Scale: %.0fx  Map: %dx%d tiles", static_cast<double>(scale), map.width(), map.height());

            ImGui::SeparatorText("Player");
            ImGui::Text("Health: %d/%d  Energy: %d/%d", player.health(), Player::kMaxHealth, player.energy(),
                        Player::kMaxEnergy);
            ImGui::Text("Swimming: %s", player.isSwimming() ? "yes" : "no");
            if (player.energyRechargeDelay() > 0) {
                ImGui::Text("Exhausted: %d ticks", player.energyRechargeDelay());
            }
            ImGui::Text("Wood: %d  Stone: %d  (on ground: %zu)", inventory.count(ItemType::Wood),
                        inventory.count(ItemType::Stone), droppedItemCount);
            if (ImGui::Button("Refill health/energy")) {
                actions.refillStats = true;
            }
        }

        ImGui::SeparatorText("Zombies");
        ImGui::Text("Alive: %zu / %d", zombies.all().size(), Zombies::kMaxAlive);
        ImGui::Checkbox("Spawn automatically", &zombies.spawningEnabled);
        if (ImGui::Button("Spawn one nearby")) actions.spawnZombie = true;
        ImGui::SameLine();
        if (ImGui::Button("Remove all")) actions.clearZombies = true;

        ImGui::SeparatorText("World");
        ImGui::InputScalar("Seed", ImGuiDataType_U32, &seedInput_);
        if (ImGui::Button("Regenerate")) {
            actions.regenerateSeed = seedInput_;
        }
        ImGui::SameLine();
        if (ImGui::Button("Random seed")) {
            seedInput_ = static_cast<std::uint32_t>(SDL_rand_bits());
            actions.regenerateSeed = seedInput_;
        }
    }
    ImGui::End();
    return actions;
}
