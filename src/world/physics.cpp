#include "mcpp/world/physics.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <numbers>
#include <vector>

namespace mcpp::world {
namespace {

constexpr double kAirDrag = 0.91;
constexpr double kVerticalDrag = 0.98;
constexpr double kSprintBoost = 0.3;          // the sprinting modifier, multiply_total
constexpr double kSprintJumpBoost = 0.2;
constexpr double kAirAccel = 0.02;
constexpr double kSprintAirAccel = 0.025999999;
constexpr double kInputScale = 0.98;
constexpr double kClimbSpeed = 0.15;
constexpr double kFluidJumpThreshold = 0.4;
constexpr double kEpsilon = 1e-7;
constexpr double kMaxSway = 0.1;

struct Aabb {
    double min[3];
    double max[3];

    Aabb moved(double dx, double dy, double dz) const {
        return {{min[0] + dx, min[1] + dy, min[2] + dz}, {max[0] + dx, max[1] + dy, max[2] + dz}};
    }
    Aabb grown(double dx, double dy, double dz) const {
        Aabb out = *this;
        const double d[3] = {dx, dy, dz};
        for (int i = 0; i < 3; ++i) {
            if (d[i] < 0.0) out.min[i] += d[i];
            else out.max[i] += d[i];
        }
        return out;
    }
    Aabb shrunk(double by) const {
        return {{min[0] + by, min[1] + by, min[2] + by}, {max[0] - by, max[1] - by, max[2] - by}};
    }
    bool overlaps(const Aabb& o) const {
        for (int i = 0; i < 3; ++i) {
            if (max[i] <= o.min[i] || min[i] >= o.max[i]) return false;
        }
        return true;
    }
};

int32_t floor_i(double v) { return static_cast<int32_t>(std::floor(v)); }

double body_height(Pose pose) {
    switch (pose) {
        case Pose::Crouching: return kCrouchHeight;
        case Pose::Swimming: return kSwimHeight;
        default: return kPlayerHeight;
    }
}

double body_eye(Pose pose) {
    switch (pose) {
        case Pose::Crouching: return kCrouchEyeHeight;
        case Pose::Swimming: return kSwimEyeHeight;
        default: return kEyeHeight;
    }
}

Aabb box_of(double x, double y, double z, double height) {
    const double half = kPlayerWidth * 0.5;
    return {{x - half, y, z - half}, {x + half, y + height, z + half}};
}

struct World {
    const BlockAt& at;
    const BlockShapes& shapes;
    const Body& body;
    bool sneaking;

    int32_t state(int32_t x, int32_t y, int32_t z) const { return at(x, y, z); }

    // Every collision box that could touch the region, in world coordinates.
    // Unloaded ground is solid, so nobody falls out of a chunk on its way.
    void collect(const Aabb& region, std::vector<Aabb>& out) const {
        out.clear();
        const int32_t x0 = floor_i(region.min[0] - kEpsilon) ;
        const int32_t x1 = floor_i(region.max[0] + kEpsilon);
        const int32_t y0 = floor_i(region.min[1] - kEpsilon) - 1;   // fences and walls reach up
        const int32_t y1 = floor_i(region.max[1] + kEpsilon);
        const int32_t z0 = floor_i(region.min[2] - kEpsilon);
        const int32_t z1 = floor_i(region.max[2] + kEpsilon);
        for (int32_t x = x0; x <= x1; ++x) {
            for (int32_t z = z0; z <= z1; ++z) {
                for (int32_t y = y0; y <= y1; ++y) {
                    const int32_t s = state(x, y, z);
                    if (s < 0) {
                        out.push_back({{double(x), double(y), double(z)},
                                       {x + 1.0, y + 1.0, z + 1.0}});
                        continue;
                    }
                    if (s == 0) continue;
                    const auto& physics = shapes.physics(s);
                    if (physics.scaffolding) {
                        // Standing on it unless you are sneaking down through it.
                        if (sneaking || body.y < y + 1.0 - 1e-5) continue;
                        out.push_back({{double(x), y + 14.0 / 16.0, double(z)},
                                       {x + 1.0, y + 1.0, z + 1.0}});
                        continue;
                    }
                    double ox = 0.0;
                    double oz = 0.0;
                    if (physics.offset > 0.0f) random_offset(x, z, physics.offset, ox, oz);
                    for (const auto& b : shapes.collision(s)) {
                        Aabb w{{x + b.min[0] + ox, y + b.min[1], z + b.min[2] + oz},
                               {x + b.max[0] + ox, y + b.max[1], z + b.max[2] + oz}};
                        if (w.max[1] <= region.min[1] - kEpsilon && y < floor_i(region.min[1])) {
                            continue;
                        }
                        out.push_back(w);
                    }
                }
            }
        }
    }

