#include "mcpp/world/physics.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace mcpp::world {
namespace {

// Everything here is per tick, which is the unit the game itself moves in.
// Speed is not set, it is accelerated towards and dragged back from, which is
// what gives walking its short run-up and its slide when the key comes up.
constexpr double kGroundDrag = 0.546;   // 0.6 slipperiness through 0.91 of air
constexpr double kAirDrag = 0.91;
constexpr double kFallDrag = 0.98;
constexpr double kGravity = 0.08;
constexpr double kTerminalFall = 3.92;  // where gravity and fall drag balance
constexpr double kJumpSpeed = 0.42;
// A sprinting jump throws you forward as well as up, which is most of why
// jumping while sprinting covers ground faster than sprinting does.
constexpr double kSprintJumpBoost = 0.2;

// Chosen so that acceleration against drag settles on the speed the game
// walks and sprints at: 4.317 and 5.612 blocks a second.
constexpr double kWalkAccel = (4.317 / 20.0) * (1.0 - kGroundDrag);
constexpr double kSprintAccel = (5.612 / 20.0) * (1.0 - kGroundDrag);
// In the air you steer, you do not walk.
constexpr double kAirAccel = 0.02;
constexpr double kSprintAirAccel = 0.026;

// Flying up or down coasts to a stop much sooner than across.
constexpr double kFlyingRiseDrag = 0.6;

// The sway stops growing well below walking pace, so sprinting rocks the view
// no harder than walking does.
constexpr double kMaxSway = 0.1;

// How high a step the game lets you walk up without jumping.
constexpr double kStepHeight = 0.6;

struct Box {
    double min[3];
    double max[3];
};

Box around(const Body& body) {
    const double half = kPlayerWidth * 0.5;
    return {{body.x - half, body.y, body.z - half},
            {body.x + half, body.y + kPlayerHeight, body.z + half}};
}

bool solid(const BlockAt& at, const BlockTable& table, int32_t x, int32_t y, int32_t z) {
    const int32_t state = at(x, y, z);
    // Unknown is solid: better to stand on a chunk that has not arrived than
    // to fall through the world while it loads.
    if (state < 0) return true;
    if (state == 0) return false;
    if (table.fluid(state)) return false;
    return table.full_cube(state);
}

// Moves the box along one axis and stops it at the first block it would enter.
double limit(const Box& box, const BlockAt& at, const BlockTable& table, int axis,
             double distance) {
    if (distance == 0.0) return 0.0;

    const int other_a = (axis + 1) % 3;
    const int other_b = (axis + 2) % 3;
    const auto floor_i = [](double value) {
        return static_cast<int32_t>(std::floor(value));
    };

    const double target = distance > 0.0 ? box.max[axis] + distance : box.min[axis] + distance;
    const int32_t from = floor_i(distance > 0.0 ? box.max[axis] : box.min[axis]);
    const int32_t to = floor_i(distance > 0.0 ? target - 1e-7 : target + 1e-7);
    const int32_t stride = distance > 0.0 ? 1 : -1;

    for (int32_t step_axis = from; step_axis != to + stride; step_axis += stride) {
        for (int32_t a = floor_i(box.min[other_a] + 1e-7);
             a <= floor_i(box.max[other_a] - 1e-7); ++a) {
            for (int32_t b = floor_i(box.min[other_b] + 1e-7);
                 b <= floor_i(box.max[other_b] - 1e-7); ++b) {
                int32_t position[3];
                position[axis] = step_axis;
                position[other_a] = a;
                position[other_b] = b;
                if (!solid(at, table, position[0], position[1], position[2])) continue;

                const double face = distance > 0.0 ? static_cast<double>(step_axis)
                                                   : static_cast<double>(step_axis) + 1.0;
                const double allowed = distance > 0.0 ? face - box.max[axis] - 1e-4
                                                      : face - box.min[axis] + 1e-4;
                return distance > 0.0 ? std::min(distance, std::max(0.0, allowed))
                                      : std::max(distance, std::min(0.0, allowed));
            }
        }
    }
    return distance;
}

} // namespace

