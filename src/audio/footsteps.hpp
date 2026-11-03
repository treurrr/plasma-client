#pragma once
#include "audio/block_sounds.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace mcpp::audio {

// Where a body is after one game tick.
struct StepInput {
    double x = 0.0;
    double y = 0.0;   // the feet
    double z = 0.0;
    bool on_ground = false;
};

struct StepSound {
    BlockSoundCue cue;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

// The registry name of the block at a place, "minecraft:air" for nothing.
using BlockNameAt = std::function<std::string(int32_t, int32_t, int32_t)>;
// Whether a block is in one of the server's block tags, by registry name.
using InTag = std::function<bool(std::string_view block, std::string_view tag)>;

// Footsteps and landings for one body, fed once a game tick. The rules are the
// ones the server follows for another player, measured off its own packets
// (corpus/sound-anvil.plrc): 0.6 of every block walked counts, a step falls
// each time that count passes the next whole number, on the block under the
// feet; a block in inside_step_sound_blocks is heard on its own, and one in
// combination_step_sound_blocks with the block under it at a third of the
// volume and four fifths of the pitch.
class Footsteps {
public:
    std::vector<StepSound> tick(const StepInput& input, const BlockSounds& table,
                                const BlockNameAt& block_at, const InTag& in_tag);
    // A teleport or a respawn: the distance from the old place is not walked.
    void reset() { started_ = false; }

    // The landing after a fall of more than this, on the block below, which is
    // when the game counts the fall as damage.
    double safe_fall_distance = 3.0;

private:
    bool started_ = false;
    StepInput last_;
    double walked_ = 0.0;
    double next_step_ = 0.0;
    double fallen_ = 0.0;
};

} // namespace mcpp::audio
