#include "mcpp/entity/motion.hpp"

#include <algorithm>
#include <cmath>

namespace mcpp::entity {
namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kRadians = kPi / 180.0f;

// The body turns this fraction of the way to where it is going each tick, and
// never lets the head get further round than this.
constexpr float kBodyTurn = 0.3f;
constexpr float kHeadLimit = 75.0f;
// Walking backwards keeps the body facing forwards.
constexpr float kBackwards = 95.0f;
// The legs' swing: radians of cycle per block walked, and how far they reach.
constexpr float kStride = 0.6662f;
constexpr float kLegReach = 1.4f;
constexpr float kArmReach = 1.0f;

double lerp(double a, double b, float t) { return a + (b - a) * static_cast<double>(t); }

} // namespace

int TickClock::advance(float seconds) {
    accumulated_ += std::clamp(static_cast<double>(seconds), 0.0, 1.0);
    int ticks = 0;
    while (accumulated_ >= kTick) {
        accumulated_ -= kTick;
        ++ticks;
    }
    ticks_ += static_cast<uint64_t>(ticks);
    return ticks;
}

float wrap_degrees(float degrees) {
    float out = std::fmod(degrees, 360.0f);
    if (out >= 180.0f) out -= 360.0f;
    if (out < -180.0f) out += 360.0f;
    return out;
}

float lerp_degrees(float from, float to, float t) {
    return from + wrap_degrees(to - from) * t;
}

void Motion::spawn(const Entity& entity) {
    position[0] = old_position[0] = target[0] = entity.x;
    position[1] = old_position[1] = target[1] = entity.y;
    position[2] = old_position[2] = target[2] = entity.z;
    yaw = old_yaw = target_yaw = entity.yaw;
    pitch = old_pitch = target_pitch = entity.pitch;
    head_yaw = old_head_yaw = target_head_yaw = entity.head_yaw;
    body_yaw = old_body_yaw = entity.head_yaw;
    steps = head_steps = 0;
}

void Motion::receive(const Entity& entity) {
    target[0] = entity.x;
    target[1] = entity.y;
    target[2] = entity.z;
    target_yaw = entity.yaw;
    target_pitch = entity.pitch;
    steps = kSteps;
    if (entity.head_yaw != target_head_yaw) {
        target_head_yaw = entity.head_yaw;
        head_steps = kSteps;
    }
}

void Motion::swing() {
    if (!swinging || swing_ticks >= kSwingTicks / 2 || swing_ticks < 0) {
        swing_ticks = -1;
        swinging = true;
    }
}

void Motion::tick() {
    for (int axis = 0; axis < 3; ++axis) old_position[axis] = position[axis];
    old_yaw = yaw;
    old_pitch = pitch;
    old_head_yaw = head_yaw;
    old_body_yaw = body_yaw;
    old_walk_speed = walk_speed;
    old_attack = attack;
    ++age;
    if (hurt_ticks > 0) --hurt_ticks;

    if (steps > 0) {
        const auto share = static_cast<double>(steps);
        for (int axis = 0; axis < 3; ++axis) {
            position[axis] += (target[axis] - position[axis]) / share;
        }
        yaw += wrap_degrees(target_yaw - yaw) / static_cast<float>(steps);
        pitch += (target_pitch - pitch) / static_cast<float>(steps);
        --steps;
    }
    if (head_steps > 0) {
        head_yaw += wrap_degrees(target_head_yaw - head_yaw) / static_cast<float>(head_steps);
        --head_steps;
    }

    const double dx = position[0] - old_position[0];
    const double dz = position[2] - old_position[2];
    const auto moved = static_cast<float>(std::sqrt(dx * dx + dz * dz));

    float heading = body_yaw;
    if (moved * moved > 0.0025f) {
        heading = static_cast<float>(std::atan2(dz, dx)) / kRadians - 90.0f;
        if (std::abs(wrap_degrees(heading - head_yaw)) > kBackwards) heading += 180.0f;
    }
    if (swinging) heading = head_yaw;
    body_yaw += wrap_degrees(heading - body_yaw) * kBodyTurn;
    const float off = wrap_degrees(head_yaw - body_yaw);
    if (std::abs(off) > kHeadLimit) body_yaw = head_yaw - std::copysign(kHeadLimit, off);

    walk_speed += (std::min(moved * 4.0f, 1.0f) - walk_speed) * 0.4f;
    walk_position += walk_speed;

    if (swinging) {
        ++swing_ticks;
        if (swing_ticks >= kSwingTicks) {
            swing_ticks = 0;
            swinging = false;
        }
    } else {
        swing_ticks = 0;
    }
    attack = static_cast<float>(swing_ticks) / static_cast<float>(kSwingTicks);
}

