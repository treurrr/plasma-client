#include "mcpp/world/physics.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace mcpp::world {
namespace {

// The game's own numbers, converted from per-tick to per-second at 20 ticks.
constexpr double kGravity = 32.0;
constexpr double kJumpSpeed = 8.4;
constexpr double kWalkSpeed = 4.317;
constexpr double kSprintSpeed = 5.612;
constexpr double kTerminalFall = 78.4;
constexpr double kAirControl = 0.25;

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

void step(Body& body, const Intent& intent, const BlockAt& at, const BlockTable& table,
          double seconds) {
    if (seconds <= 0.0) return;
    seconds = std::min(seconds, 0.1);   // a long frame must not teleport anyone

    const double yaw = intent.yaw_degrees * std::numbers::pi / 180.0;
    const double sin_yaw = std::sin(yaw);
    const double cos_yaw = std::cos(yaw);
    double wish_x = intent.strafe * cos_yaw + intent.forward * sin_yaw;
    double wish_z = intent.forward * -cos_yaw + intent.strafe * sin_yaw;
    const double length = std::hypot(wish_x, wish_z);
    if (length > 1.0) {
        wish_x /= length;
        wish_z /= length;
    }

    const double speed = intent.sprint ? kSprintSpeed : kWalkSpeed;
    if (body.on_ground) {
        body.vx = wish_x * speed;
        body.vz = wish_z * speed;
        if (intent.jump) {
            body.vy = kJumpSpeed;
            body.on_ground = false;
        }
    } else {
        // In the air you steer, you do not walk.
        body.vx += wish_x * speed * kAirControl * seconds * 20.0 / 4.0;
        body.vz += wish_z * speed * kAirControl * seconds * 20.0 / 4.0;
        const double air = std::hypot(body.vx, body.vz);
        if (air > speed) {
            body.vx *= speed / air;
            body.vz *= speed / air;
        }
    }

    body.vy = std::max(body.vy - kGravity * seconds, -kTerminalFall);

    const auto slide = [&](int axis, double& position, double& velocity) {
        const Box box = around(body);
        const double wanted = velocity * seconds;
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
    slide(0, body.x, body.vx);
    slide(2, body.z, body.vz);
}

} // namespace mcpp::world
