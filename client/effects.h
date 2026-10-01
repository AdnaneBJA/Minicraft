#pragma once

#include <SDL3/SDL.h>

#include <memory>
#include <string>
#include <vector>

class Camera;

// Short-lived visual effects drawn over the world, like the "smash" X shown when a punch hits a tree.
class Effects {
public:
    bool load(SDL_Renderer* renderer, const std::string& smashPath);

    // Shows the smash X over a tile for a moment.
    void addSmash(int tx, int ty);
    void update(float dt);
    void draw(SDL_Renderer* renderer, const Camera& camera) const;
    void clear() { smashes_.clear(); }

private:
    struct Smash {
        float x;
        float y;
        float timeLeft;
    };
    struct TextureDeleter {
        void operator()(SDL_Texture* texture) const { SDL_DestroyTexture(texture); }
    };

    std::unique_ptr<SDL_Texture, TextureDeleter> smashTexture_;
    std::vector<Smash> smashes_;
};
