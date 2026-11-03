#pragma once
#include "mcpp/world/block_table.hpp"

#include <cstdint>
#include <functional>

namespace mcpp::world {

// A player is a box, not a point: 0.6 wide and 1.8 tall, standing on its own
// feet. The eye is 1.62 above them, which is where the camera goes.
constexpr double kPlayerWidth = 0.6;
constexpr double kPlayerHeight = 1.8;
constexpr double kEyeHeight = 1.62;

// The game moves the player twenty times a second and draws the camera
// somewhere between two of those. Running the movement on the frame instead
// makes the same key feel different at sixty frames and at three hundred.
constexpr double kTickSeconds = 0.05;

struct Body {
    double x = 0.0;
    double y = 0.0;   // the feet
    double z = 0.0;
    double vx = 0.0;
    double vy = 0.0;
    double vz = 0.0;
    bool on_ground = false;
    bool sprinting = false;
    // Where the body stood before the last tick, so the camera can be drawn
    // between the two rather than snapping twenty times a second.
    double previous_x = 0.0;
    double previous_y = 0.0;
    double previous_z = 0.0;
};

// What the player wants to do this step, in their own frame: forward and
// sideways are -1 to 1, not a speed.
struct Intent {
    double forward = 0.0;
    double strafe = 0.0;
    float yaw_degrees = 0.0f;
    bool jump = false;
    bool sprint = false;
};

// Block state at a world position, or -1 where the world is not loaded. The
// body never falls through unloaded ground: unknown counts as solid.
//
// Only full cubes stop the body. Mojang's reports do not publish collision
// shapes, so a slab or a staircase is walked through rather than guessed at;
// guessing would put invisible walls in the world, which is worse.
using BlockAt = std::function<int32_t(int32_t, int32_t, int32_t)>;

// One tick of movement, resolved axis by axis so a wall stops sideways travel
// without stopping the fall.
void tick(Body& body, const Intent& intent, const BlockAt& at, const BlockTable& table);

} // namespace mcpp::world
