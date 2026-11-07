#include "mcpp/entity/humanoid.hpp"

#include "mcpp/entity/ease.hpp"
#include "mcpp/entity/motion.hpp"
#include "mcpp/jmath.hpp"

#include <algorithm>
#include <cmath>

namespace mcpp::entity {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegrees = kPi / 180.0f;

// `HumanoidModel.quadraticArmUpdate`: the swim stroke's quadratic arm curve.
float quadratic_arm_update(float x) { return -65.0f * x + x * x; }

// `AnimationUtils.bobModelPart`: the idle arm sway.
void bob(PartPose& part, float age, float side) {
    part.z_rot = part.z_rot + side * (jmath::cos(age * 0.09f) * 0.05f + 0.05f);
    part.x_rot = part.x_rot + side * (jmath::sin(age * 0.067f) * 0.05f);
}

// `AnimationUtils.animateCrossbowHold`.
void crossbow_hold(PartPose& right_arm, PartPose& left_arm, const PartPose& head, bool right) {
    PartPose& front = right ? right_arm : left_arm;
    PartPose& back = right ? left_arm : right_arm;
    front.y_rot = (right ? -0.3f : 0.3f) + head.y_rot;
    back.y_rot = (right ? 0.6f : -0.6f) + head.y_rot;
    front.x_rot = -kPi / 2.0f + head.x_rot + 0.1f;
    back.x_rot = -1.5f + head.x_rot;
}

// `AnimationUtils.animateCrossbowCharge`.
void crossbow_charge(PartPose& right_arm, PartPose& left_arm, float duration, float used,
                     bool right) {
    PartPose& holding = right ? right_arm : left_arm;
    PartPose& pulling = right ? left_arm : right_arm;
    holding.y_rot = right ? -0.8f : 0.8f;
    holding.x_rot = -0.97079635f;
    pulling.x_rot = holding.x_rot;
    const float clamped = std::clamp(used, 0.0f, duration);
    const float t = clamped / duration;
    pulling.y_rot = lerp(t, 0.4f, 0.85f) * (right ? 1.0f : -1.0f);
    pulling.x_rot = lerp(t, pulling.x_rot, -kPi / 2.0f);
}

// `Mth.inverseLerp` clamped to zero and one, as `SpearAnimations.progress` uses it.
float progress(float value, float from, float to) {
    return std::clamp((value - from) / (to - from), 0.0f, 1.0f);
}

} // namespace

// `SpearAnimations.thirdPersonHandUse`: the arm a spear is held out on. The
// block after it, the sway the item's `minecraft:kinetic_weapon` component
// drives, is not ported: nothing here reads item components yet.
void spear_hand_use(PartPose& arm, const PartPose& head, bool right, const MobState& state) {
    const int sign = right ? 1 : -1;
    arm.y_rot = -0.1f * static_cast<float>(sign) + head.y_rot;
    arm.x_rot = -kPi / 2.0f + head.x_rot + 0.8f;
    if (state.fall_flying || state.swim_amount > 0.0f) arm.x_rot -= 0.9599311f;
    arm.y_rot = kDegrees * std::clamp(180.0f / kPi * arm.y_rot, -60.0f, 60.0f);
    arm.x_rot = kDegrees * std::clamp(180.0f / kPi * arm.x_rot, -120.0f, 30.0f);
}

// `SpearAnimations.thirdPersonAttackHand`: the stab, which undoes the body
// twist the whack swing put on the arms and drives the one arm forward.
void spear_attack_hand(PartPose& right_arm, PartPose& left_arm, const PartPose& body,
                       const MobState& state) {
    const float time = state.attack_time;
    right_arm.y_rot = right_arm.y_rot - body.y_rot;
    left_arm.y_rot = left_arm.y_rot - body.y_rot;
    left_arm.x_rot = left_arm.x_rot - body.y_rot;
    const float start = ease::in_out_sine(progress(time, 0.0f, 0.05f));
    const float middle = ease::in_quad(progress(time, 0.05f, 0.2f));
    const float end = ease::in_out_expo(progress(time, 0.4f, 1.0f));
    PartPose& arm = state.attack_arm_left ? left_arm : right_arm;
    arm.x_rot += (90.0f * start - 120.0f * middle + 30.0f * end) * kDegrees;
}

