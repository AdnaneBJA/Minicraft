#include "game_menu.h"

#include "audio.h"
#include "font.h"
#include "hud.h"
#include "protocol.h"
#include "world_save.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <string_view>
#include <utility>

namespace {

constexpr float kCell = 8.0f;
constexpr float kRowHeight = 10.0f;  // 8 px line + 2 px spacing, like Minicraft+'s title menu
constexpr float kTick = 1.0f / 60.0f;
constexpr float kMessageSeconds = 2.0f;
constexpr int kVisibleWorlds = 5;
constexpr std::size_t kMaxSeedLength = 16;
constexpr const char* kCursorLeft = "> ";
constexpr const char* kCursorRight = " <";
constexpr const char* kNamePrompt = "Name: ";
constexpr const char* kSeedPrompt = "Seed: ";

// Minicraft+ colours: selected entries are white, the rest grey; text fields go green when valid, red when not.
constexpr SDL_Color kWhite{255, 255, 255, 255};
constexpr SDL_Color kGray{153, 153, 153, 255};
constexpr SDL_Color kDarkGray{102, 102, 102, 255};
constexpr SDL_Color kGreen{0, 255, 0, 255};
constexpr SDL_Color kRed{255, 0, 0, 255};
constexpr SDL_Color kCyan{0, 255, 255, 255};

constexpr std::array<std::string_view, 4> kTitleEntries{"Play", "Multiplayer", "Options", "Quit"};
constexpr std::array<std::string_view, 2> kPlayEntries{"Load World", "New World"};
constexpr std::array<std::string_view, 4> kPauseEntries{"Return to Game", "Options", "Save Game", "Save and Quit"};
constexpr int kSoundRow = 0;   // options screen rows
constexpr int kVolumeRow = 1;
constexpr int kNameRow = 0;
constexpr int kSeedRow = 1;
constexpr int kCreateRow = 2;
constexpr int kPlayerNameRow = 0;  // multiplayer connect screen rows
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

// Minicraft+'s WorldGenDisplay.getSeed: a number is used as is, any other text is hashed, and an empty field
// picks a random seed. Seeds here are 32-bit, so the result is truncated.
std::uint32_t seedFromText(std::string_view text) {
    if (text.empty()) return static_cast<std::uint32_t>(SDL_rand_bits());
    std::int64_t number = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), number);
    if (error == std::errc() && end == text.data() + text.size()) return static_cast<std::uint32_t>(number);
    std::uint64_t hash = 1125899906842597ULL;  // Minicraft's "rather large prime number"
    for (const char c : text) hash = 31 * hash + static_cast<unsigned char>(c);
    return static_cast<std::uint32_t>(hash);
}

}  // namespace

bool GameMenu::load(SDL_Renderer* renderer, const std::string& logoPath) {
    logo_ = loadTexture(renderer, logoPath);
    if (!logo_) return false;
    SDL_GetTextureSize(logo_.get(), &logoWidth_, &logoHeight_);
    return true;
}

void GameMenu::openTitle(std::vector<std::string> worlds) {
    worlds_ = std::move(worlds);
    splash_ = SDL_rand(static_cast<Sint32>(kSplashes.size()));
    open(Screen::Title);
}

void GameMenu::openPause() { open(Screen::Pause); }

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

void GameMenu::setLobbies(std::vector<LobbyEntry> lobbies) {
    lobbies_ = std::move(lobbies);
    selected_ = std::min(selected_, static_cast<int>(lobbies_.size()));  // row 0 is "Create new world"
}

std::vector<std::string> GameMenu::entries() const {
    switch (screen_) {
        case Screen::Pause:
            if (online_) return {"Return to Game", "Options", "Leave Game"};
            return {kPauseEntries.begin(), kPauseEntries.end()};
        case Screen::Options:
            return {std::string("Sound: ") + (muted_ ? "Off" : "On"),
                    "Volume: < " + std::to_string(volume_ * 100 / Audio::kVolumeSteps) + "% >"};
        case Screen::Dead: return {"Respawn", online_ ? "Leave Game" : "Save and Quit"};
        case Screen::Won: return {"Continue", online_ ? "Leave Game" : "Save and Quit"};
        default: return {};
    }
}

void GameMenu::open(Screen screen) {
    screen_ = screen;
    selected_ = 0;
    listOffset_ = 0;
    message_.clear();
    if (screen == Screen::NewWorld) {
        name_.clear();
        seed_.clear();
    }
}

