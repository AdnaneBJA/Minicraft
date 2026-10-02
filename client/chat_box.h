#pragma once

#include <SDL3/SDL.h>

#include <optional>
#include <string>
#include <vector>

class Font;

// Multiplayer chat: the last lines in the bottom-left corner (they fade after a few seconds), and a line to type
// in. Enter opens it, Enter sends, Escape closes.
class ChatBox {
public:
    static constexpr SDL_Keycode kOpenKey = SDLK_RETURN;

    void open() { open_ = true; }
    void close() {
        open_ = false;
        typing_.clear();
    }
    bool isOpen() const { return open_; }

    // A received line: "name: text", or a note from the server when `from` is empty.
    void add(const std::string& from, const std::string& text);
    // Typed characters go into the line being written.
    void handleText(const char* text);
    // Backspace deletes, Escape closes, Enter returns the line to send (and closes).
    std::optional<std::string> handleKey(SDL_Keycode key);

    void update(float dt);
    // Draws from the left edge, the newest line just above `bottom` (in the current render scale's pixels).
    void draw(SDL_Renderer* renderer, const Font& font, float bottom) const;
    void clear();

private:
    struct Line {
        std::string text;
        SDL_Color color;
        float age;  // seconds since it arrived
    };

    std::vector<Line> lines_;
    std::string typing_;
    bool open_ = false;
    float blink_ = 0.0f;
};