Humanoid::Humanoid(const Model& model) : MobModel(model) {
    head_ = part("head");
    hat_ = part("head/hat");
    body_ = part("body");
    right_arm_ = part("right_arm");
    left_arm_ = part("left_arm");
    right_leg_ = part("right_leg");
    left_leg_ = part("left_leg");
    right_chain_ = {right_arm_};
    left_chain_ = {left_arm_};
}

std::span<const int32_t> Humanoid::hand_chain(bool left) const {
    return left ? std::span<const int32_t>(left_chain_) : std::span<const int32_t>(right_chain_);
}

// `HumanoidModel.setAllVisible`.
void Humanoid::set_all_visible(Pose& pose, bool visible) const {
    pose[head_].visible = visible;
    pose[hat_].visible = visible;
    pose[body_].visible = visible;
    pose[right_arm_].visible = visible;
    pose[left_arm_].visible = visible;
    pose[right_leg_].visible = visible;
    pose[left_leg_].visible = visible;
}

// `HumanoidModel.poseBlockingArm`: an arm raised to block, following the head.
void Humanoid::pose_blocking_arm(PartPose& arm, const PartPose& head, bool right) const {
    arm.x_rot = arm.x_rot * 0.5f - 0.9424779f +
                std::clamp(head.x_rot, static_cast<float>(-kPi * 4.0 / 9.0), 0.43633232f);
    arm.y_rot = (right ? -30.0f : 30.0f) * kDegrees +
                std::clamp(head.y_rot, -kPi / 6.0f, kPi / 6.0f);
}

// `HumanoidModel.poseRightArm`.
void Humanoid::pose_right_arm(const MobState& s, Pose& pose) const {
    PartPose& head = pose[head_];
    PartPose& right = pose[right_arm_];
    PartPose& left = pose[left_arm_];
    switch (s.right_arm_pose) {
    case ArmPose::Empty:
        right.y_rot = 0.0f;
        break;
    case ArmPose::Item:
        right.x_rot = right.x_rot * 0.5f - kPi / 10.0f;
        right.y_rot = 0.0f;
        break;
    case ArmPose::Block:
        pose_blocking_arm(right, head, true);
        break;
    case ArmPose::BowAndArrow:
        right.y_rot = -0.1f + head.y_rot;
        left.y_rot = 0.1f + head.y_rot + 0.4f;
        right.x_rot = -kPi / 2.0f + head.x_rot;
        left.x_rot = -kPi / 2.0f + head.x_rot;
        break;
    case ArmPose::ThrowTrident:
        right.x_rot = right.x_rot * 0.5f - kPi;
        right.y_rot = 0.0f;
        break;
    case ArmPose::CrossbowCharge:
        crossbow_charge(right, left, s.max_crossbow_charge_duration, s.ticks_using_item, true);
        break;
    case ArmPose::CrossbowHold:
        crossbow_hold(right, left, head, true);
        break;
    case ArmPose::Spyglass:
        right.x_rot = std::clamp(head.x_rot - 1.9198622f - (s.crouching ? kPi / 12.0f : 0.0f),
                                 -2.4f, 3.3f);
        right.y_rot = head.y_rot - kPi / 12.0f;
        break;
    case ArmPose::TootHorn:
        right.x_rot = std::clamp(head.x_rot, -1.2f, 1.2f) - 1.4835298f;
        right.y_rot = head.y_rot - kPi / 6.0f;
        break;
    case ArmPose::Brush:
        right.x_rot = right.x_rot * 0.5f - kPi / 5.0f;
        right.y_rot = 0.0f;
        break;
    case ArmPose::Spear:
        spear_hand_use(right, head, true, s);
        break;
    }
}

