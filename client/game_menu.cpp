#include "game_menu.h"

#include "audio.h"
#include "font.h"
#include "hud.h"
#include "protocol.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>
#include <utility>

namespace {

constexpr float kCell = 8.0f;
constexpr float kRowHeight = 10.0f;  // 8 px line + 2 px spacing, like Minicraft+'s title menu
constexpr float kTick = 1.0f / 60.0f;
constexpr float kMessageSeconds = 4.0f;
constexpr const char* kCursorLeft = "> ";
constexpr const char* kCursorRight = " <";

// Minicraft+ colours: selected entries are white, the rest grey; text fields go green while selected.
constexpr SDL_Color kWhite{255, 255, 255, 255};
constexpr SDL_Color kGray{153, 153, 153, 255};
constexpr SDL_Color kDarkGray{102, 102, 102, 255};
constexpr SDL_Color kGreen{0, 255, 0, 255};
constexpr SDL_Color kRed{255, 0, 0, 255};
constexpr SDL_Color kCyan{0, 255, 255, 255};

constexpr int kSoundRow = 0;  // options screen rows
constexpr int kVolumeRow = 1;
constexpr int kNameRow = 0;     // start screen rows
constexpr int kAddressRow = 1;  // only without a fixed server
constexpr std::size_t kMaxAddressLength = 40;

// A few of Minicraft's title splashes that fit this game.
constexpr std::array<std::string_view, 16> kSplashes{
    "Get him, Paul!",    "Keep calm!",          "Forty-Two!",       "Punch the Moon!",
    "Notch is Awesome!", "Sleep at Night!",     "Grab your friends!", "Mouse not included!",
    "2.5D FTW!",         "Zombies included!",   "Story? Uhh...",    "3rd dimension not included!",
    "Awesome!",          "Radical!",            "...zzz...",        "Infinite terrain? What's that?"};

float centeredX(std::string_view text, float viewWidth) {
    return std::floor((viewWidth - Font::textWidth(text)) / 2.0f);
}

void drawCentered(SDL_Renderer* renderer, const Font& font, std::string_view text, float viewWidth, float y,
                  SDL_Color color) {
    font.draw(renderer, text, centeredX(text, viewWidth), std::floor(y), color);
}

// Minicraft+'s Menu.render: each entry centred on its row; the selected one white with "> <" around it.
template <typename Entries>
void drawEntries(SDL_Renderer* renderer, const Font& font, const Entries& entries, int first, int count, int selected,
                 float viewWidth, float top) {
    for (int i = 0; i < count; ++i) {
        const int index = first + i;
        const std::string_view text = entries[static_cast<std::size_t>(index)];
        const float x = centeredX(text, viewWidth);
        const float y = std::floor(top + static_cast<float>(i) * kRowHeight);
        const bool isSelected = index == selected;
        font.draw(renderer, text, x, y, isSelected ? kWhite : kGray);
        if (isSelected) {
            font.draw(renderer, kCursorLeft, x - Font::textWidth(kCursorLeft), y, kWhite);
            font.draw(renderer, kCursorRight, x + Font::textWidth(text), y, kWhite);
        }
    }
}

}  // namespace

bool GameMenu::load(SDL_Renderer* renderer, const std::string& logoPath) {
    logo_ = loadTexture(renderer, logoPath);
    if (!logo_) return false;
    SDL_GetTextureSize(logo_.get(), &logoWidth_, &logoHeight_);
    return true;
}

void GameMenu::openConnect() {
    splash_ = SDL_rand(static_cast<Sint32>(kSplashes.size()));
    open(Screen::Connect);
    selected_ = playRow();  // the name is usually already there: Enter plays
    if (!protocol::isValidName(playerName_)) selected_ = kNameRow;
}

void GameMenu::openDeath(int secondsPlayed) {
    secondsPlayed_ = secondsPlayed;
    open(Screen::Dead);
}

void GameMenu::openWon(int secondsPlayed) {
    secondsPlayed_ = secondsPlayed;
    open(Screen::Won);
}

void GameMenu::play(bool confirm) const {
    if (audio_) audio_->play(confirm ? Sound::Confirm : Sound::Select);
}

