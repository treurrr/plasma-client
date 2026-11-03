#include "audio/footsteps.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

namespace mcpp::audio {
namespace {

constexpr double kHalfWidth = 0.3;

bool is_air(std::string_view name) {
    return name.empty() || name == "minecraft:air" || name == "minecraft:cave_air" ||
           name == "minecraft:void_air";
}

int32_t block_floor(double value) { return static_cast<int32_t>(std::floor(value)); }

// `Entity.isStateClimbable`: climbable blocks and powder snow.
bool climbable(const StepWorld& world, std::string_view block) {
    return world.in_tag(block, "minecraft:climbable") || block == "minecraft:powder_snow";
}

void push(std::vector<StepSound>& sounds, const StepInput& input, std::optional<BlockSoundCue> cue) {
    if (cue) sounds.push_back({cue->event, cue->volume, cue->pitch, input.x, input.y, input.z});
}

// `Entity.playMuffledStepSound`: a step at a twentieth of the volume, lower.
std::optional<BlockSoundCue> muffled(const BlockSoundType& type) {
    if (type.step.empty() || type.step == "intentionally_empty") return std::nullopt;
    return BlockSoundCue{type.step, type.volume * 0.05f, type.pitch * 0.8f};
}

} // namespace

void Footsteps::reset() {
    move_dist_ = 0.0f;
    next_step_ = 1.0f;
    fall_distance_ = 0.0;
    has_support_ = false;
    ground_without_blocks_ = false;
}

// `Entity.checkSupportingBlock`: the block under the feet, or the one just left.
void Footsteps::find_support(const StepInput& input, const StepWorld& world) {
    const auto search = [&](double shift_x, double shift_z) -> std::optional<Pos> {
        const double min_x = input.x - kHalfWidth + shift_x;
        const double max_x = input.x + kHalfWidth + shift_x;
        const double min_z = input.z - kHalfWidth + shift_z;
        const double max_z = input.z + kHalfWidth + shift_z;
        const double min_y = input.y - 1.0e-6;
        const double max_y = input.y;
        std::optional<Pos> best;
        double best_distance = 0.0;
        for (int32_t by = block_floor(min_y); by < static_cast<int32_t>(std::ceil(max_y)); ++by) {
            for (int32_t bz = block_floor(min_z); bz < static_cast<int32_t>(std::ceil(max_z)); ++bz) {
                for (int32_t bx = block_floor(min_x); bx < static_cast<int32_t>(std::ceil(max_x)); ++bx) {
                    if (!world.collides(bx, by, bz)) continue;
                    const double dx = bx + 0.5 - input.x;
                    const double dy = by + 0.5 - input.y;
                    const double dz = bz + 0.5 - input.z;
                    const double distance = dx * dx + dy * dy + dz * dz;
                    const Pos pos{bx, by, bz};
                    const auto less = [](const Pos& a, const Pos& b) {
                        if (a.y != b.y) return a.y < b.y;
                        if (a.z != b.z) return a.z < b.z;
                        return a.x < b.x;
                    };
                    if (!best || distance < best_distance ||
                        (distance == best_distance && less(*best, pos))) {
                        best = pos;
                        best_distance = distance;
                    }
                }
            }
        }
        return best;
    };
    // Stepping off an edge, the block just left still holds the player up.
    auto found = search(0.0, 0.0);
    if (!found && !ground_without_blocks_) found = search(-input.moved_x, -input.moved_z);
    has_support_ = found.has_value();
    if (found) support_ = *found;
    ground_without_blocks_ = !found.has_value();
}

// `Entity.getOnPos`: the block a given depth under the feet.
Footsteps::Pos Footsteps::on_pos(const StepInput& input, const StepWorld& world, float below) const {
    if (has_support_) {
        if (!(below > 1.0e-5f)) return support_;
        const std::string block = world.block_at(support_.x, support_.y, support_.z);
        const bool fence = world.in_tag(block, "minecraft:fences");
        const bool keeps = (below <= 0.5f && fence) || world.in_tag(block, "minecraft:walls") ||
                           std::string_view(block).ends_with("_fence_gate");
        if (keeps) return support_;
        return {support_.x, block_floor(input.y - static_cast<double>(below)), support_.z};
    }
    return {block_floor(input.x), block_floor(input.y - static_cast<double>(below)), block_floor(input.z)};
}

std::vector<StepSound> Footsteps::tick(const StepInput& input, const BlockSounds& table,
                                       const StepWorld& world, jmath::LegacyRandom& random) {
    std::vector<StepSound> sounds;
    ++ticks_;
    if (input.on_ground) {
        find_support(input, world);
    } else {
        ground_without_blocks_ = false;
        has_support_ = false;
    }

    const Pos legacy = on_pos(input, world, 0.2f);
    const std::string legacy_block = world.block_at(legacy.x, legacy.y, legacy.z);

    if (input.flying) fall_distance_ = 0.0;
    if (!input.in_water && input.moved_y < 0.0) fall_distance_ -= static_cast<float>(input.moved_y);
    if (input.on_ground) {
        if (fall_distance_ > 0.0) {
            double distance = fall_distance_;
            double multiplier = 1.0;
            bool honey = false;
            bool hurts = true;
            if (legacy_block == "minecraft:hay_block") {
                multiplier = 0.2;
            } else if (legacy_block == "minecraft:honey_block") {
                push(sounds, input, BlockSoundCue{"block.honey_block.slide", 1.0f, 1.0f});
                multiplier = 0.2;
                honey = true;
            } else if (std::string_view(legacy_block).ends_with("_bed")) {
                distance *= 0.5;
            } else if (legacy_block == "minecraft:slime_block" && !input.crouching) {
                multiplier = 0.0;
            } else if (legacy_block == "minecraft:powder_snow") {
                hurts = false;
                if (!(distance < 4.0)) {
                    push(sounds, input,
                         BlockSoundCue{distance < 7.0 ? "entity.player.small_fall" : "entity.player.big_fall",
                                       1.0f, 1.0f});
                }
            }
            // `LivingEntity.causeFallDamage`, `LivingEntity.calculateFallDamage`, `LivingEntity.playBlockFallSound`: the fall sounds when it hurts.
            if (hurts && !input.may_fly && distance > 0.0) {
                const double power = distance + 1.0e-6 - input.safe_fall_distance;
                const auto damage =
                    static_cast<int32_t>(std::floor(power * static_cast<float>(multiplier) * input.fall_damage_multiplier));
                if (damage > 0) {
                    push(sounds, input,
                         BlockSoundCue{damage > 4 ? "entity.player.big_fall" : "entity.player.small_fall", 1.0f, 1.0f});
                    const std::string under = world.block_at(
                        block_floor(input.x), block_floor(input.y - static_cast<double>(0.2f)), block_floor(input.z));
                    if (!is_air(under)) push(sounds, input, cue(table.find(under), BlockSoundKind::Fall));
                    if (honey) push(sounds, input, cue(table.find("minecraft:honey_block"), BlockSoundKind::Fall));
                }
            }
        }
        fall_distance_ = 0.0;
    }

    // `Entity.applyMovementEmissionAndPlaySound`: distance walked, a step past the next whole one.
    const Pos on = on_pos(input, world, 1.0e-5f);
    const std::string on_block = world.block_at(on.x, on.y, on.z);
    const double moved_squared = input.moved_x * input.moved_x + input.moved_y * input.moved_y +
                                 input.moved_z * input.moved_z;
    const double horizontal_squared = input.moved_x * input.moved_x + input.moved_z * input.moved_z;
    const auto total = static_cast<float>(std::sqrt(moved_squared) * 0.6f);
    const auto horizontal = static_cast<float>(std::sqrt(horizontal_squared) * 0.6f);
    move_dist_ += climbable(world, on_block) ? total : horizontal;
    if (!(move_dist_ > next_step_) || is_air(on_block)) return sounds;

    // `Entity.vibrationAndSoundEffectsFromBlock`, `Player.playStepSound`, `Entity.walkingStepSound`: one block's step.
    const auto steps_on = [&](const Pos& pos, const std::string& block, bool audible) {
        if (is_air(block)) return false;
        const bool climbing = climbable(world, block);
        if (!(input.on_ground || climbing || (input.crouching && input.moved_y == 0.0))) return false;
        if (!audible) return true;
        if (input.in_water) {
            push(sounds, input, muffled(table.find(block)));
        } else {
            const std::string above = world.block_at(pos.x, pos.y + 1, pos.z);
            const bool combination = world.in_tag(above, "minecraft:combination_step_sound_blocks");
            const bool inside = world.in_tag(above, "minecraft:inside_step_sound_blocks");
            if (combination) {
                push(sounds, input, cue(table.find(above), BlockSoundKind::Step));
                push(sounds, input, muffled(table.find(block)));
            } else if (inside) {
                push(sounds, input, cue(table.find(above), BlockSoundKind::Step));
            } else {
                push(sounds, input, cue(table.find(block), BlockSoundKind::Step));
            }
        }
        // `Entity.playAmethystStepSound`: the chime, louder the more often.
        if (world.in_tag(block, "minecraft:crystal_sound_blocks") && ticks_ >= last_chime_ + 20) {
            chime_intensity_ *= static_cast<float>(std::pow(0.997, ticks_ - last_chime_));
            chime_intensity_ = std::min(1.0f, chime_intensity_ + 0.07f);
            const float pitch = 0.5f + chime_intensity_ * random.next_float() * 1.2f;
            const float volume = 0.1f + chime_intensity_ * 1.2f;
            push(sounds, input, BlockSoundCue{"block.amethyst_block.chime", volume, pitch});
            last_chime_ = ticks_;
        }
        return true;
    };
    bool stepped = steps_on(legacy, legacy_block, true);
    if (!(on == legacy)) stepped |= steps_on(on, on_block, false);
    if (stepped || input.in_water) next_step_ = static_cast<float>(static_cast<int32_t>(move_dist_) + 1);
    return sounds;
}

} // namespace mcpp::audio
