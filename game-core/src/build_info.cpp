#include "mc/core/build_info.hpp"

namespace mc::core {

std::string BuildInfo::versionString() {
    return std::to_string(kVersionMajor) + "." + std::to_string(kVersionMinor) + "." +
           std::to_string(kVersionPatch);
}

bool BuildInfo::isCompatibleWith(std::uint16_t major, std::uint16_t minor) {
    // Before 1.0 every minor bump may change game rules, so both must match.
    return major == kVersionMajor && minor == kVersionMinor;
}

}  // namespace mc::core