// `HumanoidModel.poseLeftArm`.
void Humanoid::pose_left_arm(const MobState& s, Pose& pose) const {
    PartPose& head = pose[head_];
    PartPose& right = pose[right_arm_];
    PartPose& left = pose[left_arm_];
    switch (s.left_arm_pose) {
    case ArmPose::Empty:
        left.y_rot = 0.0f;
        break;
    case ArmPose::Item:
        left.x_rot = left.x_rot * 0.5f - kPi / 10.0f;
        left.y_rot = 0.0f;
        break;
    case ArmPose::Block:
        pose_blocking_arm(left, head, false);
        break;
    case ArmPose::BowAndArrow:
        right.y_rot = -0.1f + head.y_rot - 0.4f;
        left.y_rot = 0.1f + head.y_rot;
        right.x_rot = -kPi / 2.0f + head.x_rot;
        left.x_rot = -kPi / 2.0f + head.x_rot;
        break;
    case ArmPose::ThrowTrident:
        left.x_rot = left.x_rot * 0.5f - kPi;
        left.y_rot = 0.0f;
        break;
    case ArmPose::CrossbowCharge:
        crossbow_charge(right, left, s.max_crossbow_charge_duration, s.ticks_using_item, false);
        break;
    case ArmPose::CrossbowHold:
        crossbow_hold(right, left, head, false);
        break;
    case ArmPose::Spyglass:
        left.x_rot = std::clamp(head.x_rot - 1.9198622f - (s.crouching ? kPi / 12.0f : 0.0f),
                                -2.4f, 3.3f);
        left.y_rot = head.y_rot + kPi / 12.0f;
        break;
    case ArmPose::TootHorn:
        left.x_rot = std::clamp(head.x_rot, -1.2f, 1.2f) - 1.4835298f;
        left.y_rot = head.y_rot + kPi / 6.0f;
        break;
    case ArmPose::Brush:
        left.x_rot = left.x_rot * 0.5f - kPi / 5.0f;
        left.y_rot = 0.0f;
        break;
    case ArmPose::Spear:
        spear_hand_use(left, head, false, s);
        break;
    }
}

// `HumanoidModel.setupAttackAnimation`: the body twist, then whichever swing
// the item in hand asks for.
void Humanoid::setup_attack(const MobState& s, Pose& pose) const {
    const float time = s.attack_time;
    if (time <= 0.0f) return;
    PartPose& head = pose[head_];
    PartPose& body = pose[body_];
    PartPose& right_arm = pose[right_arm_];
    PartPose& left_arm = pose[left_arm_];

    body.y_rot = jmath::sin(std::sqrt(time) * (kPi * 2.0f)) * 0.2f;
    if (s.attack_arm_left) body.y_rot *= -1.0f;
    const float age_scale = s.age_scale;
    right_arm.z = jmath::sin(body.y_rot) * 5.0f * age_scale;
    right_arm.x = -jmath::cos(body.y_rot) * 5.0f * age_scale;
    left_arm.z = -jmath::sin(body.y_rot) * 5.0f * age_scale;
    left_arm.x = jmath::cos(body.y_rot) * 5.0f * age_scale;
    right_arm.y_rot = right_arm.y_rot + body.y_rot;
    left_arm.y_rot = left_arm.y_rot + body.y_rot;
    left_arm.x_rot = left_arm.x_rot + body.y_rot;

    switch (s.swing_animation) {
    case SwingAnimation::Whack: {
        const float eased = ease::out_quart(time);
        const float lift = jmath::sin(eased * kPi);
        const float reach = jmath::sin(time * kPi) * -(head.x_rot - 0.7f) * 0.75f;
        PartPose& arm = s.attack_arm_left ? left_arm : right_arm;
        arm.x_rot -= lift * 1.2f + reach;
        arm.y_rot = arm.y_rot + body.y_rot * 2.0f;
        arm.z_rot = arm.z_rot + jmath::sin(time * kPi) * -0.4f;
        break;
    }
    case SwingAnimation::None:
        break;
    case SwingAnimation::Stab:
        spear_attack_hand(right_arm, left_arm, body, s);
        break;
    }
}

void Humanoid::animate(const MobState& state, Pose& pose) const { humanoid_anim(state, pose); }

