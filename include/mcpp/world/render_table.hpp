#pragma once
#include "mcpp/world/fluid.hpp"
#include <cstdint>
#include <filesystem>
#include <vector>

namespace mcpp::world {

// What fluid a block state holds, the way the game's FluidState sees it.
struct FluidState {
    Fluid fluid = Fluid::None;
    uint8_t amount = 0;     // 1 to 8; 8 for a source and for a falling column
    bool source = false;
    bool falling = false;
    // FluidState.getOwnHeight: how far up its block the surface sits.
    float own_height() const { return static_cast<float>(amount) / 9.0f; }
};

enum class RenderLayer : uint8_t { Solid, Cutout, Translucent, Tripwire };
enum class OffsetKind : uint8_t { None, Horizontal, All };

// Per block state, what the game itself reports for lighting and placing it:
// read from the table tools/bake_block_render.py asks the game for.
struct RenderInfo {
    static constexpr uint8_t kCollisionFull = 1;
    static constexpr uint8_t kViewBlocking = 2;
    static constexpr uint8_t kSolidRender = 4;
    static constexpr uint8_t kEmissive = 8;
    static constexpr uint8_t kLeaves = 16;
    static constexpr uint8_t kCanOcclude = 32;
    static constexpr uint8_t kHalfTransparent = 64;

    RenderLayer layer = RenderLayer::Solid;
    uint8_t flags = 0;
    uint8_t emission = 0;
    uint8_t light_block = 0;
    OffsetKind offset = OffsetKind::None;
    // 1.0, or 0.2 for a block whose collision fills its cell.
    float shade_brightness = 1.0f;
    float max_horizontal_offset = 0.25f;
    float max_vertical_offset = 0.2f;
    FluidState fluid;

    bool has(uint8_t flag) const { return (flags & flag) != 0; }
};

class RenderTable {
public:
    bool open(const std::filesystem::path& path);
    bool is_open() const { return !states_.empty(); }
    const RenderInfo& at(int32_t state) const {
        return state >= 0 && static_cast<size_t>(state) < states_.size() ? states_[static_cast<size_t>(state)]
                                                                          : air_;
    }
    FluidState fluid(int32_t state) const { return at(state).fluid; }
    size_t size() const { return states_.size(); }

    static std::filesystem::path default_path(int32_t protocol);

private:
    std::vector<RenderInfo> states_;
    RenderInfo air_;
};

// BlockBehaviour's random offset for plants: a hash of the column, scaled to
// at most a quarter block across and a fifth down.
void block_offset(const RenderInfo& info, int32_t x, int32_t z, float out[3]);

} // namespace mcpp::world
