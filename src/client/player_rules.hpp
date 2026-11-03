#pragma once
// The local player's rules that the window and the headless tests share: what
// the server's attributes and effects mean for movement, how a teleport lands
// on the body, what one tick sends, where the crosshair is, how the mouse
// turns, and how fast a block breaks.
#include "mcpp/session.hpp"
#include "mcpp/world/physics.hpp"
#include "mcpp/world/shapes.hpp"

#include <cstdint>
#include <string>

namespace client {

// Everything the movement reads from the server, fresh for this tick.
mcpp::world::Stats player_stats(const mcpp::Session& session);

// Block tags the movement needs, by block, from the server's tag lists.
void player_tags(const mcpp::Session& session, const mcpp::world::BlockShapes& shapes,
                 mcpp::world::BlockTags& out);

// Lands a teleport on the body the way the game does and returns where the
// body now is, which is what the answer to the server says.
void apply_teleport(mcpp::world::Body& body, const mcpp::SessionEvents::Player::Teleport& t);

// The packets of one tick, after the body moved: flight if the player
// toggled it, the keys, sprinting, the movement and the end of the tick.
void send_tick(mcpp::Session& session, const mcpp::world::Body& body);

// The mouse, in pixels, turned into degrees the way the game does it at this
// sensitivity (0.5 is the game's default, 0.15 degrees a pixel).
double mouse_scale(double sensitivity);
void turn(mcpp::world::Body& body, double dx_degrees, double dy_degrees);

// The block the crosshair is on: which, the face the ray entered and the
// exact point on it, or nothing within reach.
struct Hit {
    bool hit = false;
    int32_t pos[3] = {0, 0, 0};
    int32_t face = 1;   // down, up, north, south, west, east
    double location[3] = {0.0, 0.0, 0.0};
    bool inside = false;
};
Hit pick_block(const mcpp::world::BlockShapes& shapes, const mcpp::world::BlockAt& at,
               const double from[3], const double to[3]);
// The game's eye and look direction for a body at a fraction of a tick.
void eye_position(const mcpp::world::Body& body, double partial, double out[3]);
void view_vector(float pitch, float yaw, double out[3]);

// How far one tick of digging gets through a block, from 0 to 1.
struct DigContext {
    float tool_speed = 1.0f;     // the held item's speed on this block
    bool correct_tool = false;   // whether it gets a drop
    double mining_efficiency = 0.0;
    double block_break_speed = 1.0;
    double submerged_mining_speed = 0.2;
    int haste = 0;   // effect levels
    int conduit_power = 0;
    int mining_fatigue = 0;
    bool eye_in_water = false;
    bool on_ground = true;
};
float destroy_progress(float hardness, bool needs_tool, const DigContext& dig);

// Joins as PlasmaE, builds a movement course far from spawn and runs walking,
// sprinting, jumping, sneaking, ice, steps, ladders, swimming and the slowing
// blocks through the real tick and packets, counting the server's corrections.
int movetest(const std::string& target);

} // namespace client
