#include "world_save.h"

#include "day_night.h"
#include "player.h"
#include "world.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <utility>

namespace {

constexpr std::array<char, 4> kMagic{'M', 'C', 'W', 'S'};
// 2: inventory stacks also store durability. 3: every level, furniture and chests, hunger, armour, spawn point.
constexpr std::uint32_t kVersion = 3;
constexpr const char* kExtension = ".sav";
constexpr int kMaxStackCount = 1'000'000;
constexpr std::uint32_t kMaxFurniturePerLevel = 4096;
constexpr std::uint8_t kNoArmor = 0xFF;
constexpr std::uintmax_t kMaxFileSize = 16u * 1024u * 1024u;  // a full world is under 1 MB plus chests

// Little-endian encoding, so a save file reads the same on every platform.
class ByteWriter {
public:
    void u8(std::uint8_t value) { bytes_.push_back(value); }
    void u32(std::uint32_t value) {
        for (int i = 0; i < 4; ++i) bytes_.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
    }
    void i32(int value) { u32(static_cast<std::uint32_t>(value)); }
    void f32(float value) { u32(std::bit_cast<std::uint32_t>(value)); }
    void raw(const void* data, std::size_t size) {
        const auto* begin = static_cast<const std::uint8_t*>(data);
        bytes_.insert(bytes_.end(), begin, begin + size);
    }
    const std::vector<std::uint8_t>& bytes() const { return bytes_; }

private:
    std::vector<std::uint8_t> bytes_;
};

// Reads past the end return zeros and clear ok(), so callers can read everything and check once.
class ByteReader {
public:
    explicit ByteReader(const std::vector<std::uint8_t>& bytes) : bytes_(bytes) {}

    std::uint8_t u8() { return has(1) ? bytes_[position_++] : 0; }
    std::uint32_t u32() {
        if (!has(4)) return 0;
        std::uint32_t value = 0;
        for (int i = 0; i < 4; ++i) value |= static_cast<std::uint32_t>(bytes_[position_++]) << (8 * i);
        return value;
    }
    int i32() { return static_cast<int>(u32()); }
    float f32() { return std::bit_cast<float>(u32()); }
    bool raw(void* out, std::size_t size) {
        if (!has(size)) return false;
        std::memcpy(out, bytes_.data() + position_, size);
        position_ += size;
        return true;
    }
    bool ok() const { return ok_; }
    bool atEnd() const { return position_ == bytes_.size(); }

private:
    bool has(std::size_t size) {
        if (bytes_.size() - position_ < size) ok_ = false;
        return ok_;
    }

    const std::vector<std::uint8_t>& bytes_;
    std::size_t position_ = 0;
    bool ok_ = true;
};

// Every world is World::kSize tiles square (older saves were too).
bool isValidSize(int size) { return size == World::kSize; }

// Unknown enum values have no name (the name functions return "?"), which also catches values from a newer game.
bool isKnownTile(std::uint8_t value) { return std::string_view(tileName(static_cast<Tile>(value))) != "?"; }
bool isKnownItem(std::uint8_t value) { return std::string_view(itemName(static_cast<ItemType>(value))) != "?"; }

}  // namespace

WorldSaves::WorldSaves(std::filesystem::path directory) : directory_(std::move(directory)) {
    std::error_code error;
    std::filesystem::create_directories(directory_, error);
    if (error) SDL_Log("Could not create the saves folder: %s", error.message().c_str());
}

bool WorldSaves::isValidName(std::string_view name) {
    if (name.empty() || name.size() > kMaxNameLength || name.front() == ' ' || name.back() == ' ') return false;
    const bool allowedChars = std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '_';
    });
    if (!allowedChars) return false;
    // Windows refuses these as file names, even with an extension ("con.sav").
    static constexpr std::array<std::string_view, 4> kReserved{"con", "prn", "aux", "nul"};
    if (std::find(kReserved.begin(), kReserved.end(), name) != kReserved.end()) return false;
    const bool numberedDevice = name.size() == 4 && (name.starts_with("com") || name.starts_with("lpt")) &&
                                name[3] >= '1' && name[3] <= '9';
    return !numberedDevice;
}

std::filesystem::path WorldSaves::pathFor(std::string_view name) const {
    return directory_ / (std::string(name) + kExtension);
}

std::vector<std::string> WorldSaves::list() const {
    std::vector<std::pair<std::filesystem::file_time_type, std::string>> worlds;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(directory_, error)) {
        const std::filesystem::path& path = entry.path();
        if (!entry.is_regular_file(error) || path.extension() != kExtension) continue;
        const std::string name = path.stem().string();
        if (!isValidName(name)) continue;  // not a file this game wrote
        worlds.emplace_back(entry.last_write_time(error), name);
    }
    std::sort(worlds.begin(), worlds.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<std::string> names;
    for (auto& world : worlds) names.push_back(std::move(world.second));
    return names;
}

bool WorldSaves::exists(std::string_view name) const {
    std::error_code error;
    return isValidName(name) && std::filesystem::exists(pathFor(name), error);
}

