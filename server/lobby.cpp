#include "lobby.h"

#include "protocol.h"

#include <algorithm>

std::vector<int> Lobby::memberIds() const {
    std::vector<int> ids;
    for (const auto& [id, member] : members_) ids.push_back(id);
    return ids;
}

void Lobby::addMember(int playerId, const std::string& name) {
    Member member;
    member.commands.push_back(PlayerCommand::join(name));
    members_[playerId] = std::move(member);
}

void Lobby::removeMember(int playerId) {
    if (members_.erase(playerId) > 0) leaving_.push_back(playerId);
}

void Lobby::setInput(int playerId, const PlayerInput& input) {
    auto it = members_.find(playerId);
    if (it == members_.end()) return;
    Member& member = it->second;
    member.input = input;
    if (input.attackPressed) member.pressed = true;
}

void Lobby::addCommand(int playerId, const PlayerCommand& command) {
    auto it = members_.find(playerId);
    if (it == members_.end()) return;
    // Joining and leaving are the server's to decide; and nobody gets to flood a tick with commands.
    if (command.kind == PlayerCommand::Kind::Join || command.kind == PlayerCommand::Kind::Leave) return;
    if (static_cast<int>(it->second.commands.size()) >= protocol::kMaxCommandsPerTurn) return;
    it->second.commands.push_back(command);
}

const TickInput& Lobby::nextTick() {
    TickInput tick;
    tick.tick = static_cast<int>(history_.size()) + 1;
    for (auto& [id, member] : members_) {
        PlayerTurn turn{.playerId = id, .input = member.input, .commands = std::move(member.commands)};
        turn.input.attackPressed = member.pressed;
        member.pressed = false;
        member.commands.clear();
        tick.turns.push_back(std::move(turn));
    }
    for (const int id : leaving_) tick.turns.push_back({.playerId = id, .commands = {PlayerCommand::leave()}});
    leaving_.clear();
    // Every client processes the turns in this order, so it must be the same for everyone: by player id.
    std::sort(tick.turns.begin(), tick.turns.end(),
              [](const PlayerTurn& a, const PlayerTurn& b) { return a.playerId < b.playerId; });
    history_.push_back(std::move(tick));
    return history_.back();
}

bool Lobby::reportHash(int playerId, int tick, std::uint64_t hash) {
    auto& reports = hashes_[tick];
    reports[playerId] = hash;
    // Forget ticks long gone.
    while (!hashes_.empty() && hashes_.begin()->first < tick - 600) hashes_.erase(hashes_.begin());
    const bool agree = std::all_of(reports.begin(), reports.end(),
                                   [&](const auto& report) { return report.second == reports.begin()->second; });
    if (agree || desyncReported_) return false;
    desyncReported_ = true;
    return true;
}
