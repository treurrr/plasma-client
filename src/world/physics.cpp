#include "mcpp/world/physics.hpp"

#include "mcpp/jmath.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace mcpp::world {
namespace {

// The game's degrees-to-radians, a float.
constexpr float kRadians = 0.017453292f;
constexpr double kEpsilon = 1.0e-7;

// `Mth.floor`: rounds a double down to an int.
int32_t floor_i(double v) {
    const auto i = static_cast<int32_t>(v);
    return v < static_cast<double>(i) ? i - 1 : i;
}

// `Mth.ceil`: rounds a double up to an int.
int32_t ceil_i(double v) {
    const auto i = static_cast<int32_t>(v);
    return v > static_cast<double>(i) ? i + 1 : i;
}

// `Mth.equal`: two doubles within 1e-5f of each other.
bool near(double a, double b) { return std::abs(b - a) < static_cast<double>(1.0e-5f); }

struct Vec {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    double operator[](int axis) const { return axis == 0 ? x : axis == 1 ? y : z; }
    double& operator[](int axis) { return axis == 0 ? x : axis == 1 ? y : z; }
    // `Vec3.lengthSqr`: the squared length.
    double length_sqr() const { return x * x + y * y + z * z; }
    // `Vec3.length`: the length.
    double length() const { return std::sqrt(length_sqr()); }
    // `Vec3.horizontalDistanceSqr`: the squared length in x and z.
    double horizontal_sqr() const { return x * x + z * z; }
    // `Vec3.scale`: every axis times one factor.
    Vec scale(double by) const { return {x * by, y * by, z * by}; }
    // `Vec3.normalize`: unit length, or zero when too short.
    Vec normalize() const {
        const double d = length();
        return d < static_cast<double>(1.0e-5f) ? Vec{} : Vec{x / d, y / d, z / d};
    }
};

struct Box {
    double min[3];
    double max[3];

    // `AABB.move`: the box shifted.
    Box move(double dx, double dy, double dz) const {
        return {{min[0] + dx, min[1] + dy, min[2] + dz}, {max[0] + dx, max[1] + dy, max[2] + dz}};
    }
    Box move(const Vec& v) const { return move(v.x, v.y, v.z); }
    // `AABB.expandTowards`: the box stretched along a move.
    Box expand_towards(double dx, double dy, double dz) const {
        Box out = *this;
        const double d[3] = {dx, dy, dz};
        for (int i = 0; i < 3; ++i) {
            if (d[i] < 0.0) out.min[i] += d[i];
            else if (d[i] > 0.0) out.max[i] += d[i];
        }
        return out;
    }
    // `AABB.deflate`: the box shrunk on every side.
    Box deflate(double by) const {
        return {{min[0] + by, min[1] + by, min[2] + by}, {max[0] - by, max[1] - by, max[2] - by}};
    }
    // `AABB.intersects`: whether two boxes overlap.
    bool intersects(double x0, double y0, double z0, double x1, double y1, double z1) const {
        return min[0] < x1 && max[0] > x0 && min[1] < y1 && max[1] > y0 && min[2] < z1 && max[2] > z0;
    }
};

float pose_height(Pose pose) {
    switch (pose) {
        case Pose::Crouching: return kCrouchingHeight;
        case Pose::Swimming: return kSwimmingHeight;
        default: return kStandingHeight;
    }
}

float pose_eye(Pose pose) {
    switch (pose) {
        case Pose::Crouching: return kCrouchingEye;
        case Pose::Swimming: return kSwimmingEye;
        default: return kStandingEye;
    }
}

// `EntityDimensions.makeBoundingBox`: the body's box at a position.
Box box_at(double x, double y, double z, Pose pose) {
    const float half = kPlayerWidth / 2.0f;
    const float height = pose_height(pose);
    return {{x - half, y, z - half}, {x + half, y + height, z + half}};
}

Box box_of(const Body& b) { return box_at(b.x, b.y, b.z, b.pose); }

// The world as one tick sees it. Every block is asked for once per tick at
// most; the movement asks for the same few dozen over and over.
class Level {
public:
    explicit Level(const World& world)
        : shapes(world.shapes), tags(world.tags), min_y(world.min_y), at_(world.at) {
        const int32_t stone = shapes.find("minecraft:stone");
        block_shape = stone >= 0 ? shapes.state(stone).collision : 0;
        scaffolding_bottom = shapes.named("scaffolding_unstable_bottom");
        powder_snow_falling = shapes.named("powder_snow_falling");
    }

    int32_t raw(int32_t x, int32_t y, int32_t z) const {
        const uint64_t key = ((static_cast<uint64_t>(static_cast<uint32_t>(x)) & 0x3FFFFFFu) << 38) |
                             ((static_cast<uint64_t>(static_cast<uint32_t>(z)) & 0x3FFFFFFu) << 12) |
                             (static_cast<uint64_t>(static_cast<uint32_t>(y)) & 0xFFFu);
        const size_t slot = static_cast<size_t>((key * 0x9E3779B97F4A7C15ull) >> 54);
        if (!used_[slot] || keys_[slot] != key) {
            used_[slot] = true;
            keys_[slot] = key;
            values_[slot] = at_(x, y, z);
        }
        return values_[slot];
    }
    int32_t state(int32_t x, int32_t y, int32_t z) const { return std::max(0, raw(x, y, z)); }
    const StateInfo& info(int32_t x, int32_t y, int32_t z) const {
        return shapes.state(state(x, y, z));
    }
    const BlockInfo& block(int32_t x, int32_t y, int32_t z) const {
        return shapes.block(state(x, y, z));
    }
    bool loaded(int32_t x, int32_t z) const { return raw(x, min_y, z) >= 0; }
    bool loaded(int32_t x0, int32_t z0, int32_t x1, int32_t z1) const {
        for (int32_t cx = x0 >> 4; cx <= x1 >> 4; ++cx) {
            for (int32_t cz = z0 >> 4; cz <= z1 >> 4; ++cz) {
                if (!loaded(cx * 16, cz * 16)) return false;
            }
        }
        return true;
    }
    bool tagged(int32_t state, uint8_t bit) const {
        return tags != nullptr && tags->has(shapes.block(state), shapes, state, bit);
    }

    const BlockShapes& shapes;
    const BlockTags* tags;
    int32_t min_y;
    uint16_t block_shape = 0;
    int32_t scaffolding_bottom = -1;
    int32_t powder_snow_falling = -1;

private:
    const BlockAt& at_;
    mutable std::array<uint64_t, 1024> keys_{};
    mutable std::array<int32_t, 1024> values_{};
    mutable std::array<bool, 1024> used_{};
};

// Who is asking for a collision shape: scaffolding and powder snow answer
// differently to someone above them, sneaking, or falling.
struct Context {
    bool descending = false;
    double bottom = 0.0;
    double fall_distance = 0.0;
    bool leather_boots = false;
};

Context context_of(const Body& b, const Keys& keys, const Stats& stats) {
    return {keys.shift, b.y, b.fall_distance, stats.leather_boots};
}

// A collision shape where it stands in the world.
struct Placed {
    const Voxels* v;
    double offset[3];
    int32_t pos[3];
    bool block;