std::vector<std::string> GameMenu::entries() const {
    switch (screen_) {
        case Screen::Pause: return {"Return to Game", "Options", "Leave Game"};
        case Screen::Options:
            return {std::string("Sound: ") + (muted_ ? "Off" : "On"),
                    "Volume: < " + std::to_string(volume_ * 100 / Audio::kVolumeSteps) + "% >"};
        case Screen::Dead: return {"Respawn", "Leave Game"};
        case Screen::Won: return {"Continue", "Leave Game"};
        default: return {};
    }
}

void GameMenu::open(Screen screen) {
    screen_ = screen;
    selected_ = 0;
    message_.clear();
}

bool GameMenu::wantsTextInput() const {
    return screen_ == Screen::Connect && (selected_ == kNameRow || (!fixedServer_ && selected_ == kAddressRow));
}

int GameMenu::entryCount() const {
    switch (screen_) {
        case Screen::Connect: return playRow() + 1;
        case Screen::Pause:
        case Screen::Options:
        case Screen::Dead:
        case Screen::Won: return static_cast<int>(entries().size());
        case Screen::Connecting:
        case Screen::None: return 0;
    }
    return 0;
}

GameMenu::Action GameMenu::handleKey(SDL_Keycode key, bool repeat) {
    // W/S move the cursor too, except on the start screen where letters are typed.
    const bool typing = screen_ == Screen::Connect;
    const bool up = key == SDLK_UP || (!typing && key == SDLK_W);
    const bool down = key == SDLK_DOWN || (!typing && key == SDLK_S);
    const int count = entryCount();
    if ((up || down) && count > 0) {
        selected_ = (selected_ + (up ? count - 1 : 1)) % count;  // wraps, like Minicraft
        play(false);
        return {};
    }
    if (key == SDLK_BACKSPACE && wantsTextInput()) {
        std::string& field = selected_ == kNameRow ? playerName_ : serverAddress_;
        if (!field.empty()) field.pop_back();
        return {};
    }
    // Left and right change the volume on the options screen (repeats included, so holding scrolls).
    if (screen_ == Screen::Options && selected_ == kVolumeRow && (key == SDLK_LEFT || key == SDLK_A ||
                                                                  key == SDLK_RIGHT || key == SDLK_D)) {
        play(false);
        const bool louder = key == SDLK_RIGHT || key == SDLK_D;
        return {.kind = louder ? Action::Kind::VolumeUp : Action::Kind::VolumeDown};
    }
    if (repeat) return {};

    if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
        play(true);
        return select();
    }
    if (key == SDLK_ESCAPE) {
        switch (screen_) {
            case Screen::Connecting: return {.kind = Action::Kind::LeaveGame};
            case Screen::Pause: return {.kind = Action::Kind::Resume};
            case Screen::Options: open(Screen::Pause); break;
            case Screen::Connect:  // the start screen: there's nothing to go back to
            case Screen::Dead:
            case Screen::Won:
            case Screen::None: break;
        }
    }
    return {};
}

GameMenu::Action GameMenu::select() {
    switch (screen_) {
        case Screen::Connect:
            // Enter plays from any row, so the name can be typed and confirmed right away.
            if (!protocol::isValidName(playerName_)) {
                selected_ = kNameRow;
                showMessage("Name: 1-12 letters, digits, - or _", kRed);
                return {};
            }
            if (fixedServer_) return {.kind = Action::Kind::Connect, .playerName = playerName_, .address = *fixedServer_};
            if (serverAddress_.empty()) {
                selected_ = kAddressRow;
                return {};
            }
            return {.kind = Action::Kind::Connect, .playerName = playerName_, .address = serverAddress_};
        case Screen::Pause:
            if (selected_ == 1) {
                open(Screen::Options);
                return {};
            }
            if (selected_ == 0) return {.kind = Action::Kind::Resume};
            return {.kind = Action::Kind::LeaveGame};
        case Screen::Options:
            if (selected_ == kSoundRow) return {.kind = Action::Kind::ToggleSound};
            return {};
        case Screen::Dead:
            if (selected_ == 0) return {.kind = Action::Kind::Respawn};
            return {.kind = Action::Kind::LeaveGame};
        case Screen::Won:
            if (selected_ == 0) return {.kind = Action::Kind::Resume};
            return {.kind = Action::Kind::LeaveGame};
        case Screen::Connecting:
        case Screen::None: break;
    }
    return {};
}

