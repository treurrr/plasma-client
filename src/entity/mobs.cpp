#include "mcpp/entity/mobs.hpp"

#include "mcpp/entity/humanoid.hpp"
#include "mob_factory.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <ranges>
#include <vector>
#include <format>
#include <stdexcept>

namespace mcpp::entity {
namespace {

// `ArmPose`: twoHanded, affectsOffhandPose, in the enum's own order.
constexpr std::array<std::array<bool, 2>, 11> kArmPoseFlags = {{
    {false, false},   // EMPTY
    {false, false},   // ITEM
    {false, false},   // BLOCK
    {true, true},     // BOW_AND_ARROW
    {false, true},    // THROW_TRIDENT
    {true, true},     // CROSSBOW_CHARGE
    {true, true},     // CROSSBOW_HOLD
    {false, false},   // SPYGLASS
    {false, false},   // TOOT_HORN
    {false, false},   // BRUSH
    {false, true},    // SPEAR
}};

} // namespace

bool two_handed(ArmPose pose) { return kArmPoseFlags[static_cast<size_t>(pose)][0]; }
bool affects_offhand_pose(ArmPose pose) { return kArmPoseFlags[static_cast<size_t>(pose)][1]; }

// `AnimationState.start`: the tick it began, with nothing accumulated yet.
void AnimationState::start(int64_t tick) {
    started = true;
    start_tick = tick;
    accumulated = 0.0f;
}

void AnimationState::start_if_stopped(int64_t tick) {
    if (!started) start(tick);
}

void AnimationState::stop() {
    started = false;
    accumulated = 0.0f;
}

// `AnimationState.getAccumulatedTime`: how long it has run, in milliseconds,
// which is the unit `KeyframeAnimation` measures its keyframes in.
float AnimationState::elapsed(int64_t tick, float partial) const {
    if (!started) return 0.0f;
    return (static_cast<float>(tick - start_tick) + partial) * 50.0f;
}

// `AvatarRenderState.fallFlyingScale`: how far into the glide the body has
// tipped over.
float MobState::fall_flying_scale() const {
    return std::clamp(fall_flying_ticks * fall_flying_ticks / 100.0f, 0.0f, 1.0f);
}

void MobModel::scale(const MobState&, Placement&) const {}

namespace {

// `Direction.getStepX`, `.getStepZ` for north, south, west, east.
constexpr std::array<std::array<int8_t, 2>, 4> kBedSteps = {{{0, -1}, {0, 1}, {-1, 0}, {1, 0}}};

// `LivingEntityRenderer.sleepDirectionToRotation`.
float sleep_rotation(int8_t orientation) {
    switch (orientation) {
    case 3: return 90.0f;    // south
    case 4: return 0.0f;     // west
    case 2: return 270.0f;   // north
    case 5: return 180.0f;   // east
    default: return 0.0f;
    }
}

} // namespace

std::array<int8_t, 2> bed_step(int8_t orientation) {
    if (orientation < 2 || orientation > 5) return {0, 0};
    return {kBedSteps[static_cast<size_t>(orientation) - 2][0],
            kBedSteps[static_cast<size_t>(orientation) - 2][1]};
}

// `LivingEntityRenderer.setupRotations`: what turns the model before it is
// flipped onto its feet.
void MobModel::setup_rotations(const MobState& state, Placement& place, float body_rot,
                               float scale) const {
    if (is_shaking(state)) {
        body_rot += static_cast<float>(std::cos(std::floor(state.age_in_ticks) * 3.25) * M_PI * 0.4);
    }
    if (!state.has_pose(EntityPose::Sleeping)) {
        place.rotate(quaternion_y(to_radians(180.0f - body_rot)));
    }
    if (state.death_time > 0.0f) {
        float fall = (state.death_time - 1.0f) / 20.0f * 1.6f;
        fall = std::sqrt(fall);
        if (fall > 1.0f) fall = 1.0f;
        place.rotate(quaternion_z(to_radians(fall * flip_degrees())));
    } else if (state.auto_spin_attack) {
        place.rotate(quaternion_x(to_radians(-90.0f - state.x_rot)));
        place.rotate(quaternion_y(to_radians(state.age_in_ticks * -75.0f)));
    } else if (state.has_pose(EntityPose::Sleeping)) {
        const float turn =
            state.bed_orientation >= 0 ? sleep_rotation(state.bed_orientation) : body_rot;
        place.rotate(quaternion_y(to_radians(turn)));
        place.rotate(quaternion_z(to_radians(flip_degrees())));
        place.rotate(quaternion_y(to_radians(270.0f)));
    } else if (state.upside_down) {
        place.translate(0.0f, (state.bounding_box_height + 0.1f) / scale, 0.0f);
        place.rotate(quaternion_z(to_radians(180.0f)));
    }
}
float MobModel::white_overlay(const MobState&) const { return 0.0f; }
uint32_t MobModel::tint(const MobState&) const { return 0xFFFFFFFFu; }

std::span<const int32_t> MobModel::hand_chain(bool) const { return {}; }

int32_t MobModel::part(std::string_view path) const {
    const int32_t found = model_->find(path);
    if (found < 0) {
        throw std::runtime_error(std::format("layer {} has no part {}", model_->name, path));
    }
    return found;
}

int32_t MobModel::optional(std::string_view path) const { return model_->find(path); }

// `Model.resetPose`: every part back to the layer's own offset, turn and
// scale. Visibility is not part of it: a layer that hides a part keeps it
// hidden until something sets it again, which is what the game relies on.
std::unique_ptr<MobModel> make_mob_model(std::string_view layer, const Model& model) {
    if (layer == "player") return std::make_unique<Avatar>(model, false);
    if (layer == "player_slim") return std::make_unique<Avatar>(model, true);
    if (auto found = make_humanoid_mob_model(layer, model)) return found;
    if (auto found = make_animal_model(layer, model)) return found;
    if (auto found = make_monster_model(layer, model)) return found;
    if (auto found = make_keyframe_model(layer, model)) return found;
    return nullptr;
}

std::span<const MobType> mob_types() {
    static const std::vector<MobType> all = [] {
        std::vector<MobType> out;
        for (const auto family : {humanoid_mob_types(), animal_mob_types(), monster_mob_types(),
                                  keyframe_mob_types()}) {
            out.insert(out.end(), family.begin(), family.end());
        }
        std::ranges::sort(out, {}, &MobType::type);
        return out;
    }();
    return all;
}

const MobType* mob_type(std::string_view type) {
    const auto all = mob_types();
    const auto found = std::ranges::lower_bound(all, type, {}, &MobType::type);
    return found != all.end() && found->type == type ? &*found : nullptr;
}

void MobModel::reset(Pose& pose) const {
    if (pose.parts.size() != model_->parts.size()) {
        pose.reset(*model_);
        return;
    }
    for (size_t i = 0; i < pose.parts.size(); ++i) {
        PartPose& part = pose.parts[i];
        const bool visible = part.visible;
        const bool skip = part.skip_draw;
        part = model_->parts[i].initial;
        part.visible = visible;
        part.skip_draw = skip;
    }
}

} // namespace mcpp::entity
