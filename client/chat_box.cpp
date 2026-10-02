#include "chat_box.h"

#include "font.h"
#include "protocol.h"

#include <cmath>

namespace {

constexpr std::size_t kKeptLines = 50;
constexpr int kShownLines = 6;
constexpr float kFadeSeconds = 10.0f;  // a line stays on screen this long, unless the chat is open
constexpr float kLineHeight = 9.0f;
constexpr SDL_Color kWhite{255, 255, 255, 255};
constexpr SDL_Color kServerColor{255, 255, 85, 255};  // notes from the server: yellow

}  // namespace

void ChatBox::add(const std::string& from, const std::string& text) {
    if (from.empty()) lines_.push_back({text, kServerColor, 0.0f});
    else lines_.push_back({"<" + from + "> " + text, kWhite, 0.0f});
    if (lines_.size() > kKeptLines) lines_.erase(lines_.begin());
}

void ChatBox::handleText(const char* text) {
    if (!open_) return;
    for (const char* c = text; *c != '\0'; ++c) {
        if (*c >= 32 && *c < 127 && typing_.size() < static_cast<std::size_t>(protocol::kMaxChatLength)) {
            typing_.push_back(*c);
        }
    }
}

std::optional<std::string> ChatBox::handleKey(SDL_Keycode key) {
    if (key == SDLK_BACKSPACE && !typing_.empty()) typing_.pop_back();
    if (key == SDLK_ESCAPE) close();
    if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
        std::string line = protocol::cleanChat(typing_);
        close();
        if (!line.empty()) return line;
    }
    return std::nullopt;
}

void ChatBox::update(float dt) {
    for (Line& line : lines_) line.age += dt;
    blink_ += dt;
}

void ChatBox::draw(SDL_Renderer* renderer, const Font& font, float viewHeight) const {
    // Above the hearts and bolts (the bottom 3 rows of 8 px), newest at the bottom.
    float y = viewHeight - 3.0f * 8.0f - kLineHeight - 2.0f;
    if (open_) {
        const std::string prompt = "> " + typing_ + (std::fmod(blink_, 1.0f) < 0.5f ? "_" : "");
        const SDL_FRect background{0.0f, y - 1.0f, Font::textWidth(prompt) + 2.0f, kLineHeight};
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderFillRect(renderer, &background);
        font.draw(renderer, prompt, 1.0f, y, kWhite);
        y -= kLineHeight;
    }
    int shown = 0;
    for (auto it = lines_.rbegin(); it != lines_.rend() && shown < kShownLines; ++it, ++shown) {
        if (!open_ && it->age > kFadeSeconds) break;
        // A dark band behind each line so it reads on any ground.
        const SDL_FRect background{0.0f, y - 1.0f, Font::textWidth(it->text) + 2.0f, kLineHeight};
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 160);
        SDL_RenderFillRect(renderer, &background);
        font.drawShadowed(renderer, it->text, 1.0f, y, it->color);
        y -= kLineHeight;
    }
}

void ChatBox::clear() {
    lines_.clear();
    close();
}