// `HumanoidModel.setupAnim`: head, walk, ride, arms, attack, crouch, bob and swim.
void Humanoid::humanoid_anim(const MobState& s, Pose& pose) const {
    reset(pose);
    PartPose& head = pose[head_];
    PartPose& body = pose[body_];
    PartPose& right_arm = pose[right_arm_];
    PartPose& left_arm = pose[left_arm_];
    PartPose& right_leg = pose[right_leg_];
    PartPose& left_leg = pose[left_leg_];

    const ArmPose left_pose = s.left_arm_pose;
    const ArmPose right_pose = s.right_arm_pose;
    const float swim = s.swim_amount;

    head.x_rot = s.x_rot * kDegrees;
    head.y_rot = s.y_rot * kDegrees;
    if (s.fall_flying) {
        head.x_rot = -kPi / 4.0f;
    } else if (swim > 0.0f) {
        head.x_rot = rot_lerp_rad(swim, head.x_rot, -kPi / 4.0f);
    }

    const float walked = s.walk_position;
    const float speed = s.walk_speed;
    right_arm.x_rot = jmath::cos(walked * 0.6662f + kPi) * 2.0f * speed * 0.5f / s.speed_value;
    left_arm.x_rot = jmath::cos(walked * 0.6662f) * 2.0f * speed * 0.5f / s.speed_value;
    right_leg.x_rot = jmath::cos(walked * 0.6662f) * 1.4f * speed / s.speed_value;
    left_leg.x_rot = jmath::cos(walked * 0.6662f + kPi) * 1.4f * speed / s.speed_value;
    right_leg.y_rot = 0.005f;
    left_leg.y_rot = -0.005f;
    right_leg.z_rot = 0.005f;
    left_leg.z_rot = -0.005f;
    if (s.passenger) {
        right_arm.x_rot += -kPi / 5.0f;
        left_arm.x_rot += -kPi / 5.0f;
        right_leg.x_rot = -1.4137167f;
        right_leg.y_rot = kPi / 10.0f;
        right_leg.z_rot = 0.07853982f;
        left_leg.x_rot = -1.4137167f;
        left_leg.y_rot = -kPi / 10.0f;
        left_leg.z_rot = -0.07853982f;
    }

    // Which arm is posed first, and whether the other is posed at all: a
    // two-handed pose owns both, so the hand it belongs to goes first.
    const bool main_right = s.main_arm_right;
    if (s.using_item) {
        const bool main_hand = !s.use_item_off_hand;
        if (main_hand == main_right) {
            pose_right_arm(s, pose);
            if (!affects_offhand_pose(right_pose)) pose_left_arm(s, pose);
        } else {
            pose_left_arm(s, pose);
            if (!affects_offhand_pose(left_pose)) pose_right_arm(s, pose);
        }
    } else {
        const bool holds_two = main_right ? two_handed(left_pose) : two_handed(right_pose);
        if (main_right != holds_two) {
            pose_left_arm(s, pose);
            if (!affects_offhand_pose(left_pose)) pose_right_arm(s, pose);
        } else {
            pose_right_arm(s, pose);
            if (!affects_offhand_pose(right_pose)) pose_left_arm(s, pose);
        }
    }

    setup_attack(s, pose);

    if (s.crouching) {
        body.x_rot = 0.5f;
        right_arm.x_rot += 0.4f;
        left_arm.x_rot += 0.4f;
        right_leg.z += 4.0f;
        left_leg.z += 4.0f;
        head.y += 4.2f;
        body.y += 3.2f;
        left_arm.y += 3.2f;
        right_arm.y += 3.2f;
    }

    if (right_pose != ArmPose::Spyglass) bob(right_arm, s.age_in_ticks, 1.0f);
    if (left_pose != ArmPose::Spyglass) bob(left_arm, s.age_in_ticks, -1.0f);

    if (swim > 0.0f) {
        const float cycle = std::fmod(walked, 26.0f);
        const float right =
            right_pose != ArmPose::Spear && (s.attack_arm_left || !(s.attack_time > 0.0f)) ? swim
                                                                                           : 0.0f;
        const float left =
            left_pose != ArmPose::Spear && (!s.attack_arm_left || !(s.attack_time > 0.0f)) ? swim
                                                                                           : 0.0f;
        if (!s.using_item) {
            if (cycle < 14.0f) {
                left_arm.x_rot = rot_lerp_rad(left, left_arm.x_rot, 0.0f);
                right_arm.x_rot = lerp(right, right_arm.x_rot, 0.0f);
                left_arm.y_rot = rot_lerp_rad(left, left_arm.y_rot, kPi);
                right_arm.y_rot = lerp(right, right_arm.y_rot, kPi);
                left_arm.z_rot = rot_lerp_rad(left, left_arm.z_rot,
                                              kPi + 1.8707964f * quadratic_arm_update(cycle) /
                                                        quadratic_arm_update(14.0f));
                right_arm.z_rot = lerp(right, right_arm.z_rot,
                                       kPi - 1.8707964f * quadratic_arm_update(cycle) /
                                                 quadratic_arm_update(14.0f));
            } else if (cycle >= 14.0f && cycle < 22.0f) {
                const float t = (cycle - 14.0f) / 8.0f;
                left_arm.x_rot = rot_lerp_rad(left, left_arm.x_rot, kPi / 2.0f * t);
                right_arm.x_rot = lerp(right, right_arm.x_rot, kPi / 2.0f * t);
                left_arm.y_rot = rot_lerp_rad(left, left_arm.y_rot, kPi);
                right_arm.y_rot = lerp(right, right_arm.y_rot, kPi);
                left_arm.z_rot = rot_lerp_rad(left, left_arm.z_rot, 5.012389f - 1.8707964f * t);
                right_arm.z_rot = lerp(right, right_arm.z_rot, 1.2707963f + 1.8707964f * t);
            } else if (cycle >= 22.0f && cycle < 26.0f) {
                const float t = (cycle - 22.0f) / 4.0f;
                left_arm.x_rot = rot_lerp_rad(left, left_arm.x_rot, kPi / 2.0f - kPi / 2.0f * t);
                right_arm.x_rot = lerp(right, right_arm.x_rot, kPi / 2.0f - kPi / 2.0f * t);
                left_arm.y_rot = rot_lerp_rad(left, left_arm.y_rot, kPi);
                right_arm.y_rot = lerp(right, right_arm.y_rot, kPi);
                left_arm.z_rot = rot_lerp_rad(left, left_arm.z_rot, kPi);
                right_arm.z_rot = lerp(right, right_arm.z_rot, kPi);
            }
        }
        left_leg.x_rot = lerp(swim, left_leg.x_rot, 0.3f * jmath::cos(walked * 0.33333334f + kPi));
        right_leg.x_rot = lerp(swim, right_leg.x_rot, 0.3f * jmath::cos(walked * 0.33333334f));
    }
}

