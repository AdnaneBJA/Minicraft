#include "mc/core/build_info.hpp"

#include <gtest/gtest.h>

namespace mc::core {
namespace {

TEST(BuildInfoTest, VersionStringMatchesConstants) {
    EXPECT_EQ(BuildInfo::versionString(), "0.1.0");
}

TEST(BuildInfoTest, CompatibleWithSameMajorMinor) {
    EXPECT_TRUE(BuildInfo::isCompatibleWith(BuildInfo::kVersionMajor, BuildInfo::kVersionMinor));
}

TEST(BuildInfoTest, IncompatibleWithDifferentMinor) {
    EXPECT_FALSE(BuildInfo::isCompatibleWith(BuildInfo::kVersionMajor,
                                             static_cast<std::uint16_t>(BuildInfo::kVersionMinor + 1)));
}

}  // namespace
}  // namespace mc::core