bool GameMenu::wantsTextInput() const {
    if (screen_ == Screen::Connect) return selected_ == kPlayerNameRow || (!fixedServer_ && selected_ == kAddressRow);
    return screen_ == Screen::NewWorld && (selected_ == kNameRow || selected_ == kSeedRow);
}

int GameMenu::entryCount() const {
    switch (screen_) {
        case Screen::Title: return titleEntryCount();
        case Screen::Play: return static_cast<int>(kPlayEntries.size());
        case Screen::NewWorld: return 3;
        case Screen::Connect: return connectRow() + 1;
        case Screen::Connecting: return 0;
        case Screen::Lobbies: return 1 + static_cast<int>(lobbies_.size());
        case Screen::LoadWorld: return static_cast<int>(worlds_.size());
        case Screen::Pause:
        case Screen::Options:
        case Screen::Dead:
        case Screen::Won: return static_cast<int>(entries().size());
        case Screen::None: return 0;
    }
    return 0;
}

GameMenu::Action GameMenu::handleKey(SDL_Keycode key, bool repeat) {
    // W/S move the cursor too, except on the screens where letters are typed.
    const bool typing = screen_ == Screen::NewWorld || screen_ == Screen::Connect;
    const bool up = key == SDLK_UP || (!typing && key == SDLK_W);
    const bool down = key == SDLK_DOWN || (!typing && key == SDLK_S);
    const int count = entryCount();
    if ((up || down) && count > 0) {
        selected_ = (selected_ + (up ? count - 1 : 1)) % count;  // wraps, like Minicraft
        play(false);
        if (screen_ == Screen::LoadWorld) {
            listOffset_ = std::clamp(listOffset_, selected_ - kVisibleWorlds + 1, selected_);
        }
        return {};
    }
    if (key == SDLK_BACKSPACE && wantsTextInput()) {
        std::string& field = screen_ == Screen::Connect ? (selected_ == kPlayerNameRow ? playerName_ : serverAddress_)
                                                        : (selected_ == kNameRow ? name_ : seed_);
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
            case Screen::Play:
            case Screen::NewWorld:
                // Back to where this screen was opened from (Play is skipped when there are no worlds yet).
                open(screen_ == Screen::NewWorld && !worlds_.empty() ? Screen::Play : Screen::Title);
                break;
            case Screen::LoadWorld: open(Screen::Play); break;
            case Screen::Connect: open(Screen::Title); break;
            case Screen::Connecting:
            case Screen::Lobbies: return {.kind = Action::Kind::Disconnect};
            case Screen::Pause: return {.kind = Action::Kind::Resume};
            case Screen::Options: open(optionsReturn_); break;
            case Screen::Title:
            case Screen::Dead:
            case Screen::Won:
            case Screen::None: break;
        }
    }
    return {};
}

GameMenu::Action GameMenu::select() {
    switch (screen_) {
        case Screen::Title:
            if (selected_ == 3) return {.kind = Action::Kind::Quit};
            if (selected_ == 2) {
                optionsReturn_ = Screen::Title;
                open(Screen::Options);
                break;
            }
            if (selected_ == 1) {
                open(Screen::Connect);
                break;
            }
            // Like Minicraft+: straight to world creation when there is nothing to load.
            open(worlds_.empty() ? Screen::NewWorld : Screen::Play);
            break;
        case Screen::Play: open(selected_ == 0 ? Screen::LoadWorld : Screen::NewWorld); break;
        case Screen::NewWorld:
            // Enter creates the world from any row, so the name can be typed and confirmed right away.
            if (!nameProblem().empty()) {
                selected_ = kNameRow;
                return {};
            }
            return {.kind = Action::Kind::CreateWorld, .worldName = name_, .seed = seedFromText(seed_)};
        case Screen::LoadWorld:
            if (worlds_.empty()) return {};
            return {.kind = Action::Kind::LoadWorld, .worldName = worlds_[static_cast<std::size_t>(selected_)]};
        case Screen::Connect:
            // Enter connects from any row, so the name and address can be typed and confirmed right away.
            if (!protocol::isValidName(playerName_)) {
                selected_ = kPlayerNameRow;
                showMessage("Name: 1-12 letters, digits, - or _", kRed);
                return {};
            }
            if (fixedServer_) return {.kind = Action::Kind::Connect, .playerName = playerName_, .address = *fixedServer_};
            if (serverAddress_.empty()) {
                selected_ = kAddressRow;
                return {};
            }
            return {.kind = Action::Kind::Connect, .playerName = playerName_, .address = serverAddress_};
        case Screen::Connecting: break;
        case Screen::Lobbies:
            if (selected_ == 0) return {.kind = Action::Kind::CreateLobby};
            return {.kind = Action::Kind::JoinLobby, .lobbyId = lobbies_[static_cast<std::size_t>(selected_ - 1)].id};
        case Screen::Pause:
            if (selected_ == 1) {
                optionsReturn_ = Screen::Pause;
                open(Screen::Options);
                return {};
            }
            if (selected_ == 0) return {.kind = Action::Kind::Resume};
            if (online_) return {.kind = Action::Kind::LeaveGame};
            return {.kind = selected_ == 2 ? Action::Kind::Save : Action::Kind::SaveAndQuit};
        case Screen::Options:
            if (selected_ == kSoundRow) return {.kind = Action::Kind::ToggleSound};
            return {};
        case Screen::Dead:
            if (selected_ == 0) return {.kind = Action::Kind::Respawn};
            return {.kind = online_ ? Action::Kind::LeaveGame : Action::Kind::SaveAndQuit};
        case Screen::Won:
            if (selected_ == 0) return {.kind = Action::Kind::Resume};
            return {.kind = online_ ? Action::Kind::LeaveGame : Action::Kind::SaveAndQuit};
        case Screen::None: break;
    }
    return {};
}

