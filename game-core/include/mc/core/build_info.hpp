#pragma once

#include <cstdint>
#include <string>

namespace mc::core {

// Identifies the simulation build. Client and server compare these during the
// handshake so mismatched game rules are rejected before play starts.
class BuildInfo {
public:
    static constexpr std::uint16_t kVersionMajor = 0;
    static constexpr std::uint16_t kVersionMinor = 1;
    static constexpr std::uint16_t kVersionPatch = 0;

    [[nodiscard]] static std::string versionString();
    [[nodiscard]] static bool isCompatibleWith(std::uint16_t major, std::uint16_t minor);
};

}  // namespace mc::core
