#pragma once
#include "audio/block_sounds.hpp"

#include "mcpp/jmath.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace mcpp::audio {

// The local player after one game tick of movement.
struct StepInput {
    double x = 0.0;
    double y = 0.0;   // the feet
    double z = 0.0;
    // What the move did this tick, after collisions.
    double moved_x = 0.0;
    double moved_y = 0.0;
    double moved_z = 0.0;
    bool on_ground = false;
    bool crouching = false;
    bool in_water = false;
    bool flying = false;
    // The server lets this player fly, which also spares it fall damage.
    bool may_fly = false;
    double safe_fall_distance = 3.0;
    double fall_damage_multiplier = 1.0;
};

struct StepSound {
    std::string event;
    float volume = 1.0f;
    float pitch = 1.0f;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

// What the client knows of the world around the player.
struct StepWorld {
    // The registry name of the block at a place, "minecraft:air" for nothing.
    std::function<std::string(int32_t, int32_t, int32_t)> block_at;
    // Whether a block is in one of the server's block tags.
    std::function<bool(std::string_view block, std::string_view tag)> in_tag;
    // Whether the block there has something to stand on.
    std::function<bool(int32_t, int32_t, int32_t)> collides;
};

// The player's own footsteps, landings and the amethyst chime, which the
// server never sends back to the player who made them. Followed step for
// step from the game: the walked distance in floats at 0.6 a block, a step
// each time it passes the next whole number on the block under the feet, the
// block the player is standing on found the way the game finds it, the
// combination and inside-step tags, and the fall played only when it hurts.
class Footsteps {
public:
    std::vector<StepSound> tick(const StepInput& input, const BlockSounds& table, const StepWorld& world,
                                jmath::LegacyRandom& random);
    // A teleport, a respawn or a new world: nothing walked, nothing fallen.
    void reset();

private:
    struct Pos {
        int32_t x, y, z;
        bool operator==(const Pos&) const = default;
    };
    Pos on_pos(const StepInput& input, const StepWorld& world, float below) const;
    void find_support(const StepInput& input, const StepWorld& world);

    float move_dist_ = 0.0f;
    float next_step_ = 1.0f;
    double fall_distance_ = 0.0;
    bool has_support_ = false;
    bool ground_without_blocks_ = false;
    Pos support_{0, 0, 0};
    int32_t ticks_ = 0;
    int32_t last_chime_ = -20;
    float chime_intensity_ = 0.0f;
};

} // namespace mcpp::audio
