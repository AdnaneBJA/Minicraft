#pragma once

#include "items.h"
#include "tile_map.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Everything a saved world remembers. Mobs, dropped items and effects are not saved (they respawn or vanish).
struct WorldSaveData {
    std::uint32_t seed = 0;
    int width = 0;  // tiles
    int height = 0;
    std::vector<Tile> tiles;
    std::vector<std::uint8_t> damage;  // accumulated damage per tile
    float playerX = 0.0f;              // world pixels (sprite top-left)
    float playerY = 0.0f;
    int health = 0;
    int energy = 0;
    int dayTick = 0;
    bool pastDay1 = false;
    std::vector<Inventory::Stack> inventory;
};

// The saved worlds: one binary file per world, `<name>.sav`, in a saves folder.
class WorldSaves {
public:
    static constexpr std::size_t kMaxNameLength = 16;

    explicit WorldSaves(std::filesystem::path directory);

    // Lower-case letters, digits, spaces, '-' and '_'; not empty, no leading/trailing space, and not a reserved
    // Windows device name. Names come from the player and become file names, so this also keeps paths inside the
    // saves folder.
    static bool isValidName(std::string_view name);

    // Saved world names, most recently saved first.
    std::vector<std::string> list() const;
    bool exists(std::string_view name) const;

    bool save(std::string_view name, const WorldSaveData& data) const;
    // Returns nothing if the file is missing, damaged, or holds values out of range.
    std::optional<WorldSaveData> load(std::string_view name) const;

private:
    std::filesystem::path pathFor(std::string_view name) const;

    std::filesystem::path directory_;
};
