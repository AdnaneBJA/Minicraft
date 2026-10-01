#pragma once

#include "texture.h"

#include <SDL3/SDL.h>

#include <cstdint>
#include <string>
#include <vector>

class Font;
class Hud;

// The menus around the game, modelled on Minicraft+'s displays: the title screen (Play / Quit), the Play choice
// (Load World / New World), world creation (name + seed), world selection, and the in-game pause menu.
// The menu only collects choices; the game acts on the returned Action.
class GameMenu {
public:
    enum class Screen { None, Title, Play, NewWorld, LoadWorld, Pause };

    struct Action {
        enum class Kind { None, CreateWorld, LoadWorld, Resume, Save, SaveAndQuit, Quit };
        Kind kind = Kind::None;
        std::string worldName;   // CreateWorld, LoadWorld
        std::uint32_t seed = 0;  // CreateWorld
    };

    bool load(SDL_Renderer* renderer, const std::string& logoPath);

    // Opens the title screen. `worlds` are the saved world names, most recent first.
    void openTitle(std::vector<std::string> worlds);
    void openPause();
    void close() { open(Screen::None); }
    bool isOpen() const { return screen_ != Screen::None; }
    // True while a text field (world name or seed) is selected, so the game turns on SDL text input.
    bool wantsTextInput() const;

    // Key repeats only scroll and delete; they never select.
    Action handleKey(SDL_Keycode key, bool repeat);
    // Typed text (UTF-8) for the selected text field; characters a field doesn't accept are dropped.
    void handleText(const char* text);
    // A short-lived line of text: an error under the world list, or "World saved!" under the pause menu.
    void showMessage(std::string message, SDL_Color color);

    void update(float dt);
    // Draws in view pixels (the world render scale), over whatever is already on screen.
    void draw(SDL_Renderer* renderer, const Hud& hud, const Font& font, float viewWidth, float viewHeight) const;

private:
    void open(Screen screen);
    int entryCount() const;
    Action select();
    // Why the typed name can't be used yet, or empty if it can.
    std::string nameProblem() const;

    void drawTitle(SDL_Renderer* renderer, const Font& font, float viewWidth, float viewHeight) const;
    void drawNewWorld(SDL_Renderer* renderer, const Font& font, float viewWidth, float viewHeight) const;
    void drawLoadWorld(SDL_Renderer* renderer, const Font& font, float viewWidth, float viewHeight) const;
    void drawPause(SDL_Renderer* renderer, const Hud& hud, const Font& font, float viewWidth, float viewHeight) const;

    TexturePtr logo_;
    float logoWidth_ = 0.0f;
    float logoHeight_ = 0.0f;
    Screen screen_ = Screen::None;
    int selected_ = 0;
    int listOffset_ = 0;  // first world shown in the scrolling world list
    std::vector<std::string> worlds_;
    std::string name_;  // world creation fields
    std::string seed_;
    std::string message_;
    SDL_Color messageColor_{255, 255, 255, 255};
    float messageTimer_ = 0.0f;
    std::size_t splash_ = 0;
    float tickAccumulator_ = 0.0f;
    int ticks_ = 0;  // 60 Hz ticks; drive the splash pulse and the text caret blink
};