void tick(Body& body, const Intent& intent, const BlockAt& at, const BlockTable& table) {
    body.previous_x = body.x;
    body.previous_y = body.y;
    body.previous_z = body.z;

    const double yaw = intent.yaw_degrees * std::numbers::pi / 180.0;
    const double sin_yaw = std::sin(yaw);
    const double cos_yaw = std::cos(yaw);
    // The game's yaw: 0 looks south (+z), 90 west (-x).
    double wish_x = -intent.forward * sin_yaw - intent.strafe * cos_yaw;
    double wish_z = intent.forward * cos_yaw - intent.strafe * sin_yaw;
    const double length = std::hypot(wish_x, wish_z);
    if (length > 1.0) {
        wish_x /= length;
        wish_z /= length;
    }

    // Sprinting is a state, not a key: it needs you going forwards, and it
    // drops the moment you stop asking to.
    body.sprinting = intent.sprint && intent.forward > 0.0;

    if (intent.flying) {
        // Twice the push sprinting, and three times it going up or down, which
        // is how creative flight moves; the drag below is the air's.
        const double push = intent.flying_speed * (body.sprinting ? 2.0 : 1.0);
        body.vx += wish_x * push;
        body.vz += wish_z * push;
        body.vy += intent.rise * intent.flying_speed * 3.0;
    } else if (body.on_ground) {
        const double accel = body.sprinting ? kSprintAccel : kWalkAccel;
        body.vx += wish_x * accel;
        body.vz += wish_z * accel;
        if (intent.jump) {
            body.vy = kJumpSpeed;
            body.on_ground = false;
            if (body.sprinting) {
                body.vx += -sin_yaw * kSprintJumpBoost;
                body.vz += cos_yaw * kSprintJumpBoost;
            }
        }
    } else {
        const double accel = body.sprinting ? kSprintAirAccel : kAirAccel;
        body.vx += wish_x * accel;
        body.vz += wish_z * accel;
    }

    if (!intent.flying) body.vy = std::max((body.vy - kGravity) * kFallDrag, -kTerminalFall);

    const auto slide = [&](int axis, double& position, double& velocity) {
        const Box box = around(body);
        const double wanted = velocity;
        const double moved = limit(box, at, table, axis, wanted);
        position += moved;
        const bool blocked = std::abs(moved - wanted) > 1e-9;
        if (blocked) velocity = 0.0;
        return blocked;
    };

    // Vertically first: standing on the floor is what decides whether the next
    // sideways move is a walk or a drift through the air.
    const bool falling = body.vy <= 0.0;
    body.on_ground = slide(1, body.y, body.vy) && falling;

    // A kerb is not a wall. Blocked on the ground, try the same move one step
    // higher and drop back down, which is what lets you walk up a hillside.
    const auto walk = [&](int axis, double& position, double& velocity) {
        const double was_position = position;
        const double was_velocity = velocity;
        if (!slide(axis, position, velocity) || !body.on_ground) return;

        const double blocked_at = position;
        position = was_position;
        velocity = was_velocity;

        const double raised = body.y;
        double climb = kStepHeight;
        body.y += limit(around(body), at, table, 1, climb);
        if (body.y - raised < kStepHeight - 1e-6) {
            body.y = raised;
            position = blocked_at;
            velocity = 0.0;
            return;
        }
        slide(axis, position, velocity);
        climb = -(body.y - raised);
        body.y += limit(around(body), at, table, 1, climb);
        if (std::abs(position - was_position) <= std::abs(blocked_at - was_position)) {
            body.y = raised;
            position = blocked_at;
            velocity = 0.0;
        }
    };

    walk(0, body.x, body.vx);
    walk(2, body.z, body.vz);

    // Drag is applied after the move, so a tick that was stopped by a wall
    // still bleeds its speed off the same way an open one does.
    const double drag = body.on_ground && !intent.flying ? kGroundDrag : kAirDrag;
    body.vx *= drag;
    body.vz *= drag;
    if (intent.flying) body.vy *= kFlyingRiseDrag;

    // Both of these follow the distance actually covered, not the distance
    // asked for, so walking into a wall stops the sway instead of marching on
    // the spot.
    const double covered = std::hypot(body.x - body.previous_x, body.z - body.previous_z);
    body.previous_walked = body.walked;
    body.walked += covered * 0.6;
    body.previous_sway = body.sway;
    body.sway += ((body.on_ground ? std::min(covered, kMaxSway) : 0.0) - body.sway) * 0.4;
}

} // namespace mcpp::world
