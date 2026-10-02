#pragma once

#include "texture.h"

#include <SDL3/SDL.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

class Audio;
class Font;
class Hud;

// The menus around the game, modelled on Minicraft+'s displays: the title screen (Play / Multiplayer / Options /
// Quit), the Play choice (Load World / New World), world creation (name + seed), world selection, the multiplayer
// screens (name and server address, connecting, the lobby list), the options (sound and volume), the in-game pause
// menu, and the death and victory screens. The menu only collects choices; the game acts on the returned Action.
class GameMenu {
public:
    enum class Screen { None, Title, Play, NewWorld, LoadWorld, Connect, Connecting, Lobbies, Pause, Options, Dead, Won };

    // A lobby as the list shows it.
    struct LobbyEntry {
        int id;
        std::string label;  // "Alice's world (2)"
    };

    struct Action {
        enum class Kind {
            None, CreateWorld, LoadWorld, Resume, Save, SaveAndQuit, Quit, ToggleSound, VolumeDown, VolumeUp, Respawn,
            Connect,      // playerName, address
            Disconnect,   // back out of the multiplayer screens
            CreateLobby,
            JoinLobby,    // lobbyId
            LeaveGame,    // leave the multiplayer world (back to the lobby list)
        };
        Kind kind = Kind::None;
        std::string worldName;   // CreateWorld, LoadWorld
        std::uint32_t seed = 0;  // CreateWorld
        std::string playerName;  // Connect
        std::string address;     // Connect
        int lobbyId = 0;         // JoinLobby
    };

    bool load(SDL_Renderer* renderer, const std::string& logoPath);
    // Moving the cursor plays Minicraft's select sound and choosing an entry its confirm sound.
    void setAudio(Audio* audio) { audio_ = audio; }
    // What the options screen shows.
    void setSoundSettings(bool muted, int volume) {
        muted_ = muted;
        volume_ = volume;
    }

    // Opens the title screen. `worlds` are the saved world names, most recent first.
    void openTitle(std::vector<std::string> worlds);
    void openPause();
    // Multiplayer: name and server address.
    void openConnect() { open(Screen::Connect); }
    void openConnecting() { open(Screen::Connecting); }
    void openLobbies() { open(Screen::Lobbies); }
    void setLobbies(std::vector<LobbyEntry> lobbies);
    // In a multiplayer game the world never pauses, and the menus offer "Leave Game" instead of saving.
    void setOnline(bool online) { online_ = online; }
    // "You died! Aww :(" with the time survived (PlayerDeathDisplay).
    void openDeath(int secondsPlayed);
    // "You won! Yay :)" after beating the Air Wizard, with the time it took (EndGameDisplay).
    void openWon(int secondsPlayed);
    void close() { open(Screen::None); }
    bool isOpen() const { return screen_ != Screen::None; }
    Screen screen() const { return screen_; }
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
    void drawConnect(SDL_Renderer* renderer, const Font& font, float viewWidth, float viewHeight) const;
    void drawLobbies(SDL_Renderer* renderer, const Font& font, float viewWidth, float viewHeight) const;
    // A framed list of entries with a title in the top edge (the pause menu, options, death and victory screens).
    void drawFramedList(SDL_Renderer* renderer, const Hud& hud, const Font& font, std::string_view title,
                        const std::vector<std::string>& entries, const std::vector<std::string>& lines,
                        float viewWidth, float viewHeight) const;
    std::vector<std::string> entries() const;
    void play(bool confirm) const;

    Audio* audio_ = nullptr;
    bool muted_ = false;
    int volume_ = 0;
    Screen optionsReturn_ = Screen::Title;  // where Escape leaves the options screen to
    int secondsPlayed_ = 0;
    TexturePtr logo_;
    float logoWidth_ = 0.0f;
    float logoHeight_ = 0.0f;
    Screen screen_ = Screen::None;
    int selected_ = 0;
    int listOffset_ = 0;  // first world shown in the scrolling world list
    std::vector<std::string> worlds_;
    std::string name_;  // world creation fields
    std::string seed_;
    std::string playerName_ = "Player";  // multiplayer fields
    std::string serverAddress_ = "localhost";
    std::vector<LobbyEntry> lobbies_;
    bool online_ = false;
    std::string message_;
    SDL_Color messageColor_{255, 255, 255, 255};
    float messageTimer_ = 0.0f;
    std::size_t splash_ = 0;
    float tickAccumulator_ = 0.0f;
    int ticks_ = 0;  // 60 Hz ticks; drive the splash pulse and the text caret blink
};
