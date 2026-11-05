#pragma once
#include "mcpp/world/shapes.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace mcpp::world {

// The game moves the player twenty times a second and draws the camera
// somewhere between two of those ticks.
constexpr double kTickSeconds = 0.05;

// Standing, crouching and swimming boxes. The game keeps these as floats, so
// half a player is 0.3f, not 0.3, and the box edges are where 0.3f lands.
constexpr float kPlayerWidth = 0.6f;
constexpr float kStandingHeight = 1.8f;
constexpr float kStandingEye = 1.62f;
constexpr float kCrouchingHeight = 1.5f;
constexpr float kCrouchingEye = 1.27f;
constexpr float kSwimmingHeight = 0.6f;
constexpr float kSwimmingEye = 0.4f;

enum class Pose : uint8_t { Standing, Crouching, Swimming };

// The keys held this tick, as the game records them.
struct Keys {
    bool forward = false;
    bool backward = false;
    bool left = false;
    bool right = false;
    bool jump = false;
    bool shift = false;
    bool sprint = false;

    // The bits the game sends in player_input.
    uint8_t bits() const {
        return static_cast<uint8_t>((forward ? 1 : 0) | (backward ? 2 : 0) | (left ? 4 : 0) |
                                    (right ? 8 : 0) | (jump ? 16 : 0) | (shift ? 32 : 0) |
                                    (sprint ? 64 : 0));
    }
};

// What the server says about the player, read fresh each tick.
struct Stats {
    // movement_speed without the sprinting modifier; the body adds that
    // itself the moment it starts to sprint, as the game does.
    double movement_speed = 0.1;
    double jump_strength = 0.42;
    float step_height = 0.6f;
    double gravity = 0.08;
    double sneaking_speed = 0.3;
    double water_movement_efficiency = 0.0;
    double movement_efficiency = 0.0;
    int jump_boost = 0;   // effect levels; 0 when the effect is not on
    int levitation = 0;
    bool slow_falling = false;
    bool dolphins_grace = false;
    bool blind = false;
    bool weaving = false;
    bool may_fly = false;
    bool spectator = false;
    float flying_speed = 0.05f;
    int food = 20;
    bool using_item = false;
    float item_use_speed = 0.2f;
    bool leather_boots = false;
    bool fast_lava = false;   // the nether's lava pushes harder
    int sprint_window = 7;    // ticks between two forward taps that start a sprint
};

// Per block, from the server's tags: which of them climb, which are fences.
struct BlockTags {
    enum : uint8_t { kClimbable = 1, kFences = 2, kWalls = 4, kFenceGate = 8 };
    std::vector<uint8_t> bits;   // indexed by BlockShapes' block index
    bool has(const BlockInfo& block, const BlockShapes& shapes, int32_t state, uint8_t bit) const;
};

// Block state at a world position; -1 where the world is not loaded, which
// the game treats as air you cannot stand on and slowly falls through.
using BlockAt = std::function<int32_t(int32_t, int32_t, int32_t)>;

struct World {
    const BlockAt& at;
    const BlockShapes& shapes;
    const BlockTags* tags = nullptr;
    int32_t min_y = -64;
};

// The local player as the game simulates it: its LocalPlayer, LivingEntity
// and Entity state, the parts movement reads and writes.
struct Body {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double xo = 0.0;   // before the last tick
    double yo = 0.0;
    double zo = 0.0;
    double vx = 0.0;
    double vy = 0.0;
    double vz = 0.0;
    float yaw = 0.0f;     // the game's: 0 faces south, 90 west
    float pitch = 0.0f;   // positive looks down

    bool on_ground = false;
    bool horizontal_collision = false;
    bool minor_horizontal_collision = false;
    bool vertical_collision = false;
    bool vertical_collision_below = false;
    double fall_distance = 0.0;

    Pose pose = Pose::Standing;
    bool crouching = false;
    bool swimming = false;
    bool sprinting = false;
    bool flying = false;

    bool touching_water = false;
    bool eye_in_water = false;
    bool underwater = false;
    double water_height = 0.0;
    double lava_height = 0.0;
    bool first_tick = true;
    bool in_powder_snow = false;
    bool was_in_powder_snow = false;
    double stuck[3] = {0.0, 0.0, 0.0};

    int no_jump_delay = 0;
    int jump_trigger_time = 0;
    int sprint_trigger_time = 0;

    bool has_support = false;
    int32_t support[3] = {0, 0, 0};
    bool on_ground_no_blocks = false;

    // The previous tick's keys and the move vector they made.
    Keys keys;
    float move_x = 0.0f;   // left is positive
    float move_z = 0.0f;   // forward is positive
    float xxa = 0.0f;      // the same after crouching and items slowed it
    float zza = 0.0f;
    bool jumping = false;

    // The view: how far the feet have walked and how hard the camera bobs.
    float walk_dist = 0.0f;
    float walk_dist_o = 0.0f;
    float bob = 0.0f;
    float bob_o = 0.0f;

    // Set when flying changed by the player's own hand this tick, which the
    // game tells the server about.
    bool flying_changed = false;

    float height() const;
    float eye_height() const;
    float eye_height(Pose p) const;
    // `Entity.isInLava`: in lava, never on the first tick.
    bool in_lava() const { return !first_tick && lava_height > 0.0; }
    // The speed the body moves at now, with sprinting in it.
    static double speed(const Stats& stats, bool sprinting);
};

// Moves the body to where the server puts it.
void place(Body& body, double x, double y, double z);

// One tick of the local player: the game's LocalPlayer tick from the keys to
// the pose, in its own order and with its own float and double types.
void tick(Body& body, const Keys& keys, const Stats& stats, const World& world);

// How far a box moving along one axis (0 x, 1 y, 2 z) gets before one
// block's collision shape at (x, y, z) stops it: the game's own cell walk.
double collide_with_block(const BlockShapes& shapes, int32_t state, int32_t x, int32_t y,
                          int32_t z, int axis, const Aabb& box, double distance);

// The push the keys give: input scaled to a speed and turned by the yaw, the
// game's getInputVector with its sine table.
void input_vector(double ix, double iy, double iz, float speed, float yaw, double out[3]);

// Whether the body fits in a pose where it stands.
bool fits(const Body& body, Pose pose, const World& world);

// Boxes moved through the blocks by nobody in particular: particles. One
// mover per tick, so the blocks it reads are the tick's.
class BoxMover {
public:
    explicit BoxMover(const World& world);
    ~BoxMover();
    BoxMover(const BoxMover&) = delete;
    BoxMover& operator=(const BoxMover&) = delete;
    // How far of the move the box gets, written back into the move.
    void collide(const Aabb& box, double move[3]);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mcpp::world
