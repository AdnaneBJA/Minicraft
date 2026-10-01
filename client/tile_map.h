#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class Camera;

enum class Tile : std::uint8_t { Grass, Sand, Water, Rock, Tree };

const char* tileName(Tile tile);

class TileMap {
public:
    static constexpr int kTileSize = 16;

    bool load(SDL_Renderer* renderer, const std::string& atlasPath);

    // Procedurally generates an island: water at the edges, then sand, grass/forest, and rock in the highlands.
    void generate(std::uint32_t seed, int width, int height);

    // Finds a walkable tile near the centre of the map; returns its top-left pixel position.
    SDL_FPoint findSpawnPoint() const;

    void draw(SDL_Renderer* renderer, const Camera& camera, float timeSeconds) const;

    Tile tileAt(int tx, int ty) const { return tiles_[static_cast<std::size_t>(ty * width_ + tx)]; }
    bool inBounds(int tx, int ty) const { return tx >= 0 && ty >= 0 && tx < width_ && ty < height_; }
    int width() const { return width_; }
    int height() const { return height_; }
    float pixelWidth() const { return static_cast<float>(width_ * kTileSize); }
    float pixelHeight() const { return static_cast<float>(height_ * kTileSize); }
    std::uint32_t seed() const { return seed_; }

private:
    struct TextureDeleter {
        void operator()(SDL_Texture* texture) const { SDL_DestroyTexture(texture); }
    };

    void drawSprite(SDL_Renderer* renderer, int column, int row, float x, float y) const;

    std::unique_ptr<SDL_Texture, TextureDeleter> atlas_;
    std::vector<Tile> tiles_;
    int width_ = 0;
    int height_ = 0;
    std::uint32_t seed_ = 0;
};
