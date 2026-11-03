#include "audio/footsteps.hpp"

#include <cmath>

namespace mcpp::audio {
namespace {

constexpr double kWalkFactor = 0.6;
constexpr double kBelowFeet = 0.2;
constexpr float kUnderneathVolume = 1.0f / 3.0f;
constexpr float kUnderneathPitch = 0.8f;

bool is_air(std::string_view name) {
    return name.empty() || name == "minecraft:air" || name == "minecraft:cave_air" ||
           name == "minecraft:void_air";
}

int32_t block_floor(double value) { return static_cast<int32_t>(std::floor(value)); }

} // namespace

std::vector<StepSound> Footsteps::tick(const StepInput& input, const BlockSounds& table,
                                       const BlockNameAt& block_at, const InTag& in_tag) {
    std::vector<StepSound> sounds;
    if (!started_) {
        started_ = true;
        last_ = input;
        fallen_ = 0.0;
        return sounds;
    }
    const double dx = input.x - last_.x;
    const double dy = input.y - last_.y;
    const double dz = input.z - last_.z;
    last_ = input;

    const auto play = [&](const std::string& block, BlockSoundKind kind, float volume, float pitch) {
        if (auto cue = audio::cue(table.find(block), kind)) {
            cue->volume *= volume;
            cue->pitch *= pitch;
            sounds.push_back({*cue, input.x, input.y, input.z});
        }
    };

    const int32_t bx = block_floor(input.x);
    const int32_t bz = block_floor(input.z);
    const std::string below = block_at(bx, block_floor(input.y - kBelowFeet), bz);

    if (input.on_ground) {
        if (fallen_ > safe_fall_distance && !is_air(below)) {
            play(below, BlockSoundKind::Fall, 1.0f, 1.0f);
        }
        fallen_ = 0.0;
    } else if (dy < 0.0) {
        fallen_ -= dy;
    }

    walked_ += std::hypot(dx, dz) * kWalkFactor;
    if (!input.on_ground || walked_ <= next_step_) return sounds;
    const std::string at_feet = block_at(bx, block_floor(input.y), bz);
    if (in_tag(at_feet, "minecraft:inside_step_sound_blocks")) {
        next_step_ = std::floor(walked_) + 1.0;
        play(at_feet, BlockSoundKind::Step, 1.0f, 1.0f);
    } else if (in_tag(at_feet, "minecraft:combination_step_sound_blocks")) {
        next_step_ = std::floor(walked_) + 1.0;
        play(at_feet, BlockSoundKind::Step, 1.0f, 1.0f);
        play(below, BlockSoundKind::Step, kUnderneathVolume, kUnderneathPitch);
    } else if (!is_air(below)) {
        next_step_ = std::floor(walked_) + 1.0;
        play(below, BlockSoundKind::Step, 1.0f, 1.0f);
    }
    return sounds;
}

} // namespace mcpp::audio
