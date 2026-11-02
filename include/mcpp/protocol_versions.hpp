#pragma once
#include <cstdint>
#include <span>
#include <string_view>

// From version.json inside the official jars.
namespace mcpp::protocol {

struct Version {
    int32_t protocol;
    std::string_view name;
    bool supported;
};

constexpr int32_t kProtocol_1_8_9 = 47;
constexpr int32_t kProtocol_1_21_11 = 774;
constexpr int32_t kProtocol_26_2 = 776;   // ~/bradge-testserver
constexpr int32_t kProtocol_26_3 = 777;

constexpr Version kVersions[] = {
    {kProtocol_1_8_9,   "1.8.9",   false},  // het PvP-publiek; na 1.21.11
    {kProtocol_1_21_11, "1.21.11", true},
    {kProtocol_26_2,    "26.2",    false},
    {kProtocol_26_3,    "26.3",    false},
};

constexpr int32_t kTargetProtocol = kProtocol_1_21_11;
constexpr std::string_view kTargetVersionName = "1.21.11";

constexpr std::span<const Version> all_versions() { return {kVersions, std::size(kVersions)}; }

} // namespace mcpp::protocol