    double edge(const BlockShapes& shapes, int axis, int i) const {
        return (shapes.edges(*v, axis)[static_cast<size_t>(i)] + offset[axis]) +
               static_cast<double>(pos[axis]);
    }
    int cells(int axis) const { return v->edges[axis] - 1; }
};

// `ScaffoldingBlock.getCollisionShape`, `PowderSnowBlock.getCollisionShape`: the shape a block shows to who is asking.
bool collision_shape(const Level& level, const Context& c, int32_t x, int32_t y, int32_t z,
                     int32_t state, Placed& out) {
    const auto& shapes = level.shapes;
    const auto& info = shapes.state(state);
    const auto& block = shapes.block(state);
    int32_t shape = info.collision;
    if (block.special == Special::Scaffolding) {
        if (c.bottom > y + 1.0 - static_cast<double>(1.0e-5f) && !c.descending) {
            shape = info.collision;
        } else if (shapes.property(state, "distance") != "0" &&
                   shapes.property(state, "bottom") == "true" &&
                   c.bottom > y + 0.0 - static_cast<double>(1.0e-5f)) {
            shape = level.scaffolding_bottom;
        } else {
            return false;
        }
    } else if (block.special == Special::PowderSnow) {
        if (c.fall_distance > 2.5) {
            shape = level.powder_snow_falling;
        } else if (c.leather_boots && c.bottom > y + 1.0 - static_cast<double>(1.0e-5f) &&
                   !c.descending) {
            shape = level.block_shape;
        } else {
            return false;
        }
    }
    if (shape < 0 || shapes.empty(static_cast<uint16_t>(shape))) return false;
    out.v = &shapes.voxels(static_cast<uint16_t>(shape));
    out.offset[0] = out.offset[1] = out.offset[2] = 0.0;
    if (block.offset != 0) shapes.offset(state, x, z, out.offset);
    out.pos[0] = x;
    out.pos[1] = y;
    out.pos[2] = z;
    out.block = shape == level.block_shape && block.offset == 0;
    return true;
}

// Whether any full cell of the shape overlaps the box, the way the game
// merges the two shapes' edges: anything thinner than 1e-7 does not count.
bool overlaps(const Level& level, const Placed& p, const Box& box) {
    if (p.block) {
        return box.intersects(p.pos[0], p.pos[1], p.pos[2], p.pos[0] + 1.0, p.pos[1] + 1.0,
                              p.pos[2] + 1.0);
    }
    const auto& shapes = level.shapes;
    for (int z = 0; z < p.cells(2); ++z) {
        const double z0 = std::max(p.edge(shapes, 2, z), box.min[2]);
        const double z1 = std::min(p.edge(shapes, 2, z + 1), box.max[2]);
        if (!(z1 - z0 > kEpsilon)) continue;
        for (int y = 0; y < p.cells(1); ++y) {
            const double y0 = std::max(p.edge(shapes, 1, y), box.min[1]);
            const double y1 = std::min(p.edge(shapes, 1, y + 1), box.max[1]);
            if (!(y1 - y0 > kEpsilon)) continue;
            for (int x = 0; x < p.cells(0); ++x) {
                if (!shapes.full(*p.v, x, y, z)) continue;
                const double x0 = std::max(p.edge(shapes, 0, x), box.min[0]);
                const double x1 = std::min(p.edge(shapes, 0, x + 1), box.max[0]);
                if (x1 - x0 > kEpsilon) return true;
            }
        }
    }
    return false;
}

// `BlockCollisions.computeNext`: the block shapes a box touches, in walk order.
// Every block shape that touches the box, in the order the game walks them:
// x fastest, then y, then z, one block beyond on every side for the shapes
// that reach out of their cell.
template <class Visit>
void block_collisions(const Level& level, const Context& c, const Box& box, bool suffocating_only,
                      Visit&& visit) {
    const int32_t x0 = floor_i(box.min[0] - kEpsilon) - 1;
    const int32_t x1 = floor_i(box.max[0] + kEpsilon) + 1;
    const int32_t y0 = floor_i(box.min[1] - kEpsilon) - 1;
    const int32_t y1 = floor_i(box.max[1] + kEpsilon) + 1;
    const int32_t z0 = floor_i(box.min[2] - kEpsilon) - 1;
    const int32_t z1 = floor_i(box.max[2] + kEpsilon) + 1;
    for (int32_t z = z0; z <= z1; ++z) {
        const int edge_z = (z == z0 || z == z1) ? 1 : 0;
        for (int32_t y = y0; y <= y1; ++y) {
            const int edge_y = (y == y0 || y == y1) ? 1 : 0;
            for (int32_t x = x0; x <= x1; ++x) {
                const int type = edge_z + edge_y + ((x == x0 || x == x1) ? 1 : 0);
                if (type == 3 || type == 2) continue;
                const int32_t raw = level.raw(x, y, z);
                if (raw <= 0) continue;
                const auto& info = level.shapes.state(raw);
                if (suffocating_only && (info.flags & kSuffocating) == 0) continue;
                if (type == 1 && (info.flags & kLargeCollision) == 0) continue;
                Placed p;
                if (!collision_shape(level, c, x, y, z, raw, p)) continue;
                if (!overlaps(level, p, box)) continue;
                if (!visit(p)) return;
            }
        }
    }
}

// `CollisionGetter.noBlockCollision`: whether no block is in the box.
bool no_collision(const Level& level, const Context& c, const Box& box) {
    bool free = true;
    block_collisions(level, c, box, false, [&](const Placed&) {
        free = false;
        return false;
    });
    return free;
}

// `CollisionGetter.collidesWithSuffocatingBlock`: whether a suffocating block is in the box.
bool suffocating_in(const Level& level, const Context& c, const Box& box) {
    bool found = false;
    block_collisions(level, c, box, true, [&](const Placed&) {
        found = true;
        return false;
    });
    return found;
}

// `VoxelShape.collide`, `VoxelShape.collideX`: how far a box moves along an axis into one shape.
// How far the box may move along one axis before this shape stops it, cell
// by cell as the game walks a shape.
double collide_one(const Level& level, const Placed& p, int axis, const Box& box, double dist) {
    if (std::abs(dist) < kEpsilon) return 0.0;
    const auto& shapes = level.shapes;
    const int a = axis;
    const int b = (axis + 1) % 3;
    const int c = (axis + 2) % 3;
    const auto find = [&](int ax, double value) {
        int index = -1;
        for (int i = 0; i < p.v->edges[ax]; ++i) {
            if (value < p.edge(shapes, ax, i)) break;
            index = i;
        }
        return index;
    };
    const double max_a = box.max[a];
    const double min_a = box.min[a];
    const int i_min = find(a, min_a + kEpsilon);
    const int i_max = find(a, max_a - kEpsilon);
    const int b0 = std::max(0, find(b, box.min[b] + kEpsilon));
    const int b1 = std::min(p.cells(b), find(b, box.max[b] - kEpsilon) + 1);
    const int c0 = std::max(0, find(c, box.min[c] + kEpsilon));
    const int c1 = std::min(p.cells(c), find(c, box.max[c] - kEpsilon) + 1);
    const auto full = [&](int n, int ib, int ic) {
        int idx[3];
        idx[a] = n;
        idx[b] = ib;
        idx[c] = ic;
        return shapes.full(*p.v, idx[0], idx[1], idx[2]);
    };
    const auto any = [&](int n) {
        for (int ib = b0; ib < b1; ++ib) {
            for (int ic = c0; ic < c1; ++ic) {
                if (full(n, ib, ic)) return true;
            }
        }
        return false;
    };
    if (dist > 0.0) {
        for (int n = i_max + 1; n < p.cells(a); ++n) {
            if (!any(n)) continue;
            const double gap = p.edge(shapes, a, n) - max_a;
            if (gap >= -kEpsilon) dist = std::min(dist, gap);
            return dist;
        }
    } else if (dist < 0.0) {
        for (int n = i_min - 1; n >= 0; --n) {
            if (!any(n)) continue;
            const double gap = p.edge(shapes, a, n + 1) - min_a;
            if (gap <= kEpsilon) dist = std::max(dist, gap);
            return dist;
        }
    }
    return dist;
}

// `Shapes.collide`: one axis of a move against every shape.
double collide_axis(const Level& level, int axis, const Box& box, const std::vector<Placed>& shapes,
                    double dist) {
    for (const auto& p : shapes) {
        if (std::abs(dist) < kEpsilon) return 0.0;
        dist = collide_one(level, p, axis, box, dist);
    }
    return dist;
}

// `Entity.collideWithShapes`: a move against shapes, axis by axis.
Vec collide_with_shapes(const Level& level, const Vec& m, const Box& box,
                        const std::vector<Placed>& shapes) {
    if (shapes.empty()) return m;
    Vec out;
    const bool z_first = std::abs(m.x) < std::abs(m.z);
    const int order[3] = {1, z_first ? 2 : 0, z_first ? 0 : 2};
    for (const int axis : order) {
        const double d = m[axis];
        if (d != 0.0) out[axis] = collide_axis(level, axis, box.move(out), shapes, d);
    }
    return out;
}

// Scratch lists, reused so a tick allocates nothing once it has warmed up.
struct Scratch {
    std::vector<Placed> plain;
    std::vector<Placed> step;
    std::vector<float> heights;
};

Scratch& scratch() {
    thread_local Scratch s;
    return s;
}

void collect(const Level& level, const Context& c, const Box& box, std::vector<Placed>& out) {
    out.clear();
    block_collisions(level, c, box, false, [&](const Placed& p) {
        out.push_back(p);
        return true;
    });
}

class Mover {
public:
    Mover(Body& body, const Keys& keys, const Stats& stats, const Level& level)
        : b(body), keys(keys), stats(stats), level(level) {}

