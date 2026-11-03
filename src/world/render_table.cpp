#include "mcpp/world/render_table.hpp"

#include <algorithm>
#include <bit>
#include <cstdlib>
#include <format>
#include <fstream>
#include <sstream>
#include <string>

namespace mcpp::world {

std::filesystem::path RenderTable::default_path(int32_t protocol) {
    std::filesystem::path base;
    if (const char* data_home = std::getenv("XDG_DATA_HOME")) {
        base = data_home;
    } else if (const char* home = std::getenv("HOME")) {
        base = std::filesystem::path(home) / ".local" / "share";
    }
    return base / "plasma-client" / "data" / std::format("block-render-{}.txt", protocol);
}

bool RenderTable::open(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) return false;
    states_.clear();
    std::string line;
    while (std::getline(input, line)) {
        std::istringstream fields(line);
        size_t id = 0;
        uint32_t layer = 0, flags = 0, emission = 0, light_block = 0, offset = 0, fluid = 0, amount = 0;
        uint32_t shade = 0, max_h = 0, max_v = 0;
        if (!(fields >> id >> layer >> flags >> emission >> light_block >> shade >> offset >> max_h >>
              max_v >> fluid >> amount)) {
            continue;
        }
        if (states_.size() <= id) states_.resize(id + 1);
        RenderInfo& info = states_[id];
        info.layer = static_cast<RenderLayer>(std::min(layer, 3u));
        info.flags = static_cast<uint8_t>((flags & 0x3F) | ((flags & 256) != 0 ? RenderInfo::kHalfTransparent : 0));
        info.emission = static_cast<uint8_t>(emission);
        info.light_block = static_cast<uint8_t>(light_block);
        info.offset = static_cast<OffsetKind>(std::min(offset, 2u));
        info.shade_brightness = std::bit_cast<float>(shade);
        info.max_horizontal_offset = std::bit_cast<float>(max_h);
        info.max_vertical_offset = std::bit_cast<float>(max_v);
        info.fluid.fluid = static_cast<Fluid>(std::min(fluid, 2u));
        info.fluid.amount = static_cast<uint8_t>(amount);
        info.fluid.falling = (flags & 64) != 0;
        info.fluid.source = (flags & 128) != 0;
    }
    return !states_.empty();
}

void block_offset(const RenderInfo& info, int32_t x, int32_t z, float out[3]) {
    out[0] = out[1] = out[2] = 0.0f;
    if (info.offset == OffsetKind::None) return;
    // Mth.getSeed with y = 0; the first product wraps as a 32-bit int.
    const auto wrapped = static_cast<int32_t>(static_cast<uint32_t>(x) * 3129871u);
    int64_t seed = static_cast<int64_t>(wrapped) ^ (static_cast<int64_t>(z) * 116129781LL) ^ 0;
    seed = static_cast<int64_t>(static_cast<uint64_t>(seed) * static_cast<uint64_t>(seed) * 42317861ull +
                                static_cast<uint64_t>(seed) * 11ull);
    seed >>= 16;
    const double limit = info.max_horizontal_offset;
    const auto across = [&](int shift) {
        const double raw = (static_cast<double>(static_cast<float>((seed >> shift) & 15) / 15.0f) - 0.5) * 0.5;
        return std::clamp(raw, -limit, limit);
    };
    out[0] = static_cast<float>(across(0));
    out[2] = static_cast<float>(across(8));
    if (info.offset == OffsetKind::All) {
        out[1] = static_cast<float>((static_cast<double>(static_cast<float>((seed >> 4) & 15) / 15.0f) - 1.0) *
                                    static_cast<double>(info.max_vertical_offset));
    }
}

} // namespace mcpp::world
