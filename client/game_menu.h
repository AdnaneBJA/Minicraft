#pragma once

#include "texture.h"

#include <SDL3/SDL.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

class Audio;
class Font;
class Hud;

// The menus around the game, modelled on Minicraft+'s displays: the start screen (logo, a name, and the way into the
// server's world), connecting, the in-game menu (the world keeps going underneath), the options (sound and volume),
// and the death and victory screens. The menu only collects choices; the game acts on the returned Action.
class GameMenu {
public:
    enum class Screen { None, Connect, Connecting, Pause, Options, Dead, Won };

    struct Action {
        enum class Kind {
            None, Resume, Respawn, ToggleSound, VolumeDown, VolumeUp,
            Connect,    // playerName, address
            LeaveGame,  // leave the world, or stop connecting: back to the start screen
        };
        Kind kind = Kind::None;
        std::string playerName;  // Connect
        std::string address;     // Connect
    };

    bool load(SDL_Renderer* renderer, const std::string& logoPath);
    // Moving the cursor plays Minicraft's select sound and choosing an entry its confirm sound.
    void setAudio(Audio* audio) { audio_ = audio; }
    // What the options screen shows.
    void setSoundSettings(bool muted, int volume) {
        muted_ = muted;
        volume_ = volume;
    }

    // The start screen: a name (and, on the desktop, the server's address).
    void openConnect();
    void openConnecting() { open(Screen::Connecting); }
    void openPause() { open(Screen::Pause); }
    // The web version has one server: the start screen then only asks for a name.
    void setFixedServer(std::string url) { fixedServer_ = std::move(url); }
    // "You died! Aww :(" with the time survived (PlayerDeathDisplay).
    void openDeath(int secondsPlayed);
    // "You won! Yay :)" after beating the Air Wizard, with the time it took (EndGameDisplay).
    void openWon(int secondsPlayed);
    void close() { open(Screen::None); }
    bool isOpen() const { return screen_ != Screen::None; }
    Screen screen() const { return screen_; }
    // True while a text field is selected, so the game turns on SDL text input.
    bool wantsTextInput() const;

    // Key repeats only scroll and delete; they never select.
    Action handleKey(SDL_Keycode key, bool repeat);
    // Typed text (UTF-8) for the selected text field; characters a field doesn't accept are dropped.
    void handleText(const char* text);
    // A short-lived line of text: an error on the start screen, say.
    void showMessage(std::string message, SDL_Color color);

    void update(float dt);
    // Draws in view pixels (the world render scale), over whatever is already on screen.
    void draw(SDL_Renderer* renderer, const Hud& hud, const Font& font, float viewWidth, float viewHeight) const;

private:
    void open(Screen screen);
    int entryCount() const;
    Action select();
    // The start screen's rows: name, server address (unless the server is fixed), then "Play".
    int playRow() const { return fixedServer_ ? 1 : 2; }

    void drawConnect(SDL_Renderer* renderer, const Font& font, float viewWidth, float viewHeight) const;
    void drawPause(SDL_Renderer* renderer, const Hud& hud, const Font& font, float viewWidth, float viewHeight) const;
    // A framed list of entries with a title in the top edge (the menu, options, death and victory screens).
    void drawFramedList(SDL_Renderer* renderer, const Hud& hud, const Font& font, std::string_view title,
                        const std::vector<std::string>& entries, const std::vector<std::string>& lines,
                        float viewWidth, float viewHeight) const;
    std::vector<std::string> entries() const;
    void play(bool confirm) const;

    Audio* audio_ = nullptr;
    bool muted_ = false;
    int volume_ = 0;
    int secondsPlayed_ = 0;
    TexturePtr logo_;
    float logoWidth_ = 0.0f;
    float logoHeight_ = 0.0f;
    Screen screen_ = Screen::None;
    int selected_ = 0;
    std::string playerName_ = "Player";
    std::string serverAddress_ = "localhost";
    std::optional<std::string> fixedServer_;
    std::string message_;
    SDL_Color messageColor_{255, 255, 255, 255};
    float messageTimer_ = 0.0f;
    std::size_t splash_ = 0;
    float tickAccumulator_ = 0.0f;
    int ticks_ = 0;  // 60 Hz ticks; drive the splash pulse and the text caret blink
};
