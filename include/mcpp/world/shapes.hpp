#pragma once
#include "mcpp/world/block_table.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mcpp::world {

// A box inside one block, in blocks: 0 to 1 for a full cube, up to 1.5 high
// for a fence. Relative to the block's own lowest corner.
struct Box {
    double min[3];
    double max[3];
};

using Properties = std::vector<std::pair<std::string, std::string>>;

enum class FluidKind : uint8_t { None, Water, Lava };

// What a block does to a body touching it, beyond being in the way.
struct BlockPhysics {
    float friction = 0.6f;
    float speed = 1.0f;
    float jump = 1.0f;
    // A cobweb, a berry bush, powder snow: each tick's movement is scaled by
    // this while the body is inside, and its speed is lost.
    float stuck[3] = {0.0f, 0.0f, 0.0f};
    bool climbable = false;
    bool slime = false;
    bool bed = false;
    bool honey = false;
    bool scaffolding = false;
    bool powder_snow = false;
    FluidKind fluid = FluidKind::None;
    // Eighths of a block of fluid: 8 for a source or a falling column.
    uint8_t fluid_amount = 0;
    // A bubble column pulls down over magma and pushes up over soul sand.
    int8_t bubbles = 0;
    // Bamboo and pointed dripstone stand off-centre by a hash of where they are.
    float offset = 0.0f;
};

// Hardness, and whether only the right tool gets a drop. -1 cannot be broken.
struct DigInfo {
    float hardness = 0.0f;
    bool needs_tool = false;
};

// Every block state's collision and outline boxes and what it does to a body,
// resolved once from the states the reports list and the hand tables in data/.
class BlockShapes {
public:
    bool open(const std::filesystem::path& states, const BlockTable* models = nullptr);
    static std::filesystem::path default_path(int32_t protocol);

    // One state from its name and properties, as open does it; tests build
    // worlds out of these.
    void define(int32_t state, std::string_view name, const Properties& properties,
                const BlockTable* models = nullptr, int32_t block = -1);

    bool is_open() const { return !states_.empty(); }
    size_t size() const { return states_.size(); }
    std::span<const Box> collision(int32_t state) const;
    std::span<const Box> outline(int32_t state) const;
    const BlockPhysics& physics(int32_t state) const;
    const DigInfo& dig(int32_t state) const;
    std::string_view name(int32_t state) const;
    std::string_view property(int32_t state, std::string_view key) const;
    // The block's id in the block registry, which is what block tags hold.
    int32_t block(int32_t state) const;
    // The state with these properties of the block that state belongs to, or
    // -1; what placing a slab's top half or a stair's facing picks.
    int32_t with(int32_t state, std::string_view key, std::string_view value) const;
    // The first state of a block by name, or -1.
    int32_t find(std::string_view name) const;
    bool full_collision(int32_t state) const;

private:
    struct State {
        std::string name;
        Properties properties;
        int32_t block = -1;
        uint32_t collision_first = 0;
        uint32_t collision_count = 0;
        uint32_t outline_first = 0;
        uint32_t outline_count = 0;
        BlockPhysics physics;
        DigInfo dig;
    };
    std::vector<State> states_;
    std::vector<Box> boxes_;
};

// The boxes a shape family gives for one set of properties, in sixteenths
// turned into blocks. A family is a name in data/collision_shapes.txt:
// "full", "empty", "slab", "stair", "box:0,0,0,16,14,16|...", and so on.
// Empty for a family it does not know, and `known` says so.
std::vector<Box> family_shape(std::string_view family, const Properties& properties,
                              bool* known = nullptr);

// Where bamboo and pointed dripstone stand within their block: a hash of the
// position, the way the game spreads plants so a field of them is not a grid.
void random_offset(int32_t x, int32_t z, float max, double& dx, double& dz);

} // namespace mcpp::world
