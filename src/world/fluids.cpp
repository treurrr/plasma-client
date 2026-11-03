#include "mcpp/world/fluids.hpp"

#include <charconv>
#include <cstdlib>
#include <format>
#include <fstream>
#include <string>
#include <string_view>

namespace mcpp::world {

std::filesystem::path FluidTable::default_path(int32_t protocol) {
    std::filesystem::path base;
    if (const char* data_home = std::getenv("XDG_DATA_HOME")) {
        base = data_home;
    } else if (const char* home = std::getenv("HOME")) {
        base = std::filesystem::path(home) / ".local" / "share";
    }
    return base / "plasma-client" / "data" / std::format("states-{}.txt", protocol);
}

bool FluidTable::open(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) return false;
    states_.clear();
    std::string line;
    while (std::getline(input, line)) {
        // state id, block id, block name, properties; tab separated.
        const size_t first = line.find('\t');
        const size_t second = line.find('\t', first + 1);
        const size_t third = line.find('\t', second + 1);
        if (first == std::string::npos || second == std::string::npos) continue;
        size_t id = 0;
        std::from_chars(line.data(), line.data() + first, id);
        const std::string_view name(line.data() + second + 1,
                                    (third == std::string::npos ? line.size() : third) - second - 1);
        const std::string_view properties =
            third == std::string::npos ? std::string_view{} : std::string_view(line).substr(third + 1);
        if (states_.size() <= id) states_.resize(id + 1);
        FluidState& state = states_[id];

        const auto property = [&](std::string_view key) -> std::string_view {
            size_t at = 0;
            while (at < properties.size()) {
                size_t end = properties.find(',', at);
                if (end == std::string_view::npos) end = properties.size();
                const std::string_view pair = properties.substr(at, end - at);
                if (pair.starts_with(key) && pair.size() > key.size() && pair[key.size()] == '=') {
                    return pair.substr(key.size() + 1);
                }
                at = end + 1;
            }
            return {};
        };
        const auto still_water = [&] {
            state.fluid = Fluid::Water;
            state.amount = 8;
            state.source = true;
        };
        if (name == "minecraft:water" || name == "minecraft:lava") {
            int level = 0;
            const auto text = property("level");
            std::from_chars(text.data(), text.data() + text.size(), level);
            state.fluid = name == "minecraft:water" ? Fluid::Water : Fluid::Lava;
            state.source = level == 0;
            state.falling = level >= 8;
            state.amount = static_cast<uint8_t>(level == 0 || level >= 8 ? 8 : 8 - level);
        } else if (property("waterlogged") == "true" || name == "minecraft:bubble_column" ||
                   name == "minecraft:kelp" || name == "minecraft:kelp_plant" ||
                   name == "minecraft:seagrass" || name == "minecraft:tall_seagrass") {
            still_water();
        }
    }
    return !states_.empty();
}

} // namespace mcpp::world
