#pragma once
#include <cstdint>
#include <filesystem>
#include <vector>

namespace mcpp::world {

enum class Fluid : uint8_t { None, Water, Lava };

// What fluid a block state holds, the way the game's FluidState sees it: water
// and lava blocks by their level, every waterlogged block and the plants that
// only grow underwater as still water.
struct FluidState {
    Fluid fluid = Fluid::None;
    uint8_t amount = 0;     // 1 to 8; 8 for a source and for a falling column
    bool source = false;
    bool falling = false;
    // FluidState.getOwnHeight: how far up its block the surface sits.
    float own_height() const { return static_cast<float>(amount) / 9.0f; }
};

class FluidTable {
public:
    // Reads the state list baked beside the block table.
    bool open(const std::filesystem::path& states);
    FluidState at(int32_t state) const {
        return state >= 0 && static_cast<size_t>(state) < states_.size() ? states_[state] : FluidState{};
    }
    size_t size() const { return states_.size(); }

    static std::filesystem::path default_path(int32_t protocol);

private:
    std::vector<FluidState> states_;
};

} // namespace mcpp::world
