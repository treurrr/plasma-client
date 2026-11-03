#include "mcpp/entity/humanoid.hpp"

#include "mcpp/entity/motion.hpp"
#include "mcpp/jmath.hpp"

#include <algorithm>
#include <cmath>

namespace mcpp::entity {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegrees = kPi / 180.0f;

float quadratic_arm_update(float x) { return -65.0f * x + x * x; }

float out_quart(float x) {
    const float a = (1.0f - x) * (1.0f - x);
    return 1.0f - a * a;
}

void bob(PartPose& part, float age, float side) {
    part.z_rot = part.z_rot + side * (jmath::cos(age * 0.09f) * 0.05f + 0.05f);
    part.x_rot = part.x_rot + side * (jmath::sin(age * 0.067f) * 0.05f);
}

void pose_blocking_arm(PartPose& arm, const PartPose& head, bool right) {
    arm.x_rot = arm.x_rot * 0.5f - 0.9424779f +
                std::clamp(head.x_rot, static_cast<float>(-kPi * 4.0 / 9.0), 0.43633232f);
    arm.y_rot = (right ? -30.0f : 30.0f) * kDegrees +
                std::clamp(head.y_rot, -kPi / 6.0f, kPi / 6.0f);
}

void pose_arm(ArmPose kind, PartPose& arm, const PartPose& head, bool right) {
    switch (kind) {
    case ArmPose::Empty:
        arm.y_rot = 0.0f;
        break;
    case ArmPose::Item:
        arm.x_rot = arm.x_rot * 0.5f - kPi / 10.0f;
        arm.y_rot = 0.0f;
        break;
    case ArmPose::Block:
        pose_blocking_arm(arm, head, right);
        break;
    }
}

} // namespace

HumanoidParts HumanoidParts::of(const Model& model) {
    HumanoidParts p;
    p.head = model.find("head");
    p.hat = model.find("head/hat");
    p.body = model.find("body");
    p.right_arm = model.find("right_arm");
    p.left_arm = model.find("left_arm");
    p.right_leg = model.find("right_leg");
    p.left_leg = model.find("left_leg");
    p.jacket = model.find("body/jacket");
    p.left_sleeve = model.find("left_arm/left_sleeve");
    p.right_sleeve = model.find("right_arm/right_sleeve");
    p.left_pants = model.find("left_leg/left_pants");
    p.right_pants = model.find("right_leg/right_pants");
    return p;
}

bool HumanoidParts::complete() const {
    return head >= 0 && body >= 0 && right_arm >= 0 && left_arm >= 0 && right_leg >= 0 &&
           left_leg >= 0;
}

void animate_humanoid(const Model& model, const HumanoidParts& parts, const HumanoidState& s,
                      Pose& pose) {
    PartPose& head = pose[parts.head];
    PartPose& body = pose[parts.body];
    PartPose& right_arm = pose[parts.right_arm];
    PartPose& left_arm = pose[parts.left_arm];
    PartPose& right_leg = pose[parts.right_leg];
    PartPose& left_leg = pose[parts.left_leg];
    for (size_t i = 0; i < model.parts.size(); ++i) {
        PartPose& part = pose.parts[i];
        const bool visible = part.visible;
        const bool skip = part.skip_draw;
        part = model.parts[i].initial;
        part.visible = visible;
        part.skip_draw = skip;
    }

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

    // No pose here holds with both hands yet, and none here moves the other
    // arm, so the order the game picks between the arms does not show.
    pose_arm(s.right_arm, right_arm, head, true);
    pose_arm(s.left_arm, left_arm, head, false);

    if (s.attack_time > 0.0f) {
        body.y_rot = jmath::sin(std::sqrt(s.attack_time) * (kPi * 2.0f)) * 0.2f;
        if (s.attack_left) body.y_rot *= -1.0f;
        right_arm.z = jmath::sin(body.y_rot) * 5.0f * s.age_scale;
        right_arm.x = -jmath::cos(body.y_rot) * 5.0f * s.age_scale;
        left_arm.z = -jmath::sin(body.y_rot) * 5.0f * s.age_scale;
        left_arm.x = jmath::cos(body.y_rot) * 5.0f * s.age_scale;
        right_arm.y_rot = right_arm.y_rot + body.y_rot;
        left_arm.y_rot = left_arm.y_rot + body.y_rot;
        left_arm.x_rot = left_arm.x_rot + body.y_rot;
        const float eased = out_quart(s.attack_time);
        const float lift = jmath::sin(eased * kPi);
        const float reach = jmath::sin(s.attack_time * kPi) * -(head.x_rot - 0.7f) * 0.75f;
        PartPose& arm = s.attack_left ? left_arm : right_arm;
        arm.x_rot -= lift * 1.2f + reach;
        arm.y_rot = arm.y_rot + body.y_rot * 2.0f;
        arm.z_rot = arm.z_rot + jmath::sin(s.attack_time * kPi) * -0.4f;
    }

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

    bob(right_arm, s.age_in_ticks, 1.0f);
    bob(left_arm, s.age_in_ticks, -1.0f);

    if (swim > 0.0f) {
        const float cycle = std::fmod(walked, 26.0f);
        const float right = !s.attack_left && s.attack_time > 0.0f ? 0.0f : swim;
        const float left = s.attack_left && s.attack_time > 0.0f ? 0.0f : swim;
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

void animate_player(const Model& model, const HumanoidParts& parts, const HumanoidState& s,
                    Pose& pose) {
    pose.reset(model);
    const bool shown = !s.spectator;
    pose[parts.body].visible = shown;
    pose[parts.right_arm].visible = shown;
    pose[parts.left_arm].visible = shown;
    pose[parts.right_leg].visible = shown;
    pose[parts.left_leg].visible = shown;
    const auto set = [&](int32_t part, bool visible) {
        if (part >= 0) pose[part].visible = visible;
    };
    set(parts.hat, s.show_hat);
    set(parts.jacket, s.show_jacket);
    set(parts.left_pants, s.show_left_pants);
    set(parts.right_pants, s.show_right_pants);
    set(parts.left_sleeve, s.show_left_sleeve);
    set(parts.right_sleeve, s.show_right_sleeve);
    animate_humanoid(model, parts, s, pose);
}

} // namespace mcpp::entity