    Body& b;
    const Keys& keys;
    const Stats& stats;
    const Level& level;

    bool flying() const { return b.flying; }
    // `Player.isSwimming`: swimming, unless flying or a spectator.
    bool swimming() const { return b.swimming && !b.flying && !stats.spectator; }
    bool in_lava() const { return b.in_lava(); }
    Context context() const { return context_of(b, keys, stats); }

    int32_t block_x() const { return floor_i(b.x); }
    int32_t block_y() const { return floor_i(b.y); }
    int32_t block_z() const { return floor_i(b.z); }

    float speed() const { return static_cast<float>(Body::speed(stats, b.sprinting)); }

    // `Player.canPlayerFitWithinBlocksAndEntitiesWhen`: whether a pose's box is free.
    bool fits(Pose pose) const {
        return no_collision(level, context(), box_at(b.x, b.y, b.z, pose).deflate(kEpsilon));
    }

    // `Entity.getOnPos`: the block under the feet.
    // The block under the feet that movement reads: the one the body stands
    // on, or the cell straight down.
    void on_pos(float offset, int32_t out[3]) const {
        if (b.has_support) {
            out[0] = b.support[0];
            out[1] = b.support[1];
            out[2] = b.support[2];
            if (!(offset > 1.0e-5f)) return;
            const int32_t s = level.state(out[0], out[1], out[2]);
            if ((!(offset <= 0.5) || !level.tagged(s, BlockTags::kFences)) &&
                !level.tagged(s, BlockTags::kWalls) && !level.tagged(s, BlockTags::kFenceGate)) {
                out[1] = floor_i(b.y - static_cast<double>(offset));
            }
            return;
        }
        out[0] = floor_i(b.x);
        out[1] = floor_i(b.y - static_cast<double>(offset));
        out[2] = floor_i(b.z);
    }

    const BlockInfo& block_at(const int32_t p[3]) const { return level.block(p[0], p[1], p[2]); }

    // `Player.getBlockSpeedFactor`, `LivingEntity.getBlockSpeedFactor`, `Entity.getBlockSpeedFactor`: how much the block slows walking.
    float block_speed_factor() const {
        if (b.flying) return 1.0f;
        const int32_t here = level.state(block_x(), block_y(), block_z());
        const auto& block = level.shapes.block(here);
        float f = block.speed;
        if (block.special != Special::Water && block.special != Special::BubbleColumn && f == 1.0f) {
            int32_t below[3];
            on_pos(0.500001f, below);
            f = block_at(below).speed;
        }
        const auto t = static_cast<float>(stats.movement_efficiency);
        return f + t * (1.0f - f);
    }

    // `Entity.getBlockJumpFactor`: how much the block weakens a jump.
    float block_jump_factor() const {
        const float here = level.block(block_x(), block_y(), block_z()).jump;
        int32_t below[3];
        on_pos(0.500001f, below);
        const float under = block_at(below).jump;
        return here == 1.0f ? under : here;
    }

    // `Player.onClimbable`, `LivingEntity.onClimbable`, `LivingEntity.trapdoorUsableAsLadder`: whether the body is on a ladder or vine.
    bool on_climbable() const {
        if (b.flying) return false;
        if (stats.spectator) return false;
        const int32_t x = block_x();
        const int32_t y = block_y();
        const int32_t z = block_z();
        const int32_t s = level.state(x, y, z);
        if (level.tagged(s, BlockTags::kClimbable)) return true;
        const auto& block = level.shapes.block(s);
        if (block.special != Special::Trapdoor) return false;
        if (level.shapes.property(s, "open") != "true") return false;
        const int32_t below = level.state(x, y - 1, z);
        return level.shapes.block(below).special == Special::Ladder &&
               level.shapes.property(below, "facing") == level.shapes.property(s, "facing");
    }

    // `FlowingFluid.getHeight`: a fluid's height, full under the same fluid.
    float fluid_height_at(int32_t x, int32_t y, int32_t z, const StateInfo& s) const {
        if (level.info(x, y + 1, z).fluid == s.fluid) return 1.0f;
        return s.fluid_height();
    }