void GameMenu::handleText(const char* text) {
    if (!wantsTextInput()) return;
    // Player names: letters, digits, - and _. Addresses: a host name or IP, and maybe ":port".
    const bool isName = selected_ == kNameRow;
    std::string& field = isName ? playerName_ : serverAddress_;
    const std::size_t maxLength = isName ? static_cast<std::size_t>(protocol::kMaxNameLength) : kMaxAddressLength;
    for (const char* c = text; *c != '\0' && field.size() < maxLength; ++c) {
        const char ch = *c;
        const bool alnum = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
        if (alnum || ch == '-' || ch == '_' || (!isName && (ch == '.' || ch == ':' || ch == '/'))) field.push_back(ch);
    }
}

void GameMenu::showMessage(std::string message, SDL_Color color) {
    message_ = std::move(message);
    messageColor_ = color;
    messageTimer_ = kMessageSeconds;
}

void GameMenu::update(float dt) {
    tickAccumulator_ += dt;
    while (tickAccumulator_ >= kTick) {
        tickAccumulator_ -= kTick;
        ++ticks_;
    }
    if (messageTimer_ > 0.0f) {
        messageTimer_ -= dt;
        if (messageTimer_ <= 0.0f) message_.clear();
    }
}

void GameMenu::draw(SDL_Renderer* renderer, const Hud& hud, const Font& font, float viewWidth,
                    float viewHeight) const {
    switch (screen_) {
        case Screen::Connect: drawConnect(renderer, font, viewWidth, viewHeight); break;
        case Screen::Connecting:
            drawCentered(renderer, font, "Connecting...", viewWidth, viewHeight / 2.0f - 4.0f, kWhite);
            drawCentered(renderer, font, "(ESCAPE to cancel)", viewWidth, viewHeight - 10.0f, kDarkGray);
            break;
        case Screen::Pause: drawPause(renderer, hud, font, viewWidth, viewHeight); break;
        case Screen::Options: drawFramedList(renderer, hud, font, "Options", entries(), {}, viewWidth, viewHeight); break;
        case Screen::Dead:
        case Screen::Won: {
            const bool won = screen_ == Screen::Won;
            const std::string time = std::to_string(secondsPlayed_ / 3600) + "h " +
                                     std::to_string(secondsPlayed_ / 60 % 60) + "m " +
                                     std::to_string(secondsPlayed_ % 60) + "s";
            drawFramedList(renderer, hud, font, won ? "You won! Yay :)" : "You died! Aww :(", entries(),
                           {(won ? "Time: " : "Time survived: ") + time}, viewWidth, viewHeight);
            break;
        }
        case Screen::None: break;
    }
}

void GameMenu::drawConnect(SDL_Renderer* renderer, const Font& font, float viewWidth, float viewHeight) const {
    // Minicraft+'s TitleDisplay up top: the logo, and a pulsing splash under it.
    const float middle = std::floor(viewHeight / 2.0f);
    const SDL_FRect logo{std::floor((viewWidth - logoWidth_) / 2.0f), middle - 52.0f, logoWidth_, logoHeight_};
    SDL_RenderTexture(renderer, logo_.get(), nullptr, &logo);
    // The splash brightness swings over 50 ticks: count goes 0 -> 25 -> 0, colour level 5 -> 1 -> 5.
    const int phase = ticks_ % 50;
    const int count = phase <= 25 ? phase : 50 - phase;
    const int level = 5 - count / 5;
    const SDL_Color splashColor{static_cast<Uint8>(level * 51), static_cast<Uint8>(level * 51),
                                static_cast<Uint8>(level * 25), 255};
    drawCentered(renderer, font, kSplashes[splash_], viewWidth, middle - 30.0f, splashColor);

    // Left-aligned rows on a fixed column, the selected field green with a caret.
    const std::size_t widest = fixedServer_ ? static_cast<std::size_t>(protocol::kMaxNameLength) : kMaxAddressLength / 2;
    const float columnWidth = Font::textWidth(fixedServer_ ? "Name: " : "Server: ") + static_cast<float>(widest + 1) * kCell;
    const float left = std::floor((viewWidth - columnWidth) / 2.0f);
    const float top = middle;
    const bool caretOn = (ticks_ / 30) % 2 == 0;
    const auto drawRow = [&](int row, const std::string& text, SDL_Color color) {
        const float y = top + static_cast<float>(row) * kRowHeight;
        font.draw(renderer, text, left, y, color);
        if (row != selected_) return;
        const bool isField = row != playRow();
        if (isField && caretOn) font.draw(renderer, "_", left + Font::textWidth(text), y, color);
        font.draw(renderer, kCursorLeft, left - Font::textWidth(kCursorLeft), y, kWhite);
        font.draw(renderer, kCursorRight, left + Font::textWidth(text) + (isField ? kCell : 0.0f), y, kWhite);
    };
    drawRow(kNameRow, "Name: " + playerName_, selected_ == kNameRow ? kGreen : kGray);
    if (!fixedServer_) drawRow(kAddressRow, "Server: " + serverAddress_, selected_ == kAddressRow ? kGreen : kGray);
    drawRow(playRow(), "Play", kCyan);
    if (!message_.empty()) {
        drawCentered(renderer, font, message_, viewWidth, top + static_cast<float>(playRow() + 2) * kRowHeight,
                     messageColor_);
    }
    drawCentered(renderer, font, "(ENTER to play)", viewWidth, viewHeight - 10.0f, kDarkGray);
}

