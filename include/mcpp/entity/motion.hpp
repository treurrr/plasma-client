#pragma once
#include "mcpp/entity/model.hpp"
#include "mcpp/session.hpp"

#include <cstdint>

namespace mcpp::entity {

// The game's clock: twenty ticks a second, and how far the frame is into the
// next one. Everything that moves on screen is a tick's value blended with the
// one before by that fraction.
class TickClock {
public:
    // Ticks to run for this much wall time; at most a second's worth, so a
    // stall does not turn into a burst.
    int advance(float seconds);
    float partial() const { return static_cast<float>(accumulated_ / kTick); }
    uint64_t ticks() const { return ticks_; }

    static constexpr double kTick = 0.05;

private:
    double accumulated_ = 0.0;
    uint64_t ticks_ = 0;
};

float wrap_degrees(float degrees);
float lerp_degrees(float from, float to, float t);

// One entity as it is drawn: the server's positions arrive twenty times a
// second at best and are eased into over a few ticks, the body turns after
// the head, and the legs swing by how far it walked.
struct Motion {
    // How many ticks a new position is spread over.
    static constexpr int kSteps = 3;
    static constexpr int kSwingTicks = 6;
    static constexpr int kHurtTicks = 10;
    static constexpr int kDeathTicks = 20;

    double position[3]{};
    double old_position[3]{};
    float yaw = 0.0f;
    float old_yaw = 0.0f;
    float pitch = 0.0f;
    float old_pitch = 0.0f;
    float head_yaw = 0.0f;
    float old_head_yaw = 0.0f;
    float body_yaw = 0.0f;
    float old_body_yaw = 0.0f;

    double target[3]{};
    float target_yaw = 0.0f;
    float target_pitch = 0.0f;
    int steps = 0;
    float target_head_yaw = 0.0f;
    int head_steps = 0;

    float walk_position = 0.0f;
    float walk_speed = 0.0f;
    float old_walk_speed = 0.0f;

    int age = 0;
    bool swinging = false;
    int swing_ticks = 0;
    float attack = 0.0f;
    float old_attack = 0.0f;
    int hurt_ticks = 0;
    int death_ticks = 0;

    void spawn(const Entity& entity);
    // A new position or angle from the server, eased into rather than jumped to.
    void receive(const Entity& entity);
    void swing();
    void hurt() { hurt_ticks = kHurtTicks; }
    void tick();

    // This frame's values, between the last tick and the one before.
    double x(float partial) const;
    double y(float partial) const;
    double z(float partial) const;
};

// Puts a humanoid model - a player, a zombie - in this frame's pose.
void pose_humanoid(const Model& model, const Motion& motion, float partial, Pose& pose);

// Where a model sits in the world this frame, the death fall included.
Matrix model_transform(const Model& model, const Motion& motion, float partial,
                       const double camera[3]);

} // namespace mcpp::entity
