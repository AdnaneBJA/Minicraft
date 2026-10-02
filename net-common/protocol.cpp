#include "protocol.h"

#include <algorithm>

namespace protocol {

namespace {

constexpr int kMaxTurnsPerTick = kMaxPlayers * 2;  // players, plus some leaving in the same tick
constexpr int kMaxHistory = 60 * 60 * 60 * 6;      // 6 hours of ticks
constexpr int kCommandKinds = static_cast<int>(PlayerCommand::Kind::Transfer) + 1;

// Keys as bits: move left/right/up/down, attack held, attack pressed.
std::uint8_t packInput(const PlayerInput& input) {
    std::uint8_t bits = 0;
    if (input.moveX < 0) bits |= 1;
    if (input.moveX > 0) bits |= 2;
    if (input.moveY < 0) bits |= 4;
    if (input.moveY > 0) bits |= 8;
    if (input.attack) bits |= 16;
    if (input.attackPressed) bits |= 32;
    return bits;
}

PlayerInput unpackInput(std::uint8_t bits) {
    PlayerInput input;
    input.moveX = (bits & 2 ? 1 : 0) - (bits & 1 ? 1 : 0);
    input.moveY = (bits & 8 ? 1 : 0) - (bits & 4 ? 1 : 0);
    input.attack = (bits & 16) != 0;
    input.attackPressed = (bits & 32) != 0;
    return input;
}

void writeCommand(ByteWriter& out, const PlayerCommand& command) {
    out.u8(static_cast<std::uint8_t>(command.kind));
    out.i32(command.a);
    out.i32(command.b);
    out.i32(command.c);
    out.i32(command.d);
    out.string(command.text);
}

PlayerCommand readCommand(ByteReader& in) {
    PlayerCommand command;
    const int kind = in.u8();
    if (kind >= kCommandKinds) {
        // Unknown command: reading the rest is pointless.
        in.count(-1);  // marks the reader as failed
        return command;
    }
    command.kind = static_cast<PlayerCommand::Kind>(kind);
    command.a = in.i32();
    command.b = in.i32();
    command.c = in.i32();
    command.d = in.i32();
    command.text = in.string(kMaxNameLength);
    return command;
}

void writeTick(ByteWriter& out, const TickInput& tick) {
    out.i32(tick.tick);
    out.i32(static_cast<std::int32_t>(tick.turns.size()));
    for (const PlayerTurn& turn : tick.turns) {
        out.i32(turn.playerId);
        out.u8(packInput(turn.input));
        out.i32(static_cast<std::int32_t>(turn.commands.size()));
        for (const PlayerCommand& command : turn.commands) writeCommand(out, command);
    }
}

TickInput readTick(ByteReader& in) {
    TickInput tick;
    tick.tick = in.i32();
    const int turns = in.count(kMaxTurnsPerTick);
    for (int i = 0; i < turns && in.ok(); ++i) {
        PlayerTurn turn;
        turn.playerId = in.i32();
        turn.input = unpackInput(in.u8());
        const int commands = in.count(kMaxCommandsPerTurn + 2);  // + the Join or Leave the server adds
        for (int c = 0; c < commands && in.ok(); ++c) turn.commands.push_back(readCommand(in));
        tick.turns.push_back(std::move(turn));
    }
    return tick;
}

}  // namespace

void InputMessage::write(ByteWriter& out) const { out.u8(packInput(input)); }
void InputMessage::read(ByteReader& in) { input = unpackInput(in.u8()); }

void CommandMessage::write(ByteWriter& out) const { writeCommand(out, command); }
void CommandMessage::read(ByteReader& in) { command = readCommand(in); }

void LobbyList::write(ByteWriter& out) const {
    out.i32(static_cast<std::int32_t>(lobbies.size()));
    for (const LobbyInfo& lobby : lobbies) {
        out.i32(lobby.id);
        out.string(lobby.name);
        out.i32(lobby.players);
    }
}

void LobbyList::read(ByteReader& in) {
    const int count = in.count(kMaxPlayers);
    for (int i = 0; i < count && in.ok(); ++i) {
        LobbyInfo lobby;
        lobby.id = in.i32();
        lobby.name = in.string(kMaxLobbyNameLength);
        lobby.players = in.i32();
        lobbies.push_back(std::move(lobby));
    }
}

void Joined::write(ByteWriter& out) const {
    out.i32(lobbyId);
    out.string(lobbyName);
    out.i32(static_cast<std::int32_t>(seed));
    out.i32(static_cast<std::int32_t>(history.size()));
    for (const TickInput& tick : history) writeTick(out, tick);
}

void Joined::read(ByteReader& in) {
    lobbyId = in.i32();
    lobbyName = in.string(kMaxLobbyNameLength);
    seed = static_cast<std::uint32_t>(in.i32());
    const int count = in.count(kMaxHistory);
    history.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count && in.ok(); ++i) history.push_back(readTick(in));
}

void TickMessage::write(ByteWriter& out) const { writeTick(out, input); }
void TickMessage::read(ByteReader& in) { input = readTick(in); }

std::optional<MessageType> typeOf(std::span<const std::uint8_t> bytes) {
    if (bytes.empty() || bytes[0] > static_cast<std::uint8_t>(MessageType::Error)) return std::nullopt;
    return static_cast<MessageType>(bytes[0]);
}

bool isValidName(const std::string& name) {
    if (name.empty() || name.size() > static_cast<std::size_t>(kMaxNameLength)) return false;
    return std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    });
}

std::string cleanChat(const std::string& text) {
    std::string clean;
    for (const char c : text) {
        if (c >= 32 && c < 127) clean.push_back(c);
    }
    const auto first = clean.find_first_not_of(' ');
    if (first == std::string::npos) return {};
    const auto last = clean.find_last_not_of(' ');
    return clean.substr(first, last - first + 1).substr(0, kMaxChatLength);
}

}  // namespace protocol