Avatar::Avatar(const Model& model, bool slim) : Humanoid(model), slim_(slim) {
    jacket_ = part("body/jacket");
    left_sleeve_ = part("left_arm/left_sleeve");
    right_sleeve_ = part("right_arm/right_sleeve");
    left_pants_ = part("left_leg/left_pants");
    right_pants_ = part("right_leg/right_pants");
}

// `PlayerModel.setupAnim`: which layers of the skin show, then the humanoid.
void Avatar::animate(const MobState& s, Pose& pose) const {
    if (pose.parts.size() != model().parts.size()) pose.reset(model());
    const bool shown = !s.spectator;
    pose[body_].visible = shown;
    pose[right_arm_].visible = shown;
    pose[left_arm_].visible = shown;
    pose[right_leg_].visible = shown;
    pose[left_leg_].visible = shown;
    pose[hat_].visible = s.show_hat;
    pose[jacket_].visible = s.show_jacket;
    pose[left_pants_].visible = s.show_left_pants;
    pose[right_pants_].visible = s.show_right_pants;
    pose[left_sleeve_].visible = s.show_left_sleeve;
    pose[right_sleeve_].visible = s.show_right_sleeve;
    humanoid_anim(s, pose);
}

// `AvatarRenderer.scale`.
void Avatar::scale(const MobState&, Placement& place) const {
    place.scale(0.9375f, 0.9375f, 0.9375f);
}

// `AvatarRenderer.setupRotations`: gliding tips the whole body forward,
// swimming lays it down, and both go on top of the usual turn.
void Avatar::setup_rotations(const MobState& s, Placement& place, float body_rot,
                             float scale) const {
    const float swim = s.swim_amount;
    const float pitch = s.x_rot;
    if (s.fall_flying) {
        MobModel::setup_rotations(s, place, body_rot, scale);
        const float glide = s.fall_flying_scale();
        if (!s.auto_spin_attack) {
            place.rotate(quaternion_x(to_radians(glide * (-90.0f - pitch))));
        }
        if (s.apply_flying_y_rot) place.rotate(quaternion_y(s.flying_y_rot));
    } else if (swim > 0.0f) {
        MobModel::setup_rotations(s, place, body_rot, scale);
        const float target = s.in_water ? -90.0f - pitch : -90.0f;
        place.rotate(quaternion_x(to_radians(lerp(swim, 0.0f, target))));
        if (s.visually_swimming) place.translate(0.0f, -1.0f, 0.3f);
    } else {
        MobModel::setup_rotations(s, place, body_rot, scale);
    }
}

} // namespace mcpp::entity
