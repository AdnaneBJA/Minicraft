#pragma once

#include "bytes.h"
#include "tick_input.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

// The messages the client and minicraft-server exchange over WebSockets, and how they look as bytes.
//
// How multiplayer works: the server runs no game. Each lobby is one world, and every client in it runs the same
// Simulation. 60 times a second the server gathers what each player did (Input and Command messages) into one
// TickInput and sends it to everyone in the lobby (Tick). Because the simulation is deterministic, every client
// computes the same world from the same ticks. A player joining a running lobby gets the lobby's whole tick history
// (Joined) and replays it to catch up.
//
// Every message starts with one byte, its MessageType, and travels as one binary WebSocket message: reliable and
// in order.
namespace protocol {

constexpr std::uint16_t kDefaultPort = 7777;
constexpr int kMaxPlayers = 32;     // per server
constexpr int kMaxNameLength = 12;
constexpr int kMaxChatLength = 80;
constexpr int kMaxLobbyNameLength = 24;
constexpr int kMaxCommandsPerTurn = 16;

enum class MessageType : std::uint8_t {
    // Client -> server
    Hello,        // my name, sent right after connecting
    CreateLobby,  // start a new world and join it
    JoinLobby,
    LeaveLobby,
    Input,        // the keys I hold (sent whenever they change)
    Command,      // something I did once, usually from a menu
    Chat,
    StateHash,    // my world's fingerprint at a tick, to catch clients that went out of sync
    // Server -> client
    Welcome,      // your player id
    LobbyList,
    Joined,       // you're in a lobby: its seed and the history to replay
    Tick,         // what everyone in your lobby did during one tick
    ChatLine,     // a chat message (or a note from the server)
    Error,
};

// --- Client -> server

struct Hello {
    static constexpr MessageType kType = MessageType::Hello;
    std::string name;
    void write(ByteWriter& out) const { out.string(name); }
    void read(ByteReader& in) { name = in.string(kMaxNameLength); }
};

struct CreateLobby {
    static constexpr MessageType kType = MessageType::CreateLobby;
    void write(ByteWriter&) const {}
    void read(ByteReader&) {}
};

struct JoinLobby {
    static constexpr MessageType kType = MessageType::JoinLobby;
    int lobbyId = 0;
    void write(ByteWriter& out) const { out.i32(lobbyId); }
    void read(ByteReader& in) { lobbyId = in.i32(); }
};

struct LeaveLobby {
    static constexpr MessageType kType = MessageType::LeaveLobby;
    void write(ByteWriter&) const {}
    void read(ByteReader&) {}
};

struct InputMessage {
    static constexpr MessageType kType = MessageType::Input;
    PlayerInput input;
    void write(ByteWriter& out) const;
    void read(ByteReader& in);
};

struct CommandMessage {
    static constexpr MessageType kType = MessageType::Command;
    PlayerCommand command;
    void write(ByteWriter& out) const;
    void read(ByteReader& in);
};

struct ChatMessage {
    static constexpr MessageType kType = MessageType::Chat;
    std::string text;
    void write(ByteWriter& out) const { out.string(text); }
    void read(ByteReader& in) { text = in.string(kMaxChatLength); }
};

struct StateHashMessage {
    static constexpr MessageType kType = MessageType::StateHash;
    int tick = 0;
    std::uint64_t hash = 0;
    void write(ByteWriter& out) const {
        out.i32(tick);
        out.u64(hash);
    }
    void read(ByteReader& in) {
        tick = in.i32();
        hash = in.u64();
    }
};

// --- Server -> client

struct Welcome {
    static constexpr MessageType kType = MessageType::Welcome;
    int playerId = 0;
    void write(ByteWriter& out) const { out.i32(playerId); }
    void read(ByteReader& in) { playerId = in.i32(); }
};

struct LobbyInfo {
    int id = 0;
    std::string name;
    int players = 0;
};

struct LobbyList {
    static constexpr MessageType kType = MessageType::LobbyList;
    std::vector<LobbyInfo> lobbies;
    void write(ByteWriter& out) const;
    void read(ByteReader& in);
};

struct Joined {
    static constexpr MessageType kType = MessageType::Joined;
    int lobbyId = 0;
    std::string lobbyName;
    std::uint32_t seed = 0;
    std::vector<TickInput> history;  // every tick so far, to replay
    void write(ByteWriter& out) const;
    void read(ByteReader& in);
};

struct TickMessage {
    static constexpr MessageType kType = MessageType::Tick;
    TickInput input;
    void write(ByteWriter& out) const;
    void read(ByteReader& in);
};

struct ChatLine {
    static constexpr MessageType kType = MessageType::ChatLine;
    std::string from;  // empty: a note from the server ("Bob joined")
    std::string text;
    void write(ByteWriter& out) const {
        out.string(from);
        out.string(text);
    }
    void read(ByteReader& in) {
        from = in.string(kMaxNameLength);
        text = in.string(kMaxChatLength * 2);
    }
};

struct ErrorMessage {
    static constexpr MessageType kType = MessageType::Error;
    std::string text;
    void write(ByteWriter& out) const { out.string(text); }
    void read(ByteReader& in) { text = in.string(200); }
};

// --- Bytes

// A message as bytes: its type, then its fields.
template <typename Message>
std::vector<std::uint8_t> encode(const Message& message) {
    ByteWriter out;
    out.u8(static_cast<std::uint8_t>(Message::kType));
    message.write(out);
    return out.bytes();
}

// The type of a received message (the first byte), or nothing if it's empty.
std::optional<MessageType> typeOf(std::span<const std::uint8_t> bytes);

// Reads a message of a known type back. Nothing if the bytes are malformed: too short, too long, or with values out
// of range (a client could send anything).
template <typename Message>
std::optional<Message> decode(std::span<const std::uint8_t> bytes) {
    ByteReader in(bytes);
    if (in.u8() != static_cast<std::uint8_t>(Message::kType)) return std::nullopt;
    Message message;
    message.read(in);
    if (!in.ok() || !in.atEnd()) return std::nullopt;
    return message;
}

// Player names: 1 to kMaxNameLength letters, digits, '_' or '-'.
bool isValidName(const std::string& name);
// Chat: printable ASCII only (the font has nothing else), trimmed; empty if nothing is left.
std::string cleanChat(const std::string& text);

}  // namespace protocol