void GameMenu::handleText(const char* text) {
    if (!wantsTextInput()) return;
    if (screen_ == Screen::Connect) {
        // Player names: letters, digits, - and _. Addresses: a host name or IP, and maybe ":port".
        const bool isName = selected_ == kPlayerNameRow;
        std::string& field = isName ? playerName_ : serverAddress_;
        const std::size_t maxLength = isName ? static_cast<std::size_t>(protocol::kMaxNameLength) : kMaxAddressLength;
        for (const char* c = text; *c != '\0' && field.size() < maxLength; ++c) {
            const char ch = *c;
            const bool alnum = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
            if (alnum || ch == '-' || ch == '_' || (!isName && (ch == '.' || ch == ':'))) field.push_back(ch);
        }
        return;
    }
    const bool isName = selected_ == kNameRow;
    std::string& field = isName ? name_ : seed_;
    const std::size_t maxLength = isName ? WorldSaves::kMaxNameLength : kMaxSeedLength;
    for (const char* c = text; *c != '\0' && field.size() < maxLength; ++c) {
        const char ch = *c;
        const bool letter = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
        const bool digit = ch >= '0' && ch <= '9';
        if (isName) {
            // World names are lower case, like Minicraft+'s (they double as file names).
            if (letter || digit || ch == ' ' || ch == '-' || ch == '_') {
                field.push_back(static_cast<char>(ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch));
            }
        } else if (letter || digit || ch == '-') {
            field.push_back(ch);
        }
    }
}