double Motion::x(float partial) const { return lerp(old_position[0], position[0], partial); }
double Motion::y(float partial) const { return lerp(old_position[1], position[1], partial); }
double Motion::z(float partial) const { return lerp(old_position[2], position[2], partial); }

void pose_humanoid(const Model& model, const Motion& motion, float partial, Pose& pose) {
    const float body = lerp_degrees(motion.old_body_yaw, motion.body_yaw, partial);
    const float head = lerp_degrees(motion.old_head_yaw, motion.head_yaw, partial);
    const float pitch = motion.old_pitch + (motion.pitch - motion.old_pitch) * partial;
    const float speed = std::min(
        motion.old_walk_speed + (motion.walk_speed - motion.old_walk_speed) * partial, 1.0f);
    const float walked = motion.walk_position - motion.walk_speed * (1.0f - partial);
    const float age = static_cast<float>(motion.age) + partial;

    const auto part = [&](const char* name) -> PartPose* {
        const int32_t index = model.find(name);
        return index < 0 ? nullptr : &pose[index];
    };

    if (auto* p = part("head")) {
        p->rotation[1] = wrap_degrees(head - body) * kRadians;
        p->rotation[0] = pitch * kRadians;
    }
    const float swing = std::cos(walked * kStride);
    if (auto* p = part("right_leg")) p->rotation[0] = swing * kLegReach * speed;
    if (auto* p = part("left_leg")) p->rotation[0] = -swing * kLegReach * speed;
    auto* right_arm = part("right_arm");
    auto* left_arm = part("left_arm");
    if (right_arm != nullptr) right_arm->rotation[0] = -swing * kArmReach * speed;
    if (left_arm != nullptr) left_arm->rotation[0] = swing * kArmReach * speed;

    // A swing: the arm comes up and over in front of the body while the body
    // twists into it, over six ticks.
    float attack = motion.old_attack + (motion.attack - motion.old_attack) * partial;
    if (motion.swinging && motion.attack < motion.old_attack) attack = motion.attack;
    if (attack > 0.0f && right_arm != nullptr) {
        const float twist = std::sin(std::sqrt(attack) * kPi * 2.0f) * 0.2f;
        if (auto* p = part("body")) p->rotation[1] = twist;
        right_arm->offset[2] = std::sin(twist) * 5.0f;
        right_arm->offset[0] = -(std::cos(twist) * 5.0f - 5.0f);
        if (left_arm != nullptr) {
            left_arm->offset[2] = -std::sin(twist) * 5.0f;
            left_arm->offset[0] = std::cos(twist) * 5.0f - 5.0f;
            left_arm->rotation[1] += twist;
        }
        right_arm->rotation[1] += twist * 2.0f;
        const float eased = 1.0f - (1.0f - attack) * (1.0f - attack) * (1.0f - attack) *
                                       (1.0f - attack);
        const float lift = std::sin(eased * kPi) * 1.2f +
                           std::sin(attack * kPi) * -(pitch * kRadians - 0.7f) * 0.75f;
        right_arm->rotation[0] -= lift;
        right_arm->rotation[2] += std::sin(attack * kPi) * -0.4f;
    }

    // Standing still is not standing frozen: the arms drift a little.
    const float sway = std::cos(age * 0.09f) * 0.05f + 0.05f;
    const float drift = std::sin(age * 0.067f) * 0.05f;
    if (right_arm != nullptr) {
        right_arm->rotation[2] += sway;
        right_arm->rotation[0] += drift;
    }
    if (left_arm != nullptr) {
        left_arm->rotation[2] -= sway;
        left_arm->rotation[0] -= drift;
    }
}

Matrix model_transform(const Model& model, const Motion& motion, float partial,
                       const double camera[3]) {
    const float body = lerp_degrees(motion.old_body_yaw, motion.body_yaw, partial);
    Matrix m = translation(static_cast<float>(motion.x(partial) - camera[0]),
                           static_cast<float>(motion.y(partial) - camera[1]),
                           static_cast<float>(motion.z(partial) - camera[2]));
    m = multiply(m, rotation_y((180.0f - body) * kRadians));
    if (motion.death_ticks > 0) {
        const float fall = std::min(
            1.0f, std::sqrt(std::max(0.0f, (static_cast<float>(motion.death_ticks) + partial - 1.0f) /
                                                static_cast<float>(Motion::kDeathTicks) * 1.6f)));
        m = multiply(m, rotation_z(fall * 90.0f * kRadians));
    }
    return multiply(m, placement(180.0f, model.scale));
}

} // namespace mcpp::entity