void GameMenu::drawFramedList(SDL_Renderer* renderer, const Hud& hud, const Font& font, std::string_view title,
                              const std::vector<std::string>& list, const std::vector<std::string>& lines,
                              float viewWidth, float viewHeight) const {
    // Info lines first, then a blank row, then the entries on every other row.
    std::size_t longest = title.size();
    for (const auto& entry : list) longest = std::max(longest, entry.size() + 4);
    for (const auto& line : lines) longest = std::max(longest, line.size() + 2);
    const int columns = static_cast<int>(longest);
    const int infoRows = lines.empty() ? 0 : static_cast<int>(lines.size()) + 1;
    const int rows = infoRows + static_cast<int>(list.size()) * 2 + 1;
    const float interiorLeft = std::floor((viewWidth - static_cast<float>(columns) * kCell) / 2.0f);
    const float interiorTop = std::floor((viewHeight - static_cast<float>(rows) * kCell) / 2.0f);
    hud.drawFrame(renderer, interiorLeft, interiorTop, columns, rows);
    hud.drawTitle(renderer, font, title, centeredX(title, viewWidth), interiorTop - kCell);
    for (std::size_t i = 0; i < lines.size(); ++i) {
        drawCentered(renderer, font, lines[i], viewWidth, interiorTop + static_cast<float>(i + 1) * kCell, kWhite);
    }
    for (int i = 0; i < static_cast<int>(list.size()); ++i) {
        drawEntries(renderer, font, list, i, 1, selected_, viewWidth,
                    interiorTop + static_cast<float>(infoRows + i * 2 + 1) * kCell);
    }
}

void GameMenu::drawPause(SDL_Renderer* renderer, const Hud& hud, const Font& font, float viewWidth,
                         float viewHeight) const {
    // A framed list over the world, entries on every other row (blank rows above and below too), with the title set
    // into the top edge. The world keeps going underneath, so it's just the "Menu".
    const std::vector<std::string> list = entries();
    std::size_t longest = 0;
    for (const auto& entry : list) longest = std::max(longest, entry.size());
    const int columns = static_cast<int>(longest) + 4;  // room for "> " and " <"
    const int rows = static_cast<int>(list.size()) * 2 + 1;
    const float interiorLeft = std::floor((viewWidth - static_cast<float>(columns) * kCell) / 2.0f);
    const float interiorTop = std::floor((viewHeight - static_cast<float>(rows) * kCell) / 2.0f);
    hud.drawFrame(renderer, interiorLeft, interiorTop, columns, rows);

    const std::string_view title = "Menu";
    hud.drawTitle(renderer, font, title, centeredX(title, viewWidth), interiorTop - kCell);

    for (int i = 0; i < static_cast<int>(list.size()); ++i) {
        // drawEntries spaces rows by kRowHeight; the frame needs whole cells, so draw one entry at a time.
        drawEntries(renderer, font, list, i, 1, selected_, viewWidth, interiorTop + static_cast<float>(i * 2 + 1) * kCell);
    }
    if (!message_.empty()) {
        drawCentered(renderer, font, message_, viewWidth, interiorTop + static_cast<float>(rows + 2) * kCell,
                     messageColor_);
    }
}
