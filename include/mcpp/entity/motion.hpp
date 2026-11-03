#pragma once
#include "mcpp/session.hpp"

#include <cstdint>

namespace mcpp::entity {

// The game's clock: twenty ticks a second, and how far the frame is into the
// next one.
class TickClock {
public:
    // Ticks to run for this much wall time; at most ten at once, so a stall
    // does not turn into a burst.
    int advance(float seconds);
    float partial() const { return static_cast<float>(accumulated_ / kTick); }
    uint64_t ticks() const { return ticks_; }

    static constexpr double kTick = 0.05;

private:
    double accumulated_ = 0.0;
    uint64_t ticks_ = 0;
};

// The game's angle arithmetic, with its float and double overloads kept
// apart: they round differently.
float wrap_degrees(float degrees);
double wrap_degrees(double degrees);
float rot_lerp(float t, float from, float to);
double rot_lerp(double t, double from, double to);
float rot_lerp_rad(float t, float from, float to);
float lerp(float t, float from, float to);

struct WalkAnimation {
    float speed_old = 0.0f;
    float speed = 0.0f;
    float position = 0.0f;
    float position_scale = 1.0f;

    void update(float target, float factor, float scale);
    void stop();
    float speed_at(float partial) const;
    float position_at(float partial) const;
};

// One living entity as the client keeps it, field for field what the game
// keeps: the position eased toward what the server last said over a few
// ticks, the head eased toward its own packets, the body following the head,
// the walk cycle, the arm swing, the hurt and death counters.
struct Living {
    double x = 0.0, y = 0.0, z = 0.0;
    double xo = 0.0, yo = 0.0, zo = 0.0;
    float y_rot = 0.0f, x_rot = 0.0f;
    float y_rot_o = 0.0f, x_rot_o = 0.0f;
    float y_head_rot = 0.0f, y_head_rot_o = 0.0f;
    float y_body_rot = 0.0f, y_body_rot_o = 0.0f;
    int32_t tick_count = 0;

    // The interpolation toward the server's word.
    int32_t interpolation_length = 3;
    int32_t steps = 0;
    double target[3]{};
    float target_y_rot = 0.0f;
    float target_x_rot = 0.0f;
    bool has_previous = false;
    double previous[3]{};
    float previous_x_rot = 0.0f;
    float previous_y_rot = 0.0f;

    float lerp_y_head_rot = 0.0f;
    int32_t lerp_head_steps = 0;

    WalkAnimation walk;
    bool swinging = false;
    bool swinging_off_hand = false;
    int32_t swing_time = 0;
    int32_t swing_duration = 6;
    float attack_anim = 0.0f;
    float o_attack_anim = 0.0f;
    int32_t hurt_time = 0;
    int32_t death_time = 0;
    bool dead = false;
    bool passenger = false;
    bool baby = false;
    float max_head_rotation = 50.0f;

    // add_entity: placed, not eased, with the body facing where the head does.
    void spawn(const Entity& entity);
    // A position, an angle or both from the server, eased in over a few ticks.
    void move_to(const double* position, const float* y_rot, const float* x_rot);
    void snap_to(double nx, double ny, double nz, float yaw, float pitch);
    void lerp_head_to(float yaw, int32_t ticks);
    void swing(bool off_hand);
    void hurt() { hurt_time = 10; }
    void damaged();

    // One tick of a player someone else controls.
    void tick_remote_player();
    // One tick of a mob; the body follows by the mob's own rule.
    void tick_mob();

    // This frame's values.
    double x_at(float partial) const;
    double y_at(float partial) const;
    double z_at(float partial) const;
    float body_at(float partial) const;
    float head_at(float partial) const;
    float x_rot_at(float partial) const;
    float attack_at(float partial) const;

private:
    void set_old();
    void base_tick();
    void interpolate();
    void head_step();
    void update_swing_time();
    void turn_body_and_wrap();
    void tick_head_turn(float target);
    void tick_body_rotation_control();
    void walk_animation(bool vertical);
    int32_t no_turn_ticks_ = 0;
    float last_stable_y_head_rot_ = 0.0f;
};

} // namespace mcpp::entity