    bool free(const Aabb& box) const {
        std::vector<Aabb> boxes;
        collect(box, boxes);
        const Aabb inner = box;
        return std::ranges::none_of(boxes, [&](const Aabb& b) { return inner.overlaps(b); });
    }

    bool has_fluid(const Aabb& box) const {
        for (int32_t x = floor_i(box.min[0]); x <= floor_i(box.max[0] - kEpsilon); ++x) {
            for (int32_t y = floor_i(box.min[1]); y <= floor_i(box.max[1] - kEpsilon); ++y) {
                for (int32_t z = floor_i(box.min[2]); z <= floor_i(box.max[2] - kEpsilon); ++z) {
                    const int32_t s = state(x, y, z);
                    if (s > 0 && shapes.physics(s).fluid != FluidKind::None) return true;
                }
            }
        }
        return false;
    }

    // How high the fluid in a cell stands: to the top when more of the same
    // is above it, otherwise by how much of a block it holds.
    double fluid_top(int32_t x, int32_t y, int32_t z, FluidKind kind) const {
        const int32_t s = state(x, y, z);
        if (s <= 0) return -1.0;
        const auto& physics = shapes.physics(s);
        if (physics.fluid != kind) return -1.0;
        const int32_t above = state(x, y + 1, z);
        if (above > 0 && shapes.physics(above).fluid == kind) return y + 1.0;
        return y + physics.fluid_amount / 9.0;
    }
};

// How far the box may move along one axis before it meets one of the boxes.
double clip(const std::vector<Aabb>& boxes, const Aabb& box, int axis, double distance) {
    if (std::abs(distance) < kEpsilon) return 0.0;
    const int a = (axis + 1) % 3;
    const int b = (axis + 2) % 3;
    for (const auto& o : boxes) {
        if (o.max[a] - kEpsilon <= box.min[a] || o.min[a] + kEpsilon >= box.max[a]) continue;
        if (o.max[b] - kEpsilon <= box.min[b] || o.min[b] + kEpsilon >= box.max[b]) continue;
        if (distance > 0.0 && o.min[axis] >= box.max[axis] - kEpsilon) {
            distance = std::min(distance, o.min[axis] - box.max[axis]);
        } else if (distance < 0.0 && o.max[axis] <= box.min[axis] + kEpsilon) {
            distance = std::max(distance, o.max[axis] - box.min[axis]);
        }
    }
    return std::abs(distance) < kEpsilon ? 0.0 : distance;
}

struct Move {
    double d[3];
};

// Y first, then whichever of x and z is the larger move: the game's order,
// and the reason a diagonal run along a wall slides instead of sticking.
Move collide_with(const std::vector<Aabb>& boxes, Aabb box, Move m) {
    Move out{{0.0, 0.0, 0.0}};
    const int order[3] = {1, std::abs(m.d[0]) < std::abs(m.d[2]) ? 2 : 0,
                          std::abs(m.d[0]) < std::abs(m.d[2]) ? 0 : 2};
    for (const int axis : order) {
        const double moved = clip(boxes, box, axis, m.d[axis]);
        out.d[axis] = moved;
        box.min[axis] += moved;
        box.max[axis] += moved;
    }
    return out;
}

double horizontal2(const Move& m) { return m.d[0] * m.d[0] + m.d[2] * m.d[2]; }

Move collide(const World& world, const Aabb& box, const Move& m, bool on_ground,
             double step) {
    std::vector<Aabb> boxes;
    world.collect(box.grown(m.d[0], m.d[1], m.d[2]), boxes);
    const Move plain = collide_with(boxes, box, m);

    const bool blocked_x = plain.d[0] != m.d[0];
    const bool blocked_z = plain.d[2] != m.d[2];
    const bool landed = plain.d[1] != m.d[1] && m.d[1] < 0.0;
    if (step <= 0.0 || !(on_ground || landed) || !(blocked_x || blocked_z)) return plain;

    // A kerb is not a wall: try the same move a step higher, and keep it if
    // it gets further.
    std::vector<Aabb> around;
    Aabb region = box.grown(m.d[0], step, m.d[2]);
    region.min[1] = std::min(region.min[1], box.min[1] + m.d[1]);
    world.collect(region, around);
    Move up = collide_with(around, box, {{m.d[0], step, m.d[2]}});
    const Move rise = collide_with(around, box.grown(m.d[0], 0.0, m.d[2]), {{0.0, step, 0.0}});
    if (rise.d[1] < step) {
        const Move across =
            collide_with(around, box.moved(0.0, rise.d[1], 0.0), {{m.d[0], 0.0, m.d[2]}});
        if (horizontal2(across) > horizontal2(up)) {
            up = {{across.d[0], rise.d[1], across.d[2]}};
        }
    }
    if (horizontal2(up) <= horizontal2(plain)) return plain;
    const double settle =
        clip(around, box.moved(up.d[0], up.d[1], up.d[2]), 1, -up.d[1] + m.d[1]);
    return {{up.d[0], up.d[1] + settle, up.d[2]}};
}

void move_relative(Body& body, double speed, double strafe, double forward, float yaw) {
    const double length2 = strafe * strafe + forward * forward;
    if (length2 < 1e-7) return;
    if (length2 > 1.0) {
        const double length = std::sqrt(length2);
        strafe /= length;
        forward /= length;
    }
    strafe *= speed;
    forward *= speed;
    const double radians = yaw * std::numbers::pi / 180.0;
    const double s = std::sin(radians);
    const double c = std::cos(radians);
    body.vx += strafe * c - forward * s;
    body.vz += forward * c + strafe * s;
}

const BlockPhysics& physics_at(const World& world, int32_t x, int32_t y, int32_t z) {
    return world.shapes.physics(std::max(0, world.state(x, y, z)));
}

// The block that decides friction and speed: the one under the feet, half a
// block down, so a slab's top counts as what it is on.
const BlockPhysics& below(const World& world, const Body& body) {
    return physics_at(world, floor_i(body.x), floor_i(body.y - 0.5000001), floor_i(body.z));
}

double block_factor(const World& world, const Body& body, float BlockPhysics::*factor) {
    const auto& here = physics_at(world, floor_i(body.x), floor_i(body.y), floor_i(body.z));
    if (here.fluid == FluidKind::Water && here.bubbles != 0) return 1.0;
    if (here.*factor != 1.0f) return here.*factor;
    return below(world, body).*factor;
}

bool on_climbable(const World& world, const Body& body) {
    return physics_at(world, floor_i(body.x), floor_i(body.y), floor_i(body.z)).climbable;
}

// Space under a box at the body's feet, offset sideways: whether a step
// that way would drop further than a stair.
bool can_fall(const World& world, const Body& body, double dx, double dz, double depth) {
    const Aabb box = box_of(body.x, body.y, body.z, body.height());
    const Aabb probe{{box.min[0] + kEpsilon + dx, box.min[1] - depth - kEpsilon,
                      box.min[2] + kEpsilon + dz},
                     {box.max[0] - kEpsilon + dx, box.min[1], box.max[2] - kEpsilon + dz}};
    return world.free(probe);
}

// Sneaking at an edge stops you at the edge: the move is cut back a
// twentieth at a time until it would no longer drop you off.
Move back_off_edge(const World& world, const Body& body, const Intent& intent, Move m) {
    if (intent.flying || m.d[1] > 0.0 || !world.sneaking) return m;
    const double step = intent.step_height;
    const bool above_ground =
        body.on_ground ||
        (body.fall_distance < step && !can_fall(world, body, 0.0, 0.0, step - body.fall_distance));
    if (!above_ground) return m;
    double dx = m.d[0];
    double dz = m.d[2];
    constexpr double kBit = 0.05;
    const auto shrink = [](double v) {
        if (std::abs(v) < kBit) return 0.0;
        return v - std::copysign(kBit, v);
    };
    while (dx != 0.0 && can_fall(world, body, dx, 0.0, step)) dx = shrink(dx);
    while (dz != 0.0 && can_fall(world, body, 0.0, dz, step)) dz = shrink(dz);
    while (dx != 0.0 && dz != 0.0 && can_fall(world, body, dx, dz, step)) {
        dx = shrink(dx);
        dz = shrink(dz);
    }
    return {{dx, m.d[1], dz}};
}

bool equal(double a, double b) { return std::abs(a - b) < 1e-5; }

// Moves the body by its velocity against the world and settles everything
// that depends on what it ran into.
void move(Body& body, const World& world, const Intent& intent, double strafe, double forward) {
    Move m{{body.vx, body.vy, body.vz}};
    if (body.stuck[0] != 0.0 || body.stuck[1] != 0.0 || body.stuck[2] != 0.0) {
        for (int i = 0; i < 3; ++i) m.d[i] *= body.stuck[i];
        body.stuck[0] = body.stuck[1] = body.stuck[2] = 0.0;
        body.vx = body.vy = body.vz = 0.0;
    }
    m = back_off_edge(world, body, intent, m);

    const Aabb box = box_of(body.x, body.y, body.z, body.height());
    const Move done = collide(world, box, m, body.on_ground, intent.step_height);
    body.x += done.d[0];
    body.y += done.d[1];
    body.z += done.d[2];

    const bool hit_x = !equal(m.d[0], done.d[0]);
    const bool hit_z = !equal(m.d[2], done.d[2]);
    body.horizontal_collision = hit_x || hit_z;
    body.vertical_collision = m.d[1] != done.d[1];
    body.on_ground = body.vertical_collision && m.d[1] < 0.0;

    // A collision that turns the run by less than eight degrees does not end
    // a sprint: brushing a wall is not running into it.
    body.minor_horizontal_collision = false;
    if (body.horizontal_collision) {
        const double radians = intent.yaw_degrees * std::numbers::pi / 180.0;
        const double wx = strafe * std::cos(radians) - forward * std::sin(radians);
        const double wz = forward * std::cos(radians) + strafe * std::sin(radians);
        const double want = wx * wx + wz * wz;
        const double got = done.d[0] * done.d[0] + done.d[2] * done.d[2];
        if (want >= 1e-5 && got >= 1e-5) {
            const double cosine = (wx * done.d[0] + wz * done.d[2]) / std::sqrt(want * got);
            body.minor_horizontal_collision = std::acos(std::clamp(cosine, -1.0, 1.0)) < 0.13962634;
        }
    }
    if (hit_x) body.vx = 0.0;
    if (hit_z) body.vz = 0.0;

    if (body.on_ground) {
        body.fall_distance = 0.0;
    } else if (done.d[1] < 0.0) {
        body.fall_distance -= done.d[1];
    }

    const auto& floor = below(world, body);
    if (body.vertical_collision) {
        if (floor.slime && !world.sneaking && body.vy < 0.0) {
            body.vy = -body.vy;
        } else if (floor.bed && !world.sneaking && body.vy < 0.0) {
            body.vy = -body.vy * 0.66;
        } else {
            body.vy = 0.0;
        }
    }
    if (body.on_ground && !world.sneaking && floor.slime && std::abs(body.vy) < 0.1) {
        const double damp = 0.4 + std::abs(body.vy) * 0.2;
        body.vx *= damp;
        body.vz *= damp;
    }

    // What the body is now inside of: webs and bushes hold it, honey walls
    // slow its slide, bubble columns carry it.
    const Aabb inside = box_of(body.x, body.y, body.z, body.height()).shrunk(1e-5);
    for (int32_t x = floor_i(inside.min[0]); x <= floor_i(inside.max[0]); ++x) {
        for (int32_t y = floor_i(inside.min[1]); y <= floor_i(inside.max[1]); ++y) {
            for (int32_t z = floor_i(inside.min[2]); z <= floor_i(inside.max[2]); ++z) {
                const int32_t s = world.state(x, y, z);
                if (s <= 0) continue;
                const auto& p = world.shapes.physics(s);
                if (p.stuck[0] != 0.0f) {
                    body.stuck[0] = p.stuck[0];
                    body.stuck[1] = p.stuck[1];
                    body.stuck[2] = p.stuck[2];
                    body.fall_distance = 0.0;
                }
                if (p.bubbles != 0) {
                    const int32_t above = std::max(0, world.state(x, y + 1, z));
                    const bool open = world.shapes.collision(above).empty() &&
                                      world.shapes.physics(above).fluid == FluidKind::None;
                    if (p.bubbles < 0) {
                        body.vy = std::max(open ? -0.9 : -0.3, body.vy - 0.03);
                    } else {
                        body.vy = std::min(open ? 1.8 : 0.7, body.vy + (open ? 0.1 : 0.06));
                    }
                    body.fall_distance = 0.0;
                }
            }
        }
    }
    // Honey on a wall: falling beside it is a slow slide.
    if (!body.on_ground && body.vy < -0.08) {
        const int32_t fy = floor_i(body.y);
        for (const auto& [dx, dz] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
            const int32_t hx = floor_i(body.x) + dx;
            const int32_t hz = floor_i(body.z) + dz;
            const int32_t s = world.state(hx, fy, hz);
            if (s <= 0 || !world.shapes.physics(s).honey) continue;
            if (body.y > fy + 0.9375 - 1e-7) continue;
            const double gap_x = std::abs(hx + 0.5 - body.x);
            const double gap_z = std::abs(hz + 0.5 - body.z);
            if (gap_x + 1e-7 <= 0.4375 + kPlayerWidth / 2.0 &&
                gap_z + 1e-7 <= 0.4375 + kPlayerWidth / 2.0) {
                if (body.vy < -0.13) {
                    const double scale = -0.05 / body.vy;
                    body.vx *= scale;
                    body.vz *= scale;
                }
                body.vy = -0.05;
                body.fall_distance = 0.0;
                break;
            }
        }
    }

    const double speed = block_factor(world, body, &BlockPhysics::speed);
    body.vx *= speed;
    body.vz *= speed;
}

// Water and lava: whether the body is in them and how deep. A fluid counts
// once its surface is above the feet.
void update_fluids(Body& body, const World& world) {
    const Aabb box = box_of(body.x, body.y, body.z, body.height()).shrunk(0.001);
    body.water_height = 0.0;
    body.lava_height = 0.0;
    body.in_water = false;
    body.in_lava = false;
    for (int32_t x = floor_i(box.min[0]); x <= floor_i(box.max[0]); ++x) {
        for (int32_t y = floor_i(box.min[1]); y <= floor_i(box.max[1]); ++y) {
            for (int32_t z = floor_i(box.min[2]); z <= floor_i(box.max[2]); ++z) {
                for (const auto kind : {FluidKind::Water, FluidKind::Lava}) {
                    const double top = world.fluid_top(x, y, z, kind);
                    if (top < box.min[1]) continue;
                    const double depth = top - box.min[1];
                    if (kind == FluidKind::Water) {
                        body.in_water = true;
                        body.water_height = std::max(body.water_height, depth);
                    } else {
                        body.in_lava = true;
                        body.lava_height = std::max(body.lava_height, depth);
                    }
                }
            }
        }
    }
    if (body.in_water) body.fall_distance = 0.0;

    const double eye = body.y + body.eye_height() - 0.11111111;
    const double top = world.fluid_top(floor_i(body.x), floor_i(eye), floor_i(body.z),
                                       FluidKind::Water);
    body.eyes_in_water = top > eye;
}

double fluid_fall(double vy, double gravity, bool sprinting) {
    if (gravity == 0.0 || sprinting) return vy;
    const bool falling = vy <= 0.0;
    if (falling && std::abs(vy - 0.005) >= 0.003 && std::abs(vy - gravity / 16.0) < 0.003) {
        return -0.003;
    }
    return vy - gravity / 16.0;
}

double effective_gravity(const Body& body, const Intent& intent) {
    if (intent.slow_falling && body.vy <= 0.0) return std::min(intent.gravity, 0.01);
    return intent.gravity;
}

void travel(Body& body, const World& world, const Intent& intent, double strafe, double forward,
            bool jumping) {
    const double gravity = effective_gravity(body, intent);
    const double speed = intent.movement_speed * (body.sprinting ? 1.0 + kSprintBoost : 1.0);

    if (body.swimming) {
        const double look = -std::sin(intent.pitch_degrees * std::numbers::pi / 180.0);
        const double pull = look < -0.2 ? 0.085 : 0.06;
        const double head = body.y + 1.0 - 0.1;
        const bool water_above =
            world.fluid_top(floor_i(body.x), floor_i(head), floor_i(body.z), FluidKind::Water) >=
            0.0;
        if (look <= 0.0 || jumping || water_above) body.vy += (look - body.vy) * pull;
    }

    const auto climb_out = [&](double start_y) {
        if (!body.horizontal_collision) return;
        const Aabb box = box_of(body.x, body.y, body.z, body.height())
                             .moved(body.vx, body.vy + 0.6 - body.y + start_y, body.vz);
        if (world.free(box) && !world.has_fluid(box)) body.vy = 0.3;
    };

    if (body.in_water && !intent.flying) {
        const double start_y = body.y;
        double slow = body.sprinting ? 0.9 : 0.8;
        double accel = 0.02;
        double efficiency = intent.water_efficiency;
        if (!body.on_ground) efficiency *= 0.5;
        if (efficiency > 0.0) {
            slow += (0.54600006 - slow) * efficiency;
            accel += (speed - accel) * efficiency;
        }
        if (intent.dolphins_grace) slow = 0.96;
        move_relative(body, accel, strafe, forward, intent.yaw_degrees);
        move(body, world, intent, strafe, forward);
        if (body.horizontal_collision && on_climbable(world, body)) body.vy = 0.2;
        body.vx *= slow;
        body.vy *= 0.8;
        body.vz *= slow;
        body.vy = fluid_fall(body.vy, gravity, body.sprinting);
        climb_out(start_y);
        return;
    }
    if (body.in_lava && !intent.flying) {
        const double start_y = body.y;
        move_relative(body, 0.02, strafe, forward, intent.yaw_degrees);
        move(body, world, intent, strafe, forward);
        if (body.lava_height <= kFluidJumpThreshold) {
            body.vx *= 0.5;
            body.vy *= 0.8;
            body.vz *= 0.5;
            body.vy = fluid_fall(body.vy, gravity, body.sprinting);
        } else {
            body.vx *= 0.5;
            body.vy *= 0.5;
            body.vz *= 0.5;
        }
        if (gravity != 0.0) body.vy -= gravity / 4.0;
        climb_out(start_y);
        return;
    }

    const double slip = body.on_ground ? below(world, body).friction : 1.0;
    const double friction = slip * kAirDrag;
    double accel;
    if (body.on_ground) {
        accel = speed * (0.21600002 / (slip * slip * slip));
    } else if (intent.flying) {
        accel = intent.flying_speed * (body.sprinting ? 2.0 : 1.0);
    } else {
        accel = body.sprinting ? kSprintAirAccel : kAirAccel;
    }
    move_relative(body, accel, strafe, forward, intent.yaw_degrees);

    const bool climbing = !intent.flying && on_climbable(world, body);
    if (climbing) {
        body.fall_distance = 0.0;
        body.vx = std::clamp(body.vx, -kClimbSpeed, kClimbSpeed);
        body.vz = std::clamp(body.vz, -kClimbSpeed, kClimbSpeed);
        body.vy = std::max(body.vy, -kClimbSpeed);
        const bool scaffold =
            physics_at(world, floor_i(body.x), floor_i(body.y), floor_i(body.z)).scaffolding;
        if (body.vy < 0.0 && world.sneaking && !scaffold) body.vy = 0.0;
    }

    move(body, world, intent, strafe, forward);

    if ((body.horizontal_collision || jumping) && climbing) body.vy = 0.2;

    if (intent.levitation > 0) {
        body.vy += (0.05 * intent.levitation - body.vy) * 0.2;
    } else {
        body.vy -= gravity;
    }
    body.vx *= friction;
    body.vy *= kVerticalDrag;
    body.vz *= friction;
}

// Standing, crouching or swimming: the pose wanted, or the next one down that
// fits, so standing up under a low ceiling keeps you crouched.
void update_pose(Body& body, const World& world, const Intent& intent) {
    Pose wanted = Pose::Standing;
    if (body.swimming) wanted = Pose::Swimming;
    else if (intent.sneak && !intent.flying) wanted = Pose::Crouching;
    if (wanted == Pose::Standing && !fits(body, kPlayerHeight, world.at, world.shapes)) {
        wanted = fits(body, kCrouchHeight, world.at, world.shapes) ? Pose::Crouching
                                                                   : Pose::Swimming;
    } else if (wanted == Pose::Crouching && !fits(body, kCrouchHeight, world.at, world.shapes)) {
        wanted = Pose::Swimming;
    }
    body.pose = wanted;
}

void update_sprint(Body& body, const Intent& intent, double forward) {
    const bool slowed = body.pose == Pose::Crouching && !body.swimming;
    const bool forward_impulse = forward > 1e-5;
    const bool enough_impulse = body.eyes_in_water ? forward_impulse : forward >= 0.8;
    const bool water_ok = !body.in_water || body.eyes_in_water;
    if (!body.sprinting && intent.sprint && enough_impulse && intent.may_sprint &&
        !intent.using_item && (!slowed || body.eyes_in_water) && water_ok) {
        body.sprinting = true;
    }
    if (!body.sprinting) return;
    const bool lost = !forward_impulse || !intent.may_sprint;
    if (body.swimming) {
        if ((!body.on_ground && !intent.sneak && lost) || !body.in_water) body.sprinting = false;
        return;
    }
    if (lost || (body.horizontal_collision && !body.minor_horizontal_collision) ||
        (body.in_water && !body.eyes_in_water)) {
        body.sprinting = false;
    }
}

void update_swimming(Body& body, const World& world, const Intent& intent) {
    if (intent.flying) {
        body.swimming = false;
    } else if (body.swimming) {
        body.swimming = body.sprinting && body.in_water;
    } else {
        const auto& here =
            physics_at(world, floor_i(body.x), floor_i(body.y), floor_i(body.z));
        body.swimming = body.sprinting && body.eyes_in_water && here.fluid == FluidKind::Water;
    }
}

void jump(Body& body, const World& world, const Intent& intent) {
    const double power = intent.jump_strength * block_factor(world, body, &BlockPhysics::jump) +
                         0.1 * intent.jump_boost;
    if (power <= 1e-5) return;
    body.vy = std::max(power, body.vy);
    if (body.sprinting) {
        const double radians = intent.yaw_degrees * std::numbers::pi / 180.0;
        body.vx += -std::sin(radians) * kSprintJumpBoost;
        body.vz += std::cos(radians) * kSprintJumpBoost;
    }
}

} // namespace

double Body::height() const { return body_height(pose); }

double Body::eye_height() const { return body_eye(pose); }

bool fits(const Body& body, double height, const BlockAt& at, const BlockShapes& shapes) {
    const World world{at, shapes, body, false};
    return world.free(box_of(body.x, body.y, body.z, height).shrunk(1e-7));
}

void tick(Body& body, const Intent& intent, const BlockAt& at, const BlockShapes& shapes) {
    body.previous_x = body.x;
    body.previous_y = body.y;
    body.previous_z = body.z;
    body.previous_eye = body.eye;

    const bool sneaking = intent.sneak && !intent.flying;
    const World world{at, shapes, body, sneaking};

    update_fluids(body, world);
    update_swimming(body, world, intent);

    // The keys as the game reads them: right is negative, crouching and
    // using an item slow them down.
    double forward = intent.forward;
    double strafe = -intent.strafe;
    if (body.pose == Pose::Crouching && !body.swimming) {
        forward *= intent.sneaking_speed;
        strafe *= intent.sneaking_speed;
    }
    if (intent.using_item) {
        forward *= 0.2;
        strafe *= 0.2;
    }
    update_sprint(body, intent, forward);

    if (intent.flying) {
        body.vy += intent.rise * intent.flying_speed * 3.0;
        body.fall_distance = 0.0;
    }

    if (body.jump_delay > 0) --body.jump_delay;
    if (std::abs(body.vx) < 0.003) body.vx = 0.0;
    if (std::abs(body.vy) < 0.003) body.vy = 0.0;
    if (std::abs(body.vz) < 0.003) body.vz = 0.0;

    const bool jumping = intent.jump && !intent.flying;
    if (jumping) {
        const double depth = body.in_lava ? body.lava_height : body.water_height;
        const bool wet = body.in_water && depth > 0.0;
        if (wet && !(body.on_ground && depth <= kFluidJumpThreshold)) {
            body.vy += 0.04;
        } else if (body.in_lava && !(body.on_ground && depth <= kFluidJumpThreshold)) {
            body.vy += 0.04;
        } else if ((body.on_ground || (wet && depth <= kFluidJumpThreshold)) &&
                   body.jump_delay == 0) {
            jump(body, world, intent);
            body.jump_delay = 10;
        }
    } else {
        body.jump_delay = 0;
    }

    forward *= kInputScale;
    strafe *= kInputScale;

    if (intent.flying) {
        const double rise = body.vy;
        travel(body, world, intent, strafe, forward, jumping);
        body.vy = rise * 0.6;
        body.fall_distance = 0.0;
    } else {
        travel(body, world, intent, strafe, forward, jumping);
    }

    // The view bobs with the speed the body carries out of the tick, on the
    // ground and not swimming; the step counter follows the distance covered.
    const double covered = std::hypot(body.x - body.previous_x, body.z - body.previous_z);
    body.previous_walked = body.walked;
    body.walked += covered * 0.6;
    body.previous_sway = body.sway;
    const double carried = std::min(kMaxSway, std::hypot(body.vx, body.vz));
    body.sway += ((body.on_ground && !body.swimming ? carried : 0.0) - body.sway) * 0.4;

    // The pose follows the move, so the keys of this tick were read with the
    // last one's: crouching slows you from the tick after shift goes down.
    update_pose(body, world, intent);
    body.eye += (body.eye_height() - body.eye) * 0.5;
}

void tick(Body& body, const Intent& intent, const BlockAt& at, const BlockTable& table) {
    static std::mutex guard;
    static std::map<const BlockTable*, std::unique_ptr<BlockShapes>> cache;
    const BlockShapes* shapes = nullptr;
    {
        std::lock_guard lock(guard);
        auto& entry = cache[&table];
        if (!entry || entry->size() != table.state_count()) {
            entry = std::make_unique<BlockShapes>();
            for (size_t s = 0; s < table.state_count(); ++s) {
                const auto state = static_cast<int32_t>(s);
                std::string_view name = "minecraft:air";
                if (table.fluid(state)) name = "minecraft:water";
                else if (table.full_cube(state)) name = "minecraft:stone";
                entry->define(state, name, {});
            }
        }
        shapes = entry.get();
    }
    tick(body, intent, at, *shapes);
}

} // namespace mcpp::world