namespace {

void writeStacks(ByteWriter& writer, const std::vector<Inventory::Stack>& stacks) {
    writer.u32(static_cast<std::uint32_t>(stacks.size()));
    for (const auto& stack : stacks) {
        writer.u8(static_cast<std::uint8_t>(stack.type));
        writer.i32(stack.count);
        writer.i32(stack.durability);
    }
}

// Reads a list of stacks the way Inventory holds them (at most kMaxSlots; one stack per stackable type; tools and
// furniture one per stack). Version 1 had no durability: tools come back new.
std::optional<std::vector<Inventory::Stack>> readStacks(ByteReader& reader, std::uint32_t version) {
    const std::uint32_t stackCount = reader.u32();
    if (!reader.ok() || stackCount > static_cast<std::uint32_t>(Inventory::kMaxSlots)) return std::nullopt;
    std::vector<Inventory::Stack> stacks;
    for (std::uint32_t i = 0; i < stackCount; ++i) {
        const std::uint8_t type = reader.u8();
        const int count = reader.i32();
        if (!reader.ok() || !isKnownItem(type) || count < 1 || count > kMaxStackCount) return std::nullopt;
        const auto itemType = static_cast<ItemType>(type);
        int durability = maxDurability(itemType);
        if (version >= 2) {
            durability = reader.i32();
            if (!reader.ok() || durability < 0 || durability > maxDurability(itemType)) return std::nullopt;
        }
        const bool duplicate =
            isStackable(itemType) && std::any_of(stacks.begin(), stacks.end(),
                                                 [&](const Inventory::Stack& s) { return s.type == itemType; });
        if (duplicate || (!isStackable(itemType) && count != 1)) return std::nullopt;
        stacks.push_back({itemType, count, durability});
    }
    return stacks;
}

}  // namespace

bool WorldSaves::save(std::string_view name, const WorldSaveData& data) const {
    if (!isValidName(name)) return false;
    ByteWriter writer;
    writer.raw(kMagic.data(), kMagic.size());
    writer.u32(kVersion);
    writer.u32(data.seed);
    writer.i32(data.width);
    writer.i32(data.height);
    writer.u8(static_cast<std::uint8_t>(data.levels.size()));
    for (const auto& level : data.levels) {
        for (const Tile tile : level.tiles) writer.u8(static_cast<std::uint8_t>(tile));
        writer.raw(level.data.data(), level.data.size());
        writer.u32(static_cast<std::uint32_t>(level.furniture.size()));
        for (const auto& piece : level.furniture) {
            writer.u8(static_cast<std::uint8_t>(piece.type));
            writer.f32(piece.x);
            writer.f32(piece.y);
            writer.u8(piece.deathChest ? 1 : 0);
            writeStacks(writer, piece.contents);
        }
    }
    writer.i32(data.currentLevel);
    writer.f32(data.playerX);
    writer.f32(data.playerY);
    writer.i32(data.health);
    writer.i32(data.energy);
    writer.i32(data.hunger);
    writer.u8(data.armor ? static_cast<std::uint8_t>(*data.armor) : kNoArmor);
    writer.i32(data.armorPoints);
    writer.i32(data.spawnLevel);
    writer.f32(data.spawnX);
    writer.f32(data.spawnY);
    writer.i32(data.dayTick);
    writer.u8(data.pastDay1 ? 1 : 0);
    writer.u8(data.airWizardBeaten ? 1 : 0);
    writer.i32(data.secondsPlayed);
    writeStacks(writer, data.inventory);

    // Write a temporary file first, so a crash mid-save never leaves a half-written world behind.
    const std::filesystem::path path = pathFor(name);
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        const auto& bytes = writer.bytes();
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!file) {
            SDL_Log("Could not write %s", temporary.string().c_str());
            return false;
        }
    }
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error) {  // some platforms won't rename over an existing file
        std::filesystem::remove(path, error);
        std::filesystem::rename(temporary, path, error);
    }
    if (error) SDL_Log("Could not save %s: %s", path.string().c_str(), error.message().c_str());
    return !error;
}

