#pragma once
#include "mcpp/world/fluid.hpp"
#include <filesystem>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mcpp::world {

struct Aabb {
    double min[3];
    double max[3];
};

// A collision shape the way the game collides with it: the edges of its
// cells on each axis, relative to the block's own corner, and which cells are
// full. Two shapes that cover the same space can still collide differently
// from inside, because the game walks cell by cell.
struct Voxels {
    uint32_t first[3] = {0, 0, 0};   // into BlockShapes' edge pool
    uint8_t edges[3] = {0, 0, 0};
    uint32_t cells = 0;              // first bit in BlockShapes' cell pool, x fastest
    double top = 0.0;                // the highest edge of a full cell
};

// What a block does beyond being in the way, where it is not a number in
// the table: the game hard-codes these by block.
enum class Special : uint8_t {
    None,
    Scaffolding,
    PowderSnow,
    Cobweb,
    SweetBerryBush,
    Honey,
    Slime,
    Bed,
    BubbleColumn,
    Ice,        // ice and frosted ice: a falling fluid does not see their faces
    Trapdoor,
    Ladder,
    Water,
    Lava,
};

enum StateFlag : uint16_t {
    kAir = 1,
    kLargeCollision = 2,   // reaches out of its cell, so neighbours must be asked too
    kSuffocating = 4,
    kReplaceable = 8,
    kNeedsTool = 16,
    kFullCollision = 32,
    kBlocksMotion = 64,
    kSturdyDown = 256,     // then up, north, south, west, east
};

struct StateInfo {
    uint16_t block = 0;
    uint16_t collision = 0;
    uint16_t outline = 0;
    uint16_t interaction = 0;
    uint16_t flags = 0;
    Fluid fluid = Fluid::None;
    uint8_t fluid_amount = 0;   // eighths; 8 for a source
    bool fluid_falling = false;
    bool fluid_source = false;
    float hardness = 0.0f;      // -1 cannot be broken

    bool sturdy(int direction) const { return (flags & (kSturdyDown << direction)) != 0; }
    // The fluid's own height in the cell, without looking above it.
    float fluid_height() const { return static_cast<float>(fluid_amount) / 9.0f; }
};

struct BlockInfo {
    std::string name;
    int32_t registry_id = 0;   // what block tags hold
    int32_t first = 0;
    int32_t count = 0;
    float friction = 0.6f;
    float speed = 1.0f;
    float jump = 1.0f;
    // Plants and bamboo stand off the grid by a hash of where they are.
    uint8_t offset = 0;   // 0 none, 1 sideways, 2 sideways and down
    float max_horizontal_offset = 0.0f;
    float max_vertical_offset = 0.0f;
    Special special = Special::None;
    struct Property {
        std::string name;
        std::vector<std::string> values;
    };
    std::vector<Property> properties;   // the first one changes slowest
};

// Every block state's shapes and the numbers movement reads from it, as the
// game itself reported them (tools/oracle/BlockOracle.java). Read once; every
// lookup after that is an index.
class BlockShapes {
public:
    static std::filesystem::path default_path(int32_t protocol);
    // The baked table, read once. Without one, every state is air.
    static const BlockShapes& game();
    bool loaded() const { return states_.size() > 1; }

    bool parse(std::string_view text);
    size_t size() const { return states_.size(); }

    // A state id the table does not know is air.
    const StateInfo& state(int32_t id) const {
        return id > 0 && static_cast<size_t>(id) < states_.size() ? states_[id] : states_[0];
    }
    const BlockInfo& block(int32_t id) const { return blocks_[state(id).block]; }
    std::string_view name(int32_t id) const { return block(id).name; }

    bool empty(uint16_t shape) const { return shapes_[shape].empty; }
    const Voxels& voxels(uint16_t shape) const { return shapes_[shape].voxels; }
    std::span<const double> edges(const Voxels& v, int axis) const {
        return {edge_pool_.data() + v.first[axis], v.edges[axis]};
    }
    bool full(const Voxels& v, int x, int y, int z) const {
        const uint32_t nx = v.edges[0] - 1u;
        const uint32_t ny = v.edges[1] - 1u;
        const uint32_t bit = v.cells + static_cast<uint32_t>(x) +
                             nx * (static_cast<uint32_t>(y) + ny * static_cast<uint32_t>(z));
        return (cell_pool_[bit >> 3] >> (bit & 7)) & 1u;
    }
    // Outline and interaction shapes as boxes, for aiming.
    std::span<const Aabb> boxes(uint16_t shape) const {
        return {box_pool_.data() + shapes_[shape].box_first, shapes_[shape].box_count};
    }

    // The value of one property of a state, or empty.
    std::string_view property(int32_t id, std::string_view key) const;
    // The state of the same block with one property changed, or -1.
    int32_t with(int32_t id, std::string_view key, std::string_view value) const;
    // A block's first state by name, or -1.
    int32_t find(std::string_view block_name) const;
    // A block's state with these properties set and the rest at their first
    // value, or -1: "minecraft:oak_stairs", {{"facing", "east"}, {"half", "top"}}.
    int32_t find(std::string_view block_name,
                 std::initializer_list<std::pair<std::string_view, std::string_view>> set) const;

    // A shape the game picks by who is asking, not by state:
    // "scaffolding_unstable_bottom", "powder_snow_falling". -1 if unknown.
    int32_t named(std::string_view name) const;

    // Where a plant stands in its block, the game's own hash of the column.
    void offset(int32_t id, int32_t x, int32_t z, double out[3]) const;

private:
    struct Shape {
        bool empty = true;
        Voxels voxels;
        uint32_t box_first = 0;
        uint32_t box_count = 0;
    };
    std::vector<Shape> shapes_;
    std::vector<double> edge_pool_;
    std::vector<uint8_t> cell_pool_;
    std::vector<Aabb> box_pool_;
    std::vector<BlockInfo> blocks_;
    std::vector<StateInfo> states_;
    std::vector<std::pair<std::string, uint16_t>> named_;
};

} // namespace mcpp::world
