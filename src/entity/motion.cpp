#include "mcpp/entity/motion.hpp"

#include <algorithm>
#include <cmath>

namespace mcpp::entity {
namespace {

constexpr float kPi = 3.14159265358979323846f;

int sign(double value) { return value == 0.0 ? 0 : (value > 0.0 ? 1 : -1); }

// `Mth.rotateIfNecessary`: clamp an angle to within a limit of a target.
float rotate_if_necessary(float current, float target, float limit) {
    const float difference = std::clamp(wrap_degrees(target - current), -limit, limit);
    return target - difference;
}

// `Entity.setXRot`: pitch wrapped and clamped to straight up or down.
float clamp_x_rot(float degrees) { return std::clamp(std::fmod(degrees, 360.0f), -90.0f, 90.0f); }

} // namespace

int TickClock::advance(float seconds) {
    accumulated_ += std::clamp(static_cast<double>(seconds), 0.0, 1.0);
    int ticks = 0;
    while (accumulated_ >= kTick) {
        accumulated_ -= kTick;
        ++ticks;
    }
    ticks = std::min(ticks, 10);
    ticks_ += static_cast<uint64_t>(ticks);
    return ticks;
}

// `Mth.wrapDegrees`: an angle into [-180, 180).
float wrap_degrees(float degrees) {
    float out = std::fmod(degrees, 360.0f);
    if (out >= 180.0f) out -= 360.0f;
    if (out < -180.0f) out += 360.0f;
    return out;
}

// `Mth.wrapDegrees`: an angle into [-180, 180).
double wrap_degrees(double degrees) {
    double out = std::fmod(degrees, 360.0);
    if (out >= 180.0) out -= 360.0;
    if (out < -180.0) out += 360.0;
    return out;
}

// `Mth.rotLerp`: lerp along the short way round.
float rot_lerp(float t, float from, float to) { return from + t * wrap_degrees(to - from); }
double rot_lerp(double t, double from, double to) { return from + t * wrap_degrees(to - from); }

// `Mth.rotLerpRad`: lerp in radians along the short way round.
float rot_lerp_rad(float t, float from, float to) {
    float difference = to - from;
    while (difference < -kPi) difference += kPi * 2.0f;
    while (difference >= kPi) difference -= kPi * 2.0f;
    return from + t * difference;
}

// `Mth.lerp`: linear interpolation.
float lerp(float t, float from, float to) { return from + t * (to - from); }

// `WalkAnimationState.update`: ease the walk speed and advance the position.
void WalkAnimation::update(float target, float factor, float scale) {
    speed_old = speed;
    speed = speed + (target - speed) * factor;
    position = position + speed;
    position_scale = scale;
}

// `WalkAnimationState.stop`: stand still.
void WalkAnimation::stop() {
    speed_old = 0.0f;
    speed = 0.0f;
    position = 0.0f;
}

// `WalkAnimationState.speed`: the speed at a partial tick, at most one.
float WalkAnimation::speed_at(float partial) const {
    return std::min(lerp(partial, speed_old, speed), 1.0f);
}

// `WalkAnimationState.position`: the walked distance at a partial tick.
float WalkAnimation::position_at(float partial) const {
    return (position - speed * (1.0f - partial)) * position_scale;
}

// `LivingEntity.recreateFromPacket`, `Entity.absSnapTo`: placed with body and head as sent.
void Living::spawn(const Entity& entity) {
    y_body_rot = y_head_rot = entity.head_yaw;
    y_body_rot_o = y_body_rot;
    y_head_rot_o = y_head_rot;
    const double cx = std::clamp(entity.x, -3.0e7, 3.0e7);
    const double cz = std::clamp(entity.z, -3.0e7, 3.0e7);
    xo = x = cx;
    yo = y = entity.y;
    zo = z = cz;
    y_rot = std::fmod(entity.yaw, 360.0f);
    x_rot = std::fmod(std::clamp(entity.pitch, -90.0f, 90.0f), 360.0f);
    y_rot_o = y_rot;
    x_rot_o = x_rot;
    last_stable_y_head_rot_ = y_head_rot;
}

// `Entity.snapTo`: jump to a position and angles, old ones too.
void Living::snap_to(double nx, double ny, double nz, float yaw, float pitch) {
    x = nx;
    y = ny;
    z = nz;
    y_rot = yaw;
    x_rot = clamp_x_rot(pitch);
    set_old();
}

// `InterpolationHandler.interpolateTo`: start easing towards a new target.
void Living::move_to(const double* position, const float* yaw, const float* pitch) {
    const bool active = steps > 0;
    const double to[3] = {
        position != nullptr ? position[0] : (active ? target[0] : x),
        position != nullptr ? position[1] : (active ? target[1] : y),
        position != nullptr ? position[2] : (active ? target[2] : z),
    };
    const float to_y = yaw != nullptr ? *yaw : (active ? target_y_rot : y_rot);
    const float to_x = pitch != nullptr ? *pitch : (active ? target_x_rot : x_rot);
    if (interpolation_length == 0) {
        snap_to(to[0], to[1], to[2], to_y, to_x);
        steps = 0;
        has_previous = false;
        return;
    }
    const float current_y = active ? target_y_rot : y_rot;
    const float current_x = active ? target_x_rot : x_rot;
    const bool same_position = active ? (target[0] == to[0] && target[1] == to[1] &&
                                         target[2] == to[2])
                                      : (x == to[0] && y == to[1] && z == to[2]);
    if (active && current_y == to_y && current_x == to_x && same_position) return;
    steps = interpolation_length;
    target[0] = to[0];
    target[1] = to[1];
    target[2] = to[2];
    target_y_rot = to_y;
    target_x_rot = to_x;
    has_previous = true;
    previous[0] = x;
    previous[1] = y;
    previous[2] = z;
    previous_x_rot = x_rot;
    previous_y_rot = y_rot;
}

// `LivingEntity.lerpHeadTo`: ease the head to a yaw over some ticks.
void Living::lerp_head_to(float yaw, int32_t ticks) {
    lerp_y_head_rot = yaw;
    lerp_head_steps = ticks;
}

// `LivingEntity.swing`: start a swing unless one is well under way.
void Living::swing(bool off_hand) {
    if (!swinging || swing_time >= swing_duration / 2 || swing_time < 0) {
        swing_time = -1;
        swinging = true;
        swinging_off_hand = off_hand;
    }
}

// `LivingEntity.handleDamageEvent`: the walk kick and hurt time.
void Living::damaged() {
    walk.speed = 1.5f;
    hurt_time = 10;
}

// `Entity.setOldPosAndRot`: this tick's position and angles become the old ones.
void Living::set_old() {
    xo = x;
    yo = y;
    zo = z;
    y_rot_o = y_rot;
    x_rot_o = x_rot;
}

void Living::base_tick() {
    o_attack_anim = attack_anim;
    if (hurt_time > 0) --hurt_time;
    if (dead) ++death_time;
    y_head_rot_o = y_head_rot;
    y_body_rot_o = y_body_rot;
    y_rot_o = y_rot;
    x_rot_o = x_rot;
}

// `InterpolationHandler.interpolate`: one step towards the target.
void Living::interpolate() {
    if (steps <= 0) {
        steps = 0;
        has_previous = false;
        return;
    }
    const double share = 1.0 / steps;
    if (has_previous) {
        target[0] += x - previous[0];
        target[1] += y - previous[1];
        target[2] += z - previous[2];
        target_y_rot += y_rot - previous_y_rot;
        target_x_rot += x_rot - previous_x_rot;
    }
    const double nx = x + share * (target[0] - x);
    const double ny = y + share * (target[1] - y);
    const double nz = z + share * (target[2] - z);
    const auto ny_rot = static_cast<float>(rot_lerp(share, static_cast<double>(y_rot),
                                                    static_cast<double>(target_y_rot)));
    const auto nx_rot = static_cast<float>(
        static_cast<double>(x_rot) + share * (static_cast<double>(target_x_rot) - x_rot));
    x = nx;
    y = ny;
    z = nz;
    y_rot = std::fmod(ny_rot, 360.0f);
    x_rot = clamp_x_rot(std::fmod(nx_rot, 360.0f));
    --steps;
    previous[0] = x;
    previous[1] = y;
    previous[2] = z;
    previous_x_rot = x_rot;
    previous_y_rot = y_rot;
    has_previous = true;
}

// `LivingEntity.lerpHeadRotationStep`: one step of the head ease.
void Living::head_step() {
    if (lerp_head_steps > 0) {
        y_head_rot = static_cast<float>(rot_lerp(1.0 / lerp_head_steps,
                                                 static_cast<double>(y_head_rot),
                                                 static_cast<double>(lerp_y_head_rot)));
        --lerp_head_steps;
    }
}

// `LivingEntity.updateSwingTime`: advance the swing and its attack fraction.
void Living::update_swing_time() {
    if (swinging) {
        ++swing_time;
        if (swing_time >= swing_duration) {
            swing_time = 0;
            swinging = false;
        }
    } else {
        swing_time = 0;
    }
    attack_anim = static_cast<float>(swing_time) / static_cast<float>(swing_duration);
}

// `LivingEntity.tickHeadTurn`: the body follows, never too far from the head.
void Living::tick_head_turn(float target_body) {
    const float turn = wrap_degrees(target_body - y_body_rot);
    y_body_rot += turn * 0.3f;
    const float off = wrap_degrees(y_rot - y_body_rot);
    if (std::abs(off) > max_head_rotation) {
        y_body_rot = y_body_rot + (off - static_cast<float>(sign(off)) * max_head_rotation);
    }
}

// `BodyRotationControl.clientTick`: a mob's body turning after its head.
void Living::tick_body_rotation_control() {
    constexpr float kMaxHeadYRot = 75.0f;
    const double dx = x - xo;
    const double dz = z - zo;
    if (dx * dx + dz * dz > static_cast<double>(2.5000003E-7f)) {
        y_body_rot = y_rot;
        y_head_rot = rotate_if_necessary(y_head_rot, y_body_rot, kMaxHeadYRot);
        last_stable_y_head_rot_ = y_head_rot;
        no_turn_ticks_ = 0;
    } else if (std::abs(y_head_rot - last_stable_y_head_rot_) > 15.0f) {
        no_turn_ticks_ = 0;
        last_stable_y_head_rot_ = y_head_rot;
        y_body_rot = rotate_if_necessary(y_body_rot, y_head_rot, kMaxHeadYRot);
    } else {
        ++no_turn_ticks_;
        if (no_turn_ticks_ > 10) {
            const float share = std::clamp(static_cast<float>(no_turn_ticks_ - 10) / 10.0f,
                                           0.0f, 1.0f);
            y_body_rot = rotate_if_necessary(y_body_rot, y_head_rot, kMaxHeadYRot * (1.0f - share));
        }
    }
}

void Living::turn_body_and_wrap() {
    while (y_rot - y_rot_o < -180.0f) y_rot_o -= 360.0f;
    while (y_rot - y_rot_o >= 180.0f) y_rot_o += 360.0f;
    while (y_body_rot - y_body_rot_o < -180.0f) y_body_rot_o -= 360.0f;
    while (y_body_rot - y_body_rot_o >= 180.0f) y_body_rot_o += 360.0f;
    while (x_rot - x_rot_o < -180.0f) x_rot_o -= 360.0f;
    while (x_rot - x_rot_o >= 180.0f) x_rot_o += 360.0f;
    while (y_head_rot - y_head_rot_o < -180.0f) y_head_rot_o -= 360.0f;
    while (y_head_rot - y_head_rot_o >= 180.0f) y_head_rot_o += 360.0f;
}

// `LivingEntity.calculateEntityAnimation`, `LivingEntity.updateWalkAnimation`: walking.
void Living::walk_animation(bool vertical) {
    const double dx = x - xo;
    const double dy = vertical ? y - yo : 0.0;
    const double dz = z - zo;
    const auto distance = static_cast<float>(std::sqrt(dx * dx + dy * dy + dz * dz));
    if (!passenger && !dead) {
        walk.update(std::min(distance * 4.0f, 1.0f), 0.4f, baby ? 3.0f : 1.0f);
    } else {
        walk.stop();
    }
}

void Living::tick_remote_player() {
    set_old();
    ++tick_count;
    base_tick();
    interpolate();
    head_step();
    update_swing_time();

    // `LivingEntity.tick`: the body heading from movement and swing.
    const double dx = x - xo;
    const double dz = z - zo;
    const auto moved = static_cast<float>(dx * dx + dz * dz);
    float target_body = y_body_rot;
    if (moved > 0.0025000002f) {
        const float heading =
            static_cast<float>(std::atan2(dz, dx)) * (180.0f / kPi) - 90.0f;
        const float off = std::abs(wrap_degrees(y_rot) - heading);
        target_body = (95.0f < off && off < 265.0f) ? heading - 180.0f : heading;
    }
    if (attack_anim > 0.0f) target_body = y_rot;
    tick_head_turn(target_body);
    turn_body_and_wrap();
    walk_animation(false);
}

void Living::tick_mob() {
    set_old();
    ++tick_count;
    base_tick();
    interpolate();
    head_step();
    update_swing_time();
    tick_body_rotation_control();
    turn_body_and_wrap();
    walk_animation(false);
}

double Living::x_at(float partial) const { return xo + static_cast<double>(partial) * (x - xo); }
double Living::y_at(float partial) const { return yo + static_cast<double>(partial) * (y - yo); }
double Living::z_at(float partial) const { return zo + static_cast<double>(partial) * (z - zo); }
float Living::body_at(float partial) const { return rot_lerp(partial, y_body_rot_o, y_body_rot); }
float Living::head_at(float partial) const { return rot_lerp(partial, y_head_rot_o, y_head_rot); }
// `Entity.getXRot`: pitch at a partial tick.
float Living::x_rot_at(float partial) const {
    return partial == 1.0f ? x_rot : lerp(partial, x_rot_o, x_rot);
}

// `LivingEntity.getAttackAnim`: the swing fraction at a partial tick.
float Living::attack_at(float partial) const {
    float difference = attack_anim - o_attack_anim;
    if (difference < 0.0f) ++difference;
    return o_attack_anim + difference * partial;
}

} // namespace mcpp::entity
