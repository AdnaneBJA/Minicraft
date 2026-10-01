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
    struct Furniture {
        ItemType type;
        float x;  // centre, world pixels
        float y;
        bool deathChest = false;
        std::vector<Inventory::Stack> contents;  // what a chest holds
    };
    struct Level {
        std::vector<Tile> tiles;
        std::vector<std::uint8_t> data;  // per-tile data (damage, age, a torch's base tile, a door's state)
        std::vector<Furniture> furniture;
    };

    std::uint32_t seed = 0;
    int width = 0;  // tiles
    int height = 0;
    // Every level, sky first (World order). Saves from before the caves existed hold only the surface: then this
    // has one entry and the other levels are generated from the seed.
    std::vector<Level> levels;
    int currentLevel = 1;  // World index the player is on
    float playerX = 0.0f;  // world pixels (sprite top-left)
    float playerY = 0.0f;
    int health = 0;
    int energy = 0;
    int hunger = 10;
    std::optional<ItemType> armor;
    int armorPoints = 0;
    // Where the player respawns after dying: a bed they slept in, or (level -1) the surface spawn point.
    int spawnLevel = -1;
    float spawnX = 0.0f;
    float spawnY = 0.0f;
    int dayTick = 0;
    bool pastDay1 = false;
    bool airWizardBeaten = false;
    int secondsPlayed = 0;
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