std::string GameMenu::nameProblem() const {
    if (name_.empty()) return "Enter a world name";
    if (!WorldSaves::isValidName(name_)) return "Invalid world name";
    if (std::find(worlds_.begin(), worlds_.end(), name_) != worlds_.end()) return "Name already taken";
    return {};
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
        case Screen::Title: drawTitle(renderer, font, viewWidth, viewHeight); break;
        case Screen::Play:
            drawEntries(renderer, font, kPlayEntries, 0, static_cast<int>(kPlayEntries.size()), selected_, viewWidth,
                        viewHeight / 2.0f - kRowHeight);
            drawCentered(renderer, font, "(ESCAPE to return)", viewWidth, viewHeight - 10.0f, kDarkGray);
            break;
        case Screen::NewWorld: drawNewWorld(renderer, font, viewWidth, viewHeight); break;
        case Screen::LoadWorld: drawLoadWorld(renderer, font, viewWidth, viewHeight); break;
        case Screen::Connect: drawConnect(renderer, font, viewWidth, viewHeight); break;
        case Screen::Connecting:
            drawCentered(renderer, font, "Connecting to " + serverAddress_ + "...", viewWidth, viewHeight / 2.0f - 4.0f,
                         kWhite);
            drawCentered(renderer, font, "(ESCAPE to cancel)", viewWidth, viewHeight - 10.0f, kDarkGray);
            break;
        case Screen::Lobbies: drawLobbies(renderer, font, viewWidth, viewHeight); break;
        case Screen::Pause: drawPause(renderer, hud, font, viewWidth, viewHeight); break;
        case Screen::Options:
            drawFramedList(renderer, hud, font, "Options", entries(), {}, viewWidth, viewHeight);
            if (optionsReturn_ == Screen::Title) {  // in game, the status bar is down there
                drawCentered(renderer, font, "(M mutes in game)", viewWidth, viewHeight - 20.0f, kDarkGray);
                drawCentered(renderer, font, "(ESCAPE to return)", viewWidth, viewHeight - 10.0f, kDarkGray);
            }
            break;
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

void GameMenu::drawTitle(SDL_Renderer* renderer, const Font& font, float viewWidth, float viewHeight) const {
    // Layout from Minicraft+'s TitleDisplay: logo near the top, a pulsing splash under it, the menu at 3/5 height
    // and the controls at the bottom.
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

    drawEntries(renderer, font, kTitleEntries, 0, titleEntryCount(), selected_, viewWidth,
                std::floor(viewHeight * 3.0f / 5.0f) - kRowHeight);
    drawCentered(renderer, font, "(UP, DOWN to select)", viewWidth, viewHeight - 20.0f, kDarkGray);
    drawCentered(renderer, font, "(ENTER to accept)", viewWidth, viewHeight - 10.0f, kDarkGray);
}

void GameMenu::drawNewWorld(SDL_Renderer* renderer, const Font& font, float viewWidth, float viewHeight) const {
    drawCentered(renderer, font, "World Gen Options", viewWidth, 8.0f, kWhite);

    // Rows are left-aligned on a fixed column wide enough for a full name, so typing doesn't shift them.
    const float columnWidth = Font::textWidth(kNamePrompt) + static_cast<float>(WorldSaves::kMaxNameLength + 1) * kCell;
    const float left = std::floor((viewWidth - columnWidth) / 2.0f);
    const float top = std::floor(viewHeight / 2.0f) - 20.0f;
    const bool caretOn = (ticks_ / 30) % 2 == 0;
    const std::string problem = nameProblem();

    const auto drawRow = [&](int row, const std::string& text, SDL_Color color) {
        const float y = top + static_cast<float>(row) * kRowHeight;
        font.draw(renderer, text, left, y, color);
        if (row != selected_) return;
        const bool isField = row != kCreateRow;
        if (isField && caretOn) font.draw(renderer, "_", left + Font::textWidth(text), y, color);
        font.draw(renderer, kCursorLeft, left - Font::textWidth(kCursorLeft), y, kWhite);
        font.draw(renderer, kCursorRight, left + Font::textWidth(text) + (isField ? kCell : 0.0f), y, kWhite);
    };
    const SDL_Color nameColor = !problem.empty() ? kRed : selected_ == kNameRow ? kGreen : kGray;
    drawRow(kNameRow, kNamePrompt + name_, nameColor);
    if (seed_.empty() && selected_ != kSeedRow) {
        drawRow(kSeedRow, std::string(kSeedPrompt) + "random", kDarkGray);
    } else {
        drawRow(kSeedRow, kSeedPrompt + seed_, selected_ == kSeedRow ? kGreen : kGray);
    }
    drawRow(kCreateRow, "Create World", kCyan);

    if (!problem.empty()) drawCentered(renderer, font, problem, viewWidth, top + 4.0f * kRowHeight, kRed);
    drawCentered(renderer, font, "(ENTER to create)", viewWidth, viewHeight - 20.0f, kDarkGray);
    drawCentered(renderer, font, "(ESCAPE to return)", viewWidth, viewHeight - 10.0f, kDarkGray);
}

void GameMenu::drawLoadWorld(SDL_Renderer* renderer, const Font& font, float viewWidth, float viewHeight) const {
    drawCentered(renderer, font, "Select World", viewWidth, 8.0f, kWhite);
    // Up to five worlds at a time, scrolled to keep the selection visible (WorldSelectDisplay).
    const int shown = std::min(kVisibleWorlds, static_cast<int>(worlds_.size()));
    const float top = std::floor(viewHeight / 2.0f - static_cast<float>(shown) * kRowHeight / 2.0f);
    drawEntries(renderer, font, worlds_, listOffset_, shown, selected_, viewWidth, top);
    if (listOffset_ > 0) drawCentered(renderer, font, "...", viewWidth, top - kRowHeight, kDarkGray);
    if (listOffset_ + shown < static_cast<int>(worlds_.size())) {
        drawCentered(renderer, font, "...", viewWidth, top + static_cast<float>(shown) * kRowHeight, kDarkGray);
    }
    if (!message_.empty()) drawCentered(renderer, font, message_, viewWidth, viewHeight - 34.0f, messageColor_);
    drawCentered(renderer, font, "(ENTER to confirm)", viewWidth, viewHeight - 20.0f, kDarkGray);
    drawCentered(renderer, font, "(ESCAPE to return)", viewWidth, viewHeight - 10.0f, kDarkGray);
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

void GameMenu::drawConnect(SDL_Renderer* renderer, const Font& font, float viewWidth, float viewHeight) const {
    drawCentered(renderer, font, "Multiplayer", viewWidth, 8.0f, kWhite);
    // Same look as world creation: left-aligned rows on a fixed column, the selected field green with a caret.
    const float columnWidth = Font::textWidth("Server: ") + static_cast<float>(kMaxAddressLength / 2 + 1) * kCell;
    const float left = std::floor((viewWidth - columnWidth) / 2.0f);
    const float top = std::floor(viewHeight / 2.0f) - 20.0f;
    const bool caretOn = (ticks_ / 30) % 2 == 0;
    const auto drawRow = [&](int row, const std::string& text, SDL_Color color) {
        const float y = top + static_cast<float>(row) * kRowHeight;
        font.draw(renderer, text, left, y, color);
        if (row != selected_) return;
        const bool isField = row != connectRow();
        if (isField && caretOn) font.draw(renderer, "_", left + Font::textWidth(text), y, color);
        font.draw(renderer, kCursorLeft, left - Font::textWidth(kCursorLeft), y, kWhite);
        font.draw(renderer, kCursorRight, left + Font::textWidth(text) + (isField ? kCell : 0.0f), y, kWhite);
    };
    drawRow(kPlayerNameRow, "Name: " + playerName_, selected_ == kPlayerNameRow ? kGreen : kGray);
    if (!fixedServer_) drawRow(kAddressRow, "Server: " + serverAddress_, selected_ == kAddressRow ? kGreen : kGray);
    drawRow(connectRow(), "Connect", kCyan);
    if (!message_.empty()) drawCentered(renderer, font, message_, viewWidth, top + 4.0f * kRowHeight, messageColor_);
    drawCentered(renderer, font, "(ENTER to connect)", viewWidth, viewHeight - 20.0f, kDarkGray);
    drawCentered(renderer, font, "(ESCAPE to return)", viewWidth, viewHeight - 10.0f, kDarkGray);
}

void GameMenu::drawLobbies(SDL_Renderer* renderer, const Font& font, float viewWidth, float viewHeight) const {
    drawCentered(renderer, font, "Lobbies", viewWidth, 8.0f, kWhite);
    std::vector<std::string> rows{"Create new world"};
    for (const LobbyEntry& lobby : lobbies_) rows.push_back(lobby.label);
    const int shown = std::min(kVisibleWorlds + 1, static_cast<int>(rows.size()));
    const int first = std::clamp(selected_ - shown + 1, 0, std::max(0, static_cast<int>(rows.size()) - shown));
    drawEntries(renderer, font, rows, first, shown, selected_, viewWidth, 28.0f);
    if (lobbies_.empty()) {
        drawCentered(renderer, font, "No one is playing yet", viewWidth, 28.0f + 2.0f * kRowHeight, kDarkGray);
    }
    if (!message_.empty()) drawCentered(renderer, font, message_, viewWidth, viewHeight - 34.0f, messageColor_);
    drawCentered(renderer, font, "(ENTER to join)", viewWidth, viewHeight - 20.0f, kDarkGray);
    drawCentered(renderer, font, "(ESCAPE to disconnect)", viewWidth, viewHeight - 10.0f, kDarkGray);
}

void GameMenu::drawPause(SDL_Renderer* renderer, const Hud& hud, const Font& font, float viewWidth,
                         float viewHeight) const {
    // A framed list over the world, entries on every other row (blank rows above and below too), with the title set
    // into the top edge. In multiplayer the world keeps going underneath, so it's just the "Menu".
    const std::vector<std::string> list = entries();
    std::size_t longest = 0;
    for (const auto& entry : list) longest = std::max(longest, entry.size());
    const int columns = static_cast<int>(longest) + 4;  // room for "> " and " <"
    const int rows = static_cast<int>(list.size()) * 2 + 1;
    const float interiorLeft = std::floor((viewWidth - static_cast<float>(columns) * kCell) / 2.0f);
    const float interiorTop = std::floor((viewHeight - static_cast<float>(rows) * kCell) / 2.0f);
    hud.drawFrame(renderer, interiorLeft, interiorTop, columns, rows);

    const std::string_view title = online_ ? "Menu" : "Paused";
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