std::optional<WorldSaveData> WorldSaves::load(std::string_view name) const {
    if (!isValidName(name)) return std::nullopt;
    const std::filesystem::path path = pathFor(name);
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error || size > kMaxFileSize) return std::nullopt;
    std::ifstream file(path, std::ios::binary);
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (bytes.size() != size) return std::nullopt;

    // Every field is checked before use: the file is outside the game's control.
    ByteReader reader(bytes);
    std::array<char, 4> magic{};
    if (!reader.raw(magic.data(), magic.size()) || magic != kMagic) return std::nullopt;
    const std::uint32_t version = reader.u32();
    if (version < 1 || version > kVersion) return std::nullopt;

    WorldSaveData data;
    data.seed = reader.u32();
    data.width = reader.i32();
    data.height = reader.i32();
    if (!reader.ok() || !isValidSize(data.width) || !isValidSize(data.height)) return std::nullopt;
    const float maxX = static_cast<float>((data.width - 1) * TileMap::kTileSize);
    const float maxY = static_cast<float>((data.height - 1) * TileMap::kTileSize);
    const auto inWorld = [&](float x, float y) {
        return std::isfinite(x) && std::isfinite(y) && x >= 0.0f && y >= 0.0f && x <= maxX + 16.0f &&
               y <= maxY + 16.0f;
    };

    // Versions 1 and 2 saved only the surface; version 3 saves every level.
    const int levelCount = version >= 3 ? reader.u8() : 1;
    if (!reader.ok() || (levelCount != 1 && levelCount != World::kLevelCount)) return std::nullopt;
    if (version >= 3 && levelCount != World::kLevelCount) return std::nullopt;
    const std::size_t tileCount = static_cast<std::size_t>(data.width) * static_cast<std::size_t>(data.height);
    for (int l = 0; l < levelCount; ++l) {
        WorldSaveData::Level level;
        std::vector<std::uint8_t> tileBytes(tileCount);
        level.data.resize(tileCount);
        if (!reader.raw(tileBytes.data(), tileCount) || !reader.raw(level.data.data(), tileCount)) return std::nullopt;
        level.tiles.reserve(tileCount);
        for (std::size_t i = 0; i < tileCount; ++i) {
            if (!isKnownTile(tileBytes[i])) return std::nullopt;
            const auto tile = static_cast<Tile>(tileBytes[i]);
            level.tiles.push_back(tile);
            std::uint8_t& value = level.data[i];
            // A torch remembers the tile under it, which must be open ground; other data is a count that only makes
            // sense below the tile's break point, or a door's open flag.
            if (tile == Tile::Torch) {
                if (!isKnownTile(value) || isSolid(static_cast<Tile>(value)) || static_cast<Tile>(value) == Tile::Torch) {
                    value = static_cast<std::uint8_t>(Tile::Dirt);
                }
            } else if (tile == Tile::WoodDoor || tile == Tile::StoneDoor) {
                value = value ? 1 : 0;
            } else if (tile != Tile::Wheat && tile != Tile::Sapling && tile != Tile::CactusSapling &&
                       value >= std::max(1, maxHealth(tile))) {
                value = 0;
            }
        }
        if (version >= 3) {
            const std::uint32_t furnitureCount = reader.u32();
            if (!reader.ok() || furnitureCount > kMaxFurniturePerLevel) return std::nullopt;
            for (std::uint32_t i = 0; i < furnitureCount; ++i) {
                WorldSaveData::Furniture piece{};
                const std::uint8_t type = reader.u8();
                piece.x = reader.f32();
                piece.y = reader.f32();
                piece.deathChest = reader.u8() != 0;
                if (!reader.ok() || !isKnownItem(type) || !isFurniture(static_cast<ItemType>(type)) ||
                    !inWorld(piece.x, piece.y)) {
                    return std::nullopt;
                }
                piece.type = static_cast<ItemType>(type);
                auto contents = readStacks(reader, version);
                if (!contents) return std::nullopt;
                piece.contents = std::move(*contents);
                level.furniture.push_back(std::move(piece));
            }
        }
        data.levels.push_back(std::move(level));
    }

    if (version >= 3) {
        data.currentLevel = reader.i32();
        if (!reader.ok() || data.currentLevel < 0 || data.currentLevel >= World::kLevelCount) return std::nullopt;
    }
    data.playerX = reader.f32();
    data.playerY = reader.f32();
    data.health = reader.i32();
    data.energy = reader.i32();
    if (version >= 3) {
        data.hunger = reader.i32();
        const std::uint8_t armor = reader.u8();
        data.armorPoints = reader.i32();
        if (armor != kNoArmor) {
            if (!isKnownItem(armor) || armorLevel(static_cast<ItemType>(armor)) == 0) return std::nullopt;
            data.armor = static_cast<ItemType>(armor);
            if (data.armorPoints < 1 || data.armorPoints > Player::kMaxArmor) return std::nullopt;
        }
        data.spawnLevel = reader.i32();
        data.spawnX = reader.f32();
        data.spawnY = reader.f32();
        if (data.spawnLevel < -1 || data.spawnLevel >= World::kLevelCount ||
            (data.spawnLevel >= 0 && !inWorld(data.spawnX, data.spawnY))) {
            return std::nullopt;
        }
    }
    data.dayTick = reader.i32();
    data.pastDay1 = reader.u8() != 0;
    if (version >= 3) {
        data.airWizardBeaten = reader.u8() != 0;
        data.secondsPlayed = reader.i32();
    }
    if (!reader.ok() || !inWorld(data.playerX, data.playerY) || data.health < 1 || data.health > Player::kMaxHealth ||
        data.energy < 0 || data.energy > Player::kMaxEnergy || data.hunger < 0 || data.hunger > Player::kMaxHunger ||
        data.dayTick < 0 || data.dayTick >= DayNight::kDayLength || data.secondsPlayed < 0) {
        return std::nullopt;
    }

    auto inventory = readStacks(reader, version);
    if (!inventory) return std::nullopt;
    data.inventory = std::move(*inventory);
    if (!reader.atEnd()) return std::nullopt;
    return data;
}
