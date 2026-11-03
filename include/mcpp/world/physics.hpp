#pragma once
#include "mcpp/world/block_table.hpp"
#include "mcpp/world/shapes.hpp"

#include <cstdint>
#include <functional>

namespace mcpp::world {

// A player is a box, not a point: 0.6 wide and 1.8 tall standing, 1.5
// crouching, 0.6 swimming. The eye is where the camera goes.
constexpr double kPlayerWidth = 0.6;
constexpr double kPlayerHeight = 1.8;
constexpr double kEyeHeight = 1.62;
constexpr double kCrouchHeight = 1.5;
constexpr double kCrouchEyeHeight = 1.27;
constexpr double kSwimHeight = 0.6;
constexpr double kSwimEyeHeight = 0.4;

// The game moves the player twenty times a second and draws the camera
// somewhere between two of those. Running the movement on the frame instead
// makes the same key feel different at sixty frames and at three hundred.
constexpr double kTickSeconds = 0.05;

enum class Pose : uint8_t { Standing, Crouching, Swimming };

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
    // How far the feet have travelled and how hard the view is bobbing.
    // Both are interpolated the same way the position is.
    double walked = 0.0;
    double previous_walked = 0.0;
    double sway = 0.0;
    double previous_sway = 0.0;

    Pose pose = Pose::Standing;
    bool swimming = false;
    bool horizontal_collision = false;
    bool minor_horizontal_collision = false;
    bool vertical_collision = false;
    bool in_water = false;
    bool in_lava = false;
    bool eyes_in_water = false;
    double water_height = 0.0;
    double lava_height = 0.0;
    double fall_distance = 0.0;
    int jump_delay = 0;
    // A cobweb or a berry bush slows the next move, not this one.
    double stuck[3] = {0.0, 0.0, 0.0};
    // The camera's height eases between poses instead of jumping.
    double eye = kEyeHeight;
    double previous_eye = kEyeHeight;

    double height() const;
    double eye_height() const;
};

// What the player wants to do this step, in their own frame: forward and
// sideways are -1 to 1, not a speed, and right is positive.
struct Intent {
    double forward = 0.0;
    double strafe = 0.0;
    float yaw_degrees = 0.0f;
    float pitch_degrees = 0.0f;   // the game's: positive looks down
    bool jump = false;
    // The sprint key or a double tap. Sprinting itself is a state of the body
    // that starts on this and ends on its own conditions.
    bool sprint = false;
    bool sneak = false;
    bool using_item = false;
    // Flying, as the server allows it: no gravity, a push of the server's own
    // flying speed, and up and down on keys of their own.
    bool flying = false;
    double rise = 0.0;   // -1 to 1
    double flying_speed = 0.05;

    // The player's attributes as the server sent them. movement_speed is
    // without the sprinting modifier: the body adds that itself.
    double movement_speed = 0.1;
    double jump_strength = 0.42;
    double step_height = 0.6;
    double gravity = 0.08;
    double sneaking_speed = 0.3;
    double water_efficiency = 0.0;
    // Levels, 0 when the effect is not on.
    int jump_boost = 0;
    int levitation = 0;
    bool slow_falling = false;
    bool dolphins_grace = false;
    bool may_sprint = true;   // enough food, or allowed to fly
};

// Block state at a world position, or -1 where the world is not loaded. The
// body never falls through unloaded ground: unknown counts as solid.
using BlockAt = std::function<int32_t(int32_t, int32_t, int32_t)>;

// One tick of the player's movement, the game's own sequence: pose, sprint,
// jump, the push of the keys, the move against every block's collision
// boxes, and the drag of whatever it is moving through.
void tick(Body& body, const Intent& intent, const BlockAt& at, const BlockShapes& shapes);

// The same, in a world where every block is a full cube or nothing, as the
// block table says. For callers that have no state names.
void tick(Body& body, const Intent& intent, const BlockAt& at, const BlockTable& table);

// Whether a box of the body's width and this height fits at the body's feet.
bool fits(const Body& body, double height, const BlockAt& at, const BlockShapes& shapes);

} // namespace mcpp::world