    // `FlowingFluid.getFlow`: the way a fluid pushes in a cell.
    Vec flow(int32_t x, int32_t y, int32_t z, const StateInfo& s) const {
        static constexpr int kSteps[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
        static constexpr int kFaces[4] = {2, 5, 3, 4};
        const auto affects = [&](const StateInfo& n) {
            return n.fluid == Fluid::None || n.fluid == s.fluid;
        };
        double dx = 0.0;
        double dz = 0.0;
        for (const auto& step : kSteps) {
            const int32_t nx = x + step[0];
            const int32_t nz = z + step[1];
            const auto& n = level.info(nx, y, nz);
            if (!affects(n)) continue;
            float h = n.fluid_height();
            float d = 0.0f;
            if (h == 0.0f) {
                if ((n.flags & kBlocksMotion) == 0) {
                    const auto& under = level.info(nx, y - 1, nz);
                    if (affects(under)) {
                        h = under.fluid_height();
                        if (h > 0.0f) d = s.fluid_height() - (h - 0.8888889f);
                    }
                }
            } else if (h > 0.0f) {
                d = s.fluid_height() - h;
            }
            if (d != 0.0f) {
                dx += static_cast<float>(step[0]) * d;
                dz += static_cast<float>(step[1]) * d;
            }
        }
        Vec v{dx, 0.0, dz};
        if (s.fluid_falling) {
            const auto solid = [&](int32_t px, int32_t py, int32_t pz, int face) {
                const int32_t st = level.state(px, py, pz);
                const auto& info = level.shapes.state(st);
                if (info.fluid == s.fluid) return false;
                if (level.shapes.block(st).special == Special::Ice) return false;
                return info.sturdy(face);
            };
            for (int i = 0; i < 4; ++i) {
                const int32_t nx = x + kSteps[i][0];
                const int32_t nz = z + kSteps[i][1];
                if (solid(nx, y, nz, kFaces[i]) || solid(nx, y + 1, nz, kFaces[i])) {
                    v = v.normalize();
                    v.y += -6.0;
                    break;
                }
            }
        }
        return v.normalize();
    }

    // `Entity.touchingUnloadedChunk`: whether the box reaches an unloaded chunk.
    bool touching_unloaded() const {
        const Box box = box_of(b);
        return !level.loaded(floor_i(box.min[0] - 1.0), floor_i(box.min[2] - 1.0),
                             ceil_i(box.max[0] + 1.0), ceil_i(box.max[2] + 1.0));
    }

    // `Entity.updateFluidHeightAndDoFluidPushing`: fluid height and current push.
    bool fluid_push(Fluid kind, double scale) {
        if (touching_unloaded()) return false;
        const Box box = box_of(b).deflate(0.001);
        const int32_t x0 = floor_i(box.min[0]);
        const int32_t x1 = ceil_i(box.max[0]);
        const int32_t y0 = floor_i(box.min[1]);
        const int32_t y1 = ceil_i(box.max[1]);
        const int32_t z0 = floor_i(box.min[2]);
        const int32_t z1 = ceil_i(box.max[2]);
        double height = 0.0;
        const bool pushed = !b.flying;
        bool found = false;
        Vec push;
        int count = 0;
        for (int32_t x = x0; x < x1; ++x) {
            for (int32_t y = y0; y < y1; ++y) {
                for (int32_t z = z0; z < z1; ++z) {
                    const auto& s = level.info(x, y, z);
                    if (s.fluid != kind) continue;
                    const double top =
                        static_cast<double>(static_cast<float>(y) + fluid_height_at(x, y, z, s));
                    if (!(top >= box.min[1])) continue;
                    found = true;
                    height = std::max(top - box.min[1], height);
                    if (pushed) {
                        Vec f = flow(x, y, z, s);
                        if (height < 0.4) f = f.scale(height);
                        push.x += f.x;
                        push.y += f.y;
                        push.z += f.z;
                        ++count;
                    }
                }
            }
        }
        if (push.length() > 0.0) {
            if (count > 0) push = push.scale(1.0 / count);
            push = push.scale(scale);
            if (std::abs(b.vx) < 0.003 && std::abs(b.vz) < 0.003 &&
                push.length() < 0.0045000000000000005) {
                push = push.normalize().scale(0.0045000000000000005);
            }
            b.vx += push.x;
            b.vy += push.y;
            b.vz += push.z;
        }
        if (kind == Fluid::Water) b.water_height = height;
        else b.lava_height = height;
        return found;
    }

    // `Entity.updateInWaterStateAndDoWaterCurrentPushing`: in water, and its push.
    void water_state() {
        if (fluid_push(Fluid::Water, 0.014)) {
            b.fall_distance = 0.0;
            b.touching_water = true;
        } else {
            b.touching_water = false;
        }
    }

    // `Entity.updateInWaterStateAndDoFluidPushing`: water and lava for this tick.
    void fluids() {
        b.water_height = 0.0;
        b.lava_height = 0.0;
        water_state();
        fluid_push(Fluid::Lava, stats.fast_lava ? 0.007 : 0.0023333333333333335);
    }

    // `Entity.updateFluidOnEyes`: whether the eyes are under water.
    void fluid_on_eyes() {
        const double eye_y = b.y + static_cast<double>(b.eye_height());
        const int32_t x = floor_i(b.x);
        const int32_t y = floor_i(eye_y);
        const int32_t z = floor_i(b.z);
        const auto& s = level.info(x, y, z);
        b.eye_in_water = false;
        if (s.fluid == Fluid::None) return;
        const double top = static_cast<double>(static_cast<float>(y) + fluid_height_at(x, y, z, s));
        if (top > eye_y) b.eye_in_water = s.fluid == Fluid::Water;
    }

    // `Player.updateSwimming`, `Entity.updateSwimming`: start or stop swimming.
    void update_swimming() {
        if (b.flying) {
            b.swimming = false;
        } else if (swimming()) {
            b.swimming = b.sprinting && b.touching_water;
        } else {
            b.swimming = b.sprinting && b.underwater &&
                         level.info(block_x(), block_y(), block_z()).fluid == Fluid::Water;
        }
    }

    // `LocalPlayer.suffocatesAt`: whether a column next to the body suffocates.
    bool suffocates_at(int32_t x, int32_t z) const {
        const Box bb = box_of(b);
        const Box cell = Box{{static_cast<double>(x), bb.min[1], static_cast<double>(z)},
                             {x + 1.0, bb.max[1], z + 1.0}}
                             .deflate(kEpsilon);
        return suffocating_in(level, context(), cell);
    }

    // `LocalPlayer.moveTowardsClosestSpace`: a nudge out of a wall.
    void move_towards_closest_space(double px, double pz) {
        const int32_t x = floor_i(px);
        const int32_t z = floor_i(pz);
        if (!suffocates_at(x, z)) return;
        const double fx = px - x;
        const double fz = pz - z;
        // West, east, north, south.
        static constexpr int kSteps[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
        int best = -1;
        double closest = std::numeric_limits<double>::max();
        for (int i = 0; i < 4; ++i) {
            const bool along_x = kSteps[i][0] != 0;
            const bool positive = kSteps[i][0] + kSteps[i][1] > 0;
            const double v = along_x ? fx : fz;
            const double distance = positive ? 1.0 - v : v;
            if (distance < closest && !suffocates_at(x + kSteps[i][0], z + kSteps[i][1])) {
                closest = distance;
                best = i;
            }
        }
        if (best < 0) return;
        if (kSteps[best][0] != 0) b.vx = 0.1 * kSteps[best][0];
        else b.vz = 0.1 * kSteps[best][1];
    }

    // `Entity.moveRelative`: adds the input push to the velocity.
    void move_relative(float speed_, double ix, double iy, double iz) {
        double push[3];
        input_vector(ix, iy, iz, speed_, b.yaw, push);
        b.vx += push[0];
        b.vy += push[1];
        b.vz += push[2];
    }

    // `LocalPlayer.isHorizontalCollisionMinor`: whether a wall only grazed the move.
    bool horizontal_collision_minor(const Vec& moved) const {
        const float angle = b.yaw * kRadians;
        const double s = jmath::sin(angle);
        const double c = jmath::cos(angle);
        const double wx = b.xxa * c - b.zza * s;
        const double wz = b.zza * c + b.xxa * s;
        const double want = wx * wx + wz * wz;
        const double got = moved.x * moved.x + moved.z * moved.z;
        if (want < 1.0e-5f || got < 1.0e-5f) return false;
        const double dot = wx * moved.x + wz * moved.z;
        return std::acos(dot / std::sqrt(want * got)) < 0.13962634f;
    }

    // `Player.canFallAtLeast`: whether nothing is under the box for a depth.
    bool can_fall_at_least(double dx, double dz, double depth) const {
        const Box bb = box_of(b);
        const Box probe{{bb.min[0] + kEpsilon + dx, bb.min[1] - depth - kEpsilon,
                         bb.min[2] + kEpsilon + dz},
                        {bb.max[0] - kEpsilon + dx, bb.min[1], bb.max[2] - kEpsilon + dz}};
        return no_collision(level, context(), probe);
    }

    // `Player.maybeBackOffFromEdge`, `Player.isAboveGround`: sneaking stops at edges.
    Vec back_off_from_edge(Vec m) const {
        const float step = stats.step_height;
        if (b.flying || m.y > 0.0 || !keys.shift) return m;
        const bool above_ground =
            b.on_ground ||
            (b.fall_distance < step && !can_fall_at_least(0.0, 0.0, step - b.fall_distance));
        if (!above_ground) return m;
        double dx = m.x;
        double dz = m.z;
        const double sx = (dx > 0.0 ? 1.0 : dx < 0.0 ? -1.0 : 0.0) * 0.05;
        const double sz = (dz > 0.0 ? 1.0 : dz < 0.0 ? -1.0 : 0.0) * 0.05;
        while (dx != 0.0 && can_fall_at_least(dx, 0.0, step)) {
            if (std::abs(dx) <= 0.05) {
                dx = 0.0;
                break;
            }
            dx -= sx;
        }
        while (dz != 0.0 && can_fall_at_least(0.0, dz, step)) {
            if (std::abs(dz) <= 0.05) {
                dz = 0.0;
                break;
            }
            dz -= sz;
        }
        while (dx != 0.0 && dz != 0.0 && can_fall_at_least(dx, dz, step)) {
            if (std::abs(dx) <= 0.05) dx = 0.0;
            else dx -= sx;
            if (std::abs(dz) <= 0.05) dz = 0.0;
            else dz -= sz;
        }
        return {dx, m.y, dz};
    }

    // `Entity.collide`, `Entity.collectCandidateStepUpHeights`: a move against blocks, stepping up.
    Vec collide(const Vec& m) const {
        auto& s = scratch();
        const Box bb = box_of(b);
        const Context c = context();
        Vec plain = m;
        if (m.length_sqr() != 0.0) {
            collect(level, c, bb.expand_towards(m.x, m.y, m.z), s.plain);
            plain = collide_with_shapes(level, m, bb, s.plain);
        }
        const bool hit_x = m.x != plain.x;
        const bool hit_y = m.y != plain.y;
        const bool hit_z = m.z != plain.z;
        const bool landed = hit_y && m.y < 0.0;
        const float step = stats.step_height;
        if (step > 0.0f && (landed || b.on_ground) && (hit_x || hit_z)) {
            const Box from = landed ? bb.move(0.0, plain.y, 0.0) : bb;
            Box region = from.expand_towards(m.x, step, m.z);
            if (!landed) region = region.expand_towards(0.0, static_cast<double>(-1.0e-5f), 0.0);
            collect(level, c, region, s.step);
            const auto plain_y = static_cast<float>(plain.y);
            s.heights.clear();
            for (const auto& p : s.step) {
                for (int i = 0; i < p.v->edges[1]; ++i) {
                    const auto h = static_cast<float>(p.edge(level.shapes, 1, i) - from.min[1]);
                    if (h < 0.0f || h == plain_y) continue;
                    if (h > step) break;
                    if (std::ranges::find(s.heights, h) == s.heights.end()) s.heights.push_back(h);
                }
            }
            std::ranges::sort(s.heights);
            for (const float h : s.heights) {
                const Vec v = collide_with_shapes(level, {m.x, h, m.z}, from, s.step);
                if (v.horizontal_sqr() > plain.horizontal_sqr()) {
                    const double back = bb.min[1] - from.min[1];
                    return {v.x, v.y - back, v.z};
                }
            }
        }
        return plain;
    }

    // `CollisionGetter.findSupportingBlock`: the nearest block under the feet.
    void find_support(const Box& box, bool& found, int32_t out[3]) const {
        found = false;
        double best = std::numeric_limits<double>::max();
        block_collisions(level, context(), box, false, [&](const Placed& p) {
            const double dx = p.pos[0] + 0.5 - b.x;
            const double dy = p.pos[1] + 0.5 - b.y;
            const double dz = p.pos[2] + 0.5 - b.z;
            const double d = dx * dx + dy * dy + dz * dz;
            const auto later = [&] {
                if (out[1] != p.pos[1]) return out[1] < p.pos[1];
                if (out[2] != p.pos[2]) return out[2] < p.pos[2];
                return out[0] < p.pos[0];
            };
            if (d < best || (d == best && (!found || later()))) {
                out[0] = p.pos[0];
                out[1] = p.pos[1];
                out[2] = p.pos[2];
                best = d;
                found = true;
            }
            return true;
        });
    }

    // `Entity.checkSupportingBlock`: remembers the block stood on.
    void check_support(const Vec* moved) {
        if (!b.on_ground) {
            b.on_ground_no_blocks = false;
            b.has_support = false;
            return;
        }
        const Box bb = box_of(b);
        const Box feet{{bb.min[0], bb.min[1] - 1.0e-6, bb.min[2]}, {bb.max[0], bb.min[1], bb.max[2]}};
        bool found = false;
        int32_t pos[3] = {0, 0, 0};
        find_support(feet, found, pos);
        if (found || b.on_ground_no_blocks) {
            b.has_support = found;
        } else if (moved != nullptr) {
            find_support(feet.move(-moved->x, 0.0, -moved->z), found, pos);
            b.has_support = found;
        }
        if (b.has_support) std::copy(pos, pos + 3, b.support);
        b.on_ground_no_blocks = !found;
    }

    // `Entity.move`: one move with collisions and their effects.
    void move(Vec m) {
        const double x0 = b.x;
        const double z0 = b.z;
        if (stats.spectator) {
            b.x += m.x;
            b.y += m.y;
            b.z += m.z;
            b.horizontal_collision = b.vertical_collision = b.vertical_collision_below = false;
            b.minor_horizontal_collision = false;
            add_walked(x0, z0);
            return;
        }
        const Vec stuck{b.stuck[0], b.stuck[1], b.stuck[2]};
        if (stuck.length_sqr() > 1.0e-7) {
            m = {m.x * stuck.x, m.y * stuck.y, m.z * stuck.z};
            b.stuck[0] = b.stuck[1] = b.stuck[2] = 0.0;
            b.vx = b.vy = b.vz = 0.0;
        }
        m = back_off_from_edge(m);
        const Vec c = collide(m);
        const double moved2 = c.length_sqr();
        if (moved2 > 1.0e-7 || m.length_sqr() - moved2 < 1.0e-7) {
            movement_from = {b.x, b.y, b.z};
            movement_wanted = m;
            moved = true;
            b.x += c.x;
            b.y += c.y;
            b.z += c.z;
        }
        const bool hit_x = !near(m.x, c.x);
        const bool hit_z = !near(m.z, c.z);
        b.horizontal_collision = hit_x || hit_z;
        b.vertical_collision = m.y != c.y;
        b.vertical_collision_below = b.vertical_collision && m.y < 0.0;
        b.on_ground = b.vertical_collision_below;
        check_support(&c);
        b.minor_horizontal_collision = b.horizontal_collision && horizontal_collision_minor(c);

        int32_t on[3];
        on_pos(0.2f, on);
        const auto& below = block_at(on);
        // `LivingEntity.checkFallDamage`, `Entity.checkFallDamage`: fall distance from the move.
        if (!b.touching_water) water_state();
        if (!b.touching_water && c.y < 0.0) b.fall_distance -= static_cast<float>(c.y);
        if (b.on_ground) b.fall_distance = 0.0;

        if (b.horizontal_collision) {
            if (hit_x) b.vx = 0.0;
            if (hit_z) b.vz = 0.0;
        }
        // `SlimeBlock.updateEntityMovementAfterFallOn`, `BedBlock.updateEntityMovementAfterFallOn`: bounce on landing.
        if (m.y != c.y) {
            if (below.special == Special::Slime && !keys.shift) {
                if (b.vy < 0.0) b.vy = -b.vy * 1.0;
            } else if (below.special == Special::Bed && !keys.shift) {
                if (b.vy < 0.0) b.vy = -b.vy * static_cast<double>(0.66f) * 1.0;
            } else {
                b.vy *= 0.0;
            }
        }
        const float f = block_speed_factor();
        b.vx *= f;
        b.vz *= f;

        add_walked(x0, z0);
    }

    void add_walked(double x0, double z0) {
        const auto dx = static_cast<float>(b.x - x0);
        const auto dz = static_cast<float>(b.z - z0);
        b.walk_dist += static_cast<float>(std::sqrt(dx * dx + dz * dz)) * 0.6f;
    }

    // `LivingEntity.getEffectiveGravity`: gravity, less with slow falling.
    double effective_gravity() const {
        return b.vy <= 0.0 && stats.slow_falling ? std::min(stats.gravity, 0.01) : stats.gravity;
    }

    // `Player.getFlyingSpeed`: acceleration off the ground.
    float flying_speed() const {
        if (b.flying) return b.sprinting ? stats.flying_speed * 2.0f : stats.flying_speed;
        return b.sprinting ? 0.025999999f : 0.02f;
    }

    // `LivingEntity.handleOnClimbable`: clamps speed on a ladder.
    void on_climbable_clamp() {
        if (!on_climbable()) return;
        b.fall_distance = 0.0;
        b.vx = std::clamp(b.vx, static_cast<double>(-0.15f), static_cast<double>(0.15f));
        b.vz = std::clamp(b.vz, static_cast<double>(-0.15f), static_cast<double>(0.15f));
        double y = std::max(b.vy, static_cast<double>(-0.15f));
        const bool scaffolding =
            level.block(block_x(), block_y(), block_z()).special == Special::Scaffolding;
        if (y < 0.0 && !scaffolding && keys.shift) y = 0.0;
        b.vy = y;
    }

    // `LivingEntity.travelInAir`, `LivingEntity.handleRelativeFrictionAndCalculateMovement`, `LivingEntity.getFrictionInfluencedSpeed`: a step through air.
    void travel_in_air(double ix, double iz) {
        int32_t below[3];
        on_pos(0.500001f, below);
        const float friction = b.on_ground ? block_at(below).friction : 1.0f;
        const float drag = friction * 0.91f;
        const float accel = b.on_ground ? speed() * (0.21600002f / (friction * friction * friction))
                                        : flying_speed();
        move_relative(accel, ix, 0.0, iz);
        on_climbable_clamp();
        move({b.vx, b.vy, b.vz});
        double vy = b.vy;
        if ((b.horizontal_collision || b.jumping) &&
            (on_climbable() || (b.was_in_powder_snow && stats.leather_boots))) {
            vy = 0.2;
        }
        if (stats.levitation > 0) {
            vy += (0.05 * stats.levitation - vy) * 0.2;
        } else if (level.loaded(below[0], below[2])) {
            vy -= effective_gravity();
        } else if (b.y > level.min_y) {
            vy = -0.1;
        } else {
            vy = 0.0;
        }
        b.vx *= drag;
        b.vy = vy * static_cast<double>(0.98f);
        b.vz *= drag;
    }

    // `Entity.isFree`: whether the box moved there is free of blocks and fluid.
    bool free_at(double dx, double dy, double dz) const {
        const Box box = box_of(b).move(dx, dy, dz);
        if (!no_collision(level, context(), box)) return false;
        for (int32_t x = floor_i(box.min[0]); x < ceil_i(box.max[0]); ++x) {
            for (int32_t y = floor_i(box.min[1]); y < ceil_i(box.max[1]); ++y) {
                for (int32_t z = floor_i(box.min[2]); z < ceil_i(box.max[2]); ++z) {
                    if (level.info(x, y, z).fluid != Fluid::None) return false;
                }
            }
        }
        return true;
    }

    // `LivingEntity.jumpOutOfFluid`: a hop onto a ledge from a fluid.
    void jump_out_of_fluid(double start_y) {
        if (b.horizontal_collision &&
            free_at(b.vx, b.vy + static_cast<double>(0.6f) - b.y + start_y, b.vz)) {
            b.vy = static_cast<double>(0.3f);
        }
    }

    // `LivingEntity.getFluidFallingAdjustedMovement`: sinking in a fluid.
    double fluid_falling(double gravity, bool falling, double vy) const {
        if (gravity == 0.0 || b.sprinting) return vy;
        if (falling && std::abs(vy - 0.005) >= 0.003 && std::abs(vy - gravity / 16.0) < 0.003) {
            return -0.003;
        }
        return vy - gravity / 16.0;
    }

    // `LivingEntity.travelInFluid`, `LivingEntity.travelInWater`, `LivingEntity.travelInLava`: a step through water or lava.
    void travel_in_fluid(double ix, double iz) {
        const bool falling = b.vy <= 0.0;
        const double start_y = b.y;
        const double gravity = effective_gravity();
        if (b.touching_water) {
            float slow = b.sprinting ? 0.9f : 0.8f;
            float accel = 0.02f;
            auto efficiency = static_cast<float>(stats.water_movement_efficiency);
            if (!b.on_ground) efficiency *= 0.5f;
            if (efficiency > 0.0f) {
                slow += (0.54600006f - slow) * efficiency;
                accel += (speed() - accel) * efficiency;
            }
            if (stats.dolphins_grace) slow = 0.96f;
            move_relative(accel, ix, 0.0, iz);
            move({b.vx, b.vy, b.vz});
            double vy = b.vy;
            if (b.horizontal_collision && on_climbable()) vy = 0.2;
            b.vx *= slow;
            vy *= static_cast<double>(0.8f);
            b.vz *= slow;
            b.vy = fluid_falling(gravity, falling, vy);
        } else {
            move_relative(0.02f, ix, 0.0, iz);
            move({b.vx, b.vy, b.vz});
            if (b.lava_height <= fluid_jump_threshold()) {
                b.vx *= 0.5;
                b.vy *= static_cast<double>(0.8f);
                b.vz *= 0.5;
                b.vy = fluid_falling(gravity, falling, b.vy);
            } else {
                b.vx *= 0.5;
                b.vy *= 0.5;
                b.vz *= 0.5;
            }
            if (gravity != 0.0) b.vy += -gravity / 4.0;
        }
        jump_out_of_fluid(start_y);
    }

    // `Entity.getFluidJumpThreshold`: how deep a fluid must be to swim up.
    double fluid_jump_threshold() const { return b.eye_height() < 0.4 ? 0.0 : 0.4; }

    // `Player.travel`, `LivingEntity.travel`: one tick of travel.
    void travel(double ix, double iz) {
        if (swimming()) {
            const float angle = b.pitch * kRadians;
            const double look_y = -static_cast<double>(jmath::sin(angle));
            const double k = look_y < -0.2 ? 0.085 : 0.06;
            const int32_t hx = floor_i(b.x);
            const int32_t hy = floor_i(b.y + 1.0 - 0.1);
            const int32_t hz = floor_i(b.z);
            if (look_y <= 0.0 || b.jumping || level.info(hx, hy, hz).fluid != Fluid::None) {
                b.vy += (look_y - b.vy) * k;
            }
        }
        const double rise = b.vy;
        if ((b.touching_water || in_lava()) && !b.flying) {
            travel_in_fluid(ix, iz);
        } else {
            travel_in_air(ix, iz);
        }
        if (b.flying) b.vy = rise * 0.6;
    }

    // `LivingEntity.jumpFromGround`, `LivingEntity.getJumpPower`, `LivingEntity.getJumpBoostPower`: a jump.
    void jump_from_ground() {
        const float power = static_cast<float>(stats.jump_strength) * 1.0f * block_jump_factor() +
                            (stats.jump_boost > 0
                                 ? 0.1f * (static_cast<float>(stats.jump_boost - 1) + 1.0f)
                                 : 0.0f);
        if (power <= 1.0e-5f) return;
        b.vy = std::max(static_cast<double>(power), b.vy);
        if (b.sprinting) {
            const float angle = b.yaw * kRadians;
            b.vx += -jmath::sin(angle) * 0.2;
            b.vz += jmath::cos(angle) * 0.2;
        }
    }

    // What the blocks the body passed through do to it: webs and bushes
    // hold it, bubble columns carry it, honey walls slow its fall.
    void inside_blocks() {
        // `SlimeBlock.stepOn`: slime slows a slow walk.
        if (b.on_ground) {
            int32_t on[3];
            on_pos(0.2f, on);
            if (block_at(on).special == Special::Slime) {
                const double vy = std::abs(b.vy);
                if (vy < 0.1 && !keys.shift) {
                    const double m = 0.4 + vy * 0.2;
                    b.vx *= m;
                    b.vz *= m;
                }
            }
        }
        const Vec to{b.x, b.y, b.z};
        const Vec from = moved ? movement_from : Vec{b.xo, b.yo, b.zo};
        const Box at_to = box_of(b).deflate(static_cast<double>(1.0e-5f));
        const Vec d{to.x - from.x, to.y - from.y, to.z - from.z};
        const bool long_move = d.length_sqr() > 0.9999900000002526 * 0.9999900000002526;
        // The body goes one axis at a time, in the order the collision took
        // them; every block it passes through on the way counts once.
        std::array<int32_t, 3 * 128> seen{};
        size_t seen_count = 0;
        const auto first_visit = [&](int32_t x, int32_t y, int32_t z) {
            for (size_t i = 0; i < seen_count; ++i) {
                if (seen[i * 3] == x && seen[i * 3 + 1] == y && seen[i * 3 + 2] == z) return false;
            }
            if (seen_count < 128) {
                seen[seen_count * 3] = x;
                seen[seen_count * 3 + 1] = y;
                seen[seen_count * 3 + 2] = z;
                ++seen_count;
            }
            return true;
        };
        Box steps[3];
        int step_count = 0;
        Box cur = at_to.move(-d.x, -d.y, -d.z);
        if (moved && d.length_sqr() > 0.0) {
            const bool z_first = std::abs(movement_wanted.x) < std::abs(movement_wanted.z);
            const int order[3] = {1, z_first ? 2 : 0, z_first ? 0 : 2};
            for (const int axis : order) {
                if (d[axis] == 0.0) continue;
                Box next = cur;
                next.min[axis] += d[axis];
                next.max[axis] += d[axis];
                Box swept = cur;
                for (int i = 0; i < 3; ++i) {
                    swept.min[i] = std::min(cur.min[i], next.min[i]);
                    swept.max[i] = std::max(cur.max[i], next.max[i]);
                }
                steps[step_count++] = swept;
                cur = next;
            }
        }
        if (step_count == 0) {
            Box swept = cur;
            for (int i = 0; i < 3; ++i) {
                swept.min[i] = std::min(cur.min[i], at_to.min[i]);
                swept.max[i] = std::max(cur.max[i], at_to.max[i]);
            }
            steps[step_count++] = swept;
        }
        bool powder = false;
        for (int k = 0; k < step_count; ++k) {
        const Box& swept = steps[k];
        for (int32_t x = floor_i(swept.min[0]); x <= floor_i(swept.max[0]); ++x) {
            for (int32_t y = floor_i(swept.min[1]); y <= floor_i(swept.max[1]); ++y) {
                for (int32_t z = floor_i(swept.min[2]); z <= floor_i(swept.max[2]); ++z) {
                    const int32_t s = level.state(x, y, z);
                    if (s == 0 || !first_visit(x, y, z)) continue;
                    const auto& block = level.shapes.block(s);
                    switch (block.special) {
                        case Special::Cobweb:
                            b.fall_distance = 0.0;
                            if (stats.weaving) {
                                b.stuck[0] = 0.5;
                                b.stuck[1] = 0.25;
                                b.stuck[2] = 0.5;
                            } else {
                                b.stuck[0] = 0.25;
                                b.stuck[1] = static_cast<double>(0.05f);
                                b.stuck[2] = 0.25;
                            }
                            break;
                        case Special::SweetBerryBush:
                            b.fall_distance = 0.0;
                            b.stuck[0] = static_cast<double>(0.8f);
                            b.stuck[1] = 0.75;
                            b.stuck[2] = static_cast<double>(0.8f);
                            break;
                        case Special::PowderSnow:
                            if (level.block(block_x(), block_y(), block_z()).special ==
                                Special::PowderSnow) {
                                b.fall_distance = 0.0;
                                b.stuck[0] = static_cast<double>(0.9f);
                                b.stuck[1] = 1.5;
                                b.stuck[2] = static_cast<double>(0.9f);
                            }
                            powder = true;
                            break;
                        case Special::BubbleColumn: {
                            if (b.flying) break;
                            if (!long_move && !at_to.intersects(x, y, z, x + 1.0, y + 1.0, z + 1.0)) break;
                            const bool drag = level.shapes.property(s, "drag") == "true";
                            const auto& above = level.info(x, y + 1, z);
                            const bool open = level.shapes.empty(above.collision) &&
                                              above.fluid == Fluid::None;
                            if (open) {
                                b.vy = drag ? std::max(-0.9, b.vy - 0.03) : std::min(1.8, b.vy + 0.1);
                            } else {
                                b.vy = drag ? std::max(-0.3, b.vy - 0.03) : std::min(0.7, b.vy + 0.06);
                                b.fall_distance = 0.0;
                            }
                            break;
                        }
                        case Special::Honey: honey_slide(x, y, z); break;
                        default: break;
                    }
                }
            }
        }
        }
        if (powder) b.in_powder_snow = true;
    }

    // `HoneyBlock.isSlidingDown`, `HoneyBlock.doSlideMovement`: sliding down a honey wall.
    void honey_slide(int32_t x, int32_t y, int32_t z) {
        if (b.on_ground) return;
        if (b.y > y + 0.9375 - 1.0e-7) return;
        const double old_vy = b.vy / static_cast<double>(0.98f) + 0.08;
        if (old_vy >= -0.08) return;
        const double gx = std::abs(x + 0.5 - b.x);
        const double gz = std::abs(z + 0.5 - b.z);
        const double reach = 0.4375 + static_cast<double>(kPlayerWidth / 2.0f);
        if (!(gx + 1.0e-7 > reach || gz + 1.0e-7 > reach)) return;
        const double slide = (-0.05 - 0.08) * static_cast<double>(0.98f);
        if (old_vy < -0.13) {
            const double m = -0.05 / old_vy;
            b.vx *= m;
            b.vz *= m;
        }
        b.vy = slide;
        b.fall_distance = 0.0;
    }

    Vec movement_from;
    Vec movement_wanted;
    bool moved = false;
};

// `KeyboardInput.calculateImpulse`, `Vec2.normalized`: the keys as a unit move vector.
// The keys as a direction: left and forward positive, a unit vector.
void move_vector(const Keys& k, float& mx, float& mz) {
    const float forward = k.forward == k.backward ? 0.0f : (k.forward ? 1.0f : -1.0f);
    const float left = k.left == k.right ? 0.0f : (k.left ? 1.0f : -1.0f);
    const float length = std::sqrt(left * left + forward * forward);
    if (length < 1.0e-4f) {
        mx = mz = 0.0f;
        return;
    }
    mx = left / length;
    mz = forward / length;
}

} // namespace

double collide_with_block(const BlockShapes& shapes, int32_t state, int32_t x, int32_t y,
                          int32_t z, int axis, const Aabb& box, double distance) {
    const auto empty = [](int32_t, int32_t, int32_t) { return 0; };
    const BlockAt at = empty;
    const Level level(World{at, shapes, nullptr, -64});
    Placed p;
    Context above;
    above.bottom = std::numeric_limits<double>::max();
    if (!collision_shape(level, above, x, y, z, state, p)) return distance;
    const Box b{{box.min[0], box.min[1], box.min[2]}, {box.max[0], box.max[1], box.max[2]}};
    if (std::abs(distance) < kEpsilon) return 0.0;
    return collide_one(level, p, axis, b, distance);
}

// `Entity.getInputVector`: input scaled and turned by the yaw.
void input_vector(double ix, double iy, double iz, float speed, float yaw, double out[3]) {
    out[0] = out[1] = out[2] = 0.0;
    const double length2 = ix * ix + iy * iy + iz * iz;
    if (length2 < 1.0e-7) return;
    Vec v{ix, iy, iz};
    if (length2 > 1.0) v = v.normalize();
    v = v.scale(speed);
    const float angle = yaw * kRadians;
    const float s = jmath::sin(angle);
    const float c = jmath::cos(angle);
    out[0] = v.x * c - v.z * s;
    out[1] = v.y;
    out[2] = v.z * c + v.x * s;
}

bool BlockTags::has(const BlockInfo& block, const BlockShapes& shapes, int32_t state,
                    uint8_t bit) const {
    const size_t index = shapes.state(state).block;
    (void)block;
    return index < bits.size() && (bits[index] & bit) != 0;
}

float Body::height() const { return pose_height(pose); }

float Body::eye_height() const { return pose_eye(pose); }

float Body::eye_height(Pose p) const { return pose_eye(p); }

double Body::speed(const Stats& stats, bool sprinting) {
    return sprinting ? stats.movement_speed * (1.0 + 0.3) : stats.movement_speed;
}

void place(Body& body, double x, double y, double z) {
    body.x = x;
    body.y = y;
    body.z = z;
}

bool fits(const Body& body, Pose pose, const World& world) {
    const Level level(world);
    Context c;
    c.bottom = body.y;
    c.fall_distance = body.fall_distance;
    return no_collision(level, c, box_at(body.x, body.y, body.z, pose).deflate(kEpsilon));
}

void tick(Body& b, const Keys& keys, const Stats& stats, const World& world) {
    const Level level(world);
    Mover m(b, keys, stats, level);
    b.flying_changed = false;
    if (!stats.may_fly && b.flying) b.flying = false;

    b.xo = b.x;
    b.yo = b.y;
    b.zo = b.z;
    b.walk_dist_o = b.walk_dist;
    if (stats.spectator) b.on_ground = false;
    b.underwater = b.eye_in_water;

    // `Entity.baseTick`: fluids, eyes and swimming.
    // The entity's base tick.
    b.was_in_powder_snow = b.in_powder_snow;
    b.in_powder_snow = false;
    m.fluids();
    m.fluid_on_eyes();
    m.update_swimming();
    if (m.in_lava()) b.fall_distance *= 0.5;
    b.first_tick = false;

    // `LocalPlayer.aiStep`: crouching, sprinting and flight from the keys.
    // The local player's own step: crouching from last tick's keys, then
    // this tick's keys, sprinting, flight, and the push out of walls.
    if (b.sprint_trigger_time > 0) --b.sprint_trigger_time;
    const Keys was = b.keys;
    const bool had_forward = b.move_z > 1.0e-5f;
    b.crouching = !b.flying && !m.swimming() && m.fits(Pose::Crouching) &&
                  (was.shift || !m.fits(Pose::Standing));
    b.keys = keys;
    move_vector(keys, b.move_x, b.move_z);
    const bool forward = b.move_z > 1.0e-5f;

    if (!stats.spectator) {
        const double reach = static_cast<double>(kPlayerWidth) * 0.35;
        m.move_towards_closest_space(b.x - reach, b.z + reach);
        m.move_towards_closest_space(b.x - reach, b.z - reach);
        m.move_towards_closest_space(b.x + reach, b.z - reach);
        m.move_towards_closest_space(b.x + reach, b.z + reach);
    }

    if (was.shift || stats.using_item || keys.backward) b.sprint_trigger_time = 0;
    const bool underwater = b.underwater;
    const bool shallow = b.touching_water && !underwater;
    const auto sprint_possible = [&](bool flying) {
        return !stats.blind && (stats.food > 6 || stats.may_fly) && (flying || !shallow);
    };
    const bool crawling = b.pose == Pose::Swimming && !b.touching_water;
    const bool moving_slowly = b.crouching || crawling;
    if (!b.sprinting && forward && sprint_possible(b.flying) && !stats.using_item &&
        (!moving_slowly || underwater)) {
        if (!had_forward) {
            if (b.sprint_trigger_time > 0) b.sprinting = true;
            else b.sprint_trigger_time = stats.sprint_window;
        }
        if (keys.sprint) b.sprinting = true;
    }
    if (b.sprinting) {
        if (m.swimming()) {
            if (!sprint_possible(true) || !b.touching_water ||
                (!forward && !b.on_ground && !keys.shift)) {
                b.sprinting = false;
            }
        } else if (!sprint_possible(b.flying) || !forward ||
                   (b.horizontal_collision && !b.minor_horizontal_collision)) {
            b.sprinting = false;
        }
    }

    if (stats.may_fly) {
        if (stats.spectator) {
            if (!b.flying) {
                b.flying = true;
                b.flying_changed = true;
            }
        } else if (!was.jump && keys.jump) {
            if (b.jump_trigger_time == 0) {
                b.jump_trigger_time = 7;
            } else if (!m.swimming()) {
                b.flying = !b.flying;
                if (b.flying && b.on_ground) m.jump_from_ground();
                b.flying_changed = true;
                b.jump_trigger_time = 0;
            }
        }
    }

    if (b.touching_water && keys.shift && !b.flying) b.vy += static_cast<double>(-0.04f);
    if (b.flying) {
        const int rise = (keys.jump ? 1 : 0) - (keys.shift ? 1 : 0);
        if (rise != 0) b.vy += static_cast<float>(rise) * stats.flying_speed * 3.0f;
    }

    // `AbstractClientPlayer.updateBob`: the camera bob from speed.
    // The bob the camera draws comes from the speed the body carried in.
    b.bob_o = b.bob;
    const float bob = b.on_ground && !m.swimming()
                          ? std::min(0.1f, static_cast<float>(std::sqrt(b.vx * b.vx + b.vz * b.vz)))
                          : 0.0f;
    b.bob += (bob - b.bob) * 0.4f;

    if (b.jump_trigger_time > 0) --b.jump_trigger_time;
    if (b.flying) b.fall_distance = 0.0;

    // `LivingEntity.aiStep`: jumping and travel.
    // The living entity's step.
    if (b.no_jump_delay > 0) --b.no_jump_delay;
    if (b.vx * b.vx + b.vz * b.vz < 9.0e-6) b.vx = b.vz = 0.0;
    if (std::abs(b.vy) < 0.003) b.vy = 0.0;

    // `LocalPlayer.modifyInput`, `LocalPlayer.modifyInputSpeedForSquareMovement`: the move vector slowed and squared.
    float ix = b.move_x;
    float iz = b.move_z;
    if (ix * ix + iz * iz != 0.0f) {
        ix *= 0.98f;
        iz *= 0.98f;
        if (stats.using_item) {
            ix *= stats.item_use_speed;
            iz *= stats.item_use_speed;
        }
        if (moving_slowly) {
            const auto sneak = static_cast<float>(stats.sneaking_speed);
            ix *= sneak;
            iz *= sneak;
        }
        const float length = std::sqrt(ix * ix + iz * iz);
        if (length > 0.0f) {
            const float nx = ix * (1.0f / length);
            const float nz = iz * (1.0f / length);
            const float ax = std::abs(nx);
            const float az = std::abs(nz);
            const float ratio = az > ax ? ax / az : az / ax;
            const float to_square = std::sqrt(1.0f + ratio * ratio);
            const float scale = std::min(length * to_square, 1.0f);
            ix = nx * scale;
            iz = nz * scale;
        }
    }
    b.xxa = ix;
    b.zza = iz;
    b.jumping = keys.jump;

    if (b.jumping && !b.flying) {
        const double h = m.in_lava() ? b.lava_height : b.water_height;
        const bool in_water = b.touching_water && h > 0.0;
        const double threshold = m.fluid_jump_threshold();
        if (!in_water || (b.on_ground && !(h > threshold))) {
            if (!m.in_lava() || (b.on_ground && !(h > threshold))) {
                if ((b.on_ground || (in_water && h <= threshold)) && b.no_jump_delay == 0) {
                    m.jump_from_ground();
                    b.no_jump_delay = 10;
                }
            } else {
                b.vy += static_cast<double>(0.04f);
            }
        } else {
            b.vy += static_cast<double>(0.04f);
        }
    } else {
        b.no_jump_delay = 0;
    }

    if (stats.slow_falling || stats.levitation > 0) b.fall_distance = 0.0;
    m.travel(b.xxa, b.zza);
    m.inside_blocks();

    if (b.on_ground && b.flying && !stats.spectator) {
        b.flying = false;
        b.flying_changed = true;
    }

    b.x = std::clamp(b.x, -2.9999999e7, 2.9999999e7);
    b.z = std::clamp(b.z, -2.9999999e7, 2.9999999e7);

    // `Player.updatePlayerPose`: the pose that fits.
    // The pose follows the move: crouching, or the next one down that fits.
    if (m.fits(Pose::Swimming)) {
        const Pose wanted = m.swimming() ? Pose::Swimming
                                         : (keys.shift && !b.flying ? Pose::Crouching : Pose::Standing);
        if (stats.spectator || m.fits(wanted)) b.pose = wanted;
        else if (m.fits(Pose::Crouching)) b.pose = Pose::Crouching;
        else b.pose = Pose::Swimming;
    }
}

struct BoxMover::Impl {
    explicit Impl(const World& world) : level(world) {}
    Level level;
    std::vector<Placed> shapes;
};

BoxMover::BoxMover(const World& world) : impl_(std::make_unique<Impl>(world)) {}
BoxMover::~BoxMover() = default;

// `Entity.collideBoundingBox`, `Entity.collectColliders`: a box moved against the blocks, nobody asking.
void BoxMover::collide(const Aabb& box, double move[3]) {
    const Context nobody{false, -std::numeric_limits<double>::max(), 0.0, false};
    const Box b{{box.min[0], box.min[1], box.min[2]}, {box.max[0], box.max[1], box.max[2]}};
    const Vec m{move[0], move[1], move[2]};
    collect(impl_->level, nobody, b.expand_towards(m.x, m.y, m.z), impl_->shapes);
    const Vec out = collide_with_shapes(impl_->level, m, b, impl_->shapes);
    move[0] = out.x;
    move[1] = out.y;
    move[2] = out.z;
}

} // namespace mcpp::world
