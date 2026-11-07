#include "client/player_rules.hpp"

#include "mcpp/jmath.hpp"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace client {
namespace {

constexpr float kRadians = 0.017453292f;

// `Mth.floor`: rounds a double down to an int.
int32_t floor_i(double v) {
    const auto i = static_cast<int32_t>(v);
    return v < static_cast<double>(i) ? i - 1 : i;
}

// `AttributeInstance.calculateValue`: an attribute's value from its modifiers.
// An attribute's value as the game computes it, leaving out one modifier.
double value_without(std::string_view name, const mcpp::Attribute& a, std::string_view skip) {
    double sum = a.base;
    for (const auto& m : a.modifiers) {
        if (m.operation == 0 && m.id != skip) sum += m.amount;
    }
    double result = sum;
    for (const auto& m : a.modifiers) {
        if (m.operation == 1 && m.id != skip) result += sum * m.amount;
    }
    for (const auto& m : a.modifiers) {
        if (m.operation == 2 && m.id != skip) result *= 1.0 + m.amount;
    }
    return mcpp::sanitize_attribute(name, result);
}

// `Direction.getApproximateNearest`: the face a vector points at most.
int direction_of(float x, float y, float z) {
    static constexpr int kNormals[6][3] = {{0, -1, 0}, {0, 1, 0}, {0, 0, -1},
                                           {0, 0, 1},  {-1, 0, 0}, {1, 0, 0}};
    int best = 2;
    float most = 1.4e-45f;
    for (int i = 0; i < 6; ++i) {
        const float d = x * static_cast<float>(kNormals[i][0]) +
                        y * static_cast<float>(kNormals[i][1]) +
                        z * static_cast<float>(kNormals[i][2]);
        if (d > most) {
            most = d;
            best = i;
        }
    }
    return best;
}

int opposite(int face) { return face ^ 1; }

// `AABB.clip`, `AABB.getDirection`, `AABB.clipPoint`: where a segment enters a set of boxes.
// The nearest face of any of the boxes a segment enters, as the game's box
// clip finds it; t is the fraction of the segment, updated when closer.
bool clip_boxes(std::span<const mcpp::world::Aabb> boxes, const double from[3], const double to[3],
                const int32_t pos[3], double& t, int& face) {
    const double d[3] = {to[0] - from[0], to[1] - from[1], to[2] - from[2]};
    bool found = false;
    const auto clip_point = [&](int axis, double plane, int dir, const double lo[3],
                                const double hi[3]) {
        const int a = (axis + 1) % 3;
        const int b = (axis + 2) % 3;
        const double s = (plane - from[axis]) / d[axis];
        const double pa = from[a] + s * d[a];
        const double pb = from[b] + s * d[b];
        if (0.0 < s && s < t && lo[a] - 1.0e-7 < pa && pa < hi[a] + 1.0e-7 &&
            lo[b] - 1.0e-7 < pb && pb < hi[b] + 1.0e-7) {
            t = s;
            face = dir;
            found = true;
        }
    };
    for (const auto& box : boxes) {
        const double lo[3] = {box.min[0] + pos[0], box.min[1] + pos[1], box.min[2] + pos[2]};
        const double hi[3] = {box.max[0] + pos[0], box.max[1] + pos[1], box.max[2] + pos[2]};
        // x, then y, then z, each from the face the segment comes in by.
        static constexpr int kLow[3] = {4, 0, 2};
        static constexpr int kHigh[3] = {5, 1, 3};
        for (const int axis : {0, 1, 2}) {
            if (d[axis] > 1.0e-7) clip_point(axis, lo[axis], kLow[axis], lo, hi);
            else if (d[axis] < -1.0e-7) clip_point(axis, hi[axis], kHigh[axis], lo, hi);
        }
    }
    return found;
}

// `VoxelShape.clip`: a segment against one block's shape.
// One block's outline, clipped the way the game clips it: a start inside
// the shape hits at once, on the face against the ray.
bool clip_shape(std::span<const mcpp::world::Aabb> boxes, const double from[3], const double to[3],
                const int32_t pos[3], Hit& hit) {
    if (boxes.empty()) return false;
    const double d[3] = {to[0] - from[0], to[1] - from[1], to[2] - from[2]};
    if (d[0] * d[0] + d[1] * d[1] + d[2] * d[2] < 1.0e-7) return false;
    const double p[3] = {from[0] + d[0] * 0.001, from[1] + d[1] * 0.001, from[2] + d[2] * 0.001};
    for (const auto& box : boxes) {
        bool inside = true;
        for (int i = 0; i < 3; ++i) {
            const double v = p[i] - pos[i];
            inside = inside && v >= box.min[i] && v < box.max[i];
        }
        if (inside) {
            hit.hit = true;
            hit.inside = true;
            std::copy(p, p + 3, hit.location);
            std::copy(pos, pos + 3, hit.pos);
            hit.face = opposite(direction_of(static_cast<float>(d[0]), static_cast<float>(d[1]),
                                             static_cast<float>(d[2])));
            return true;
        }
    }
    double t = 1.0;
    int face = -1;
    if (!clip_boxes(boxes, from, to, pos, t, face)) return false;
    hit.hit = true;
    hit.inside = false;
    hit.face = face;
    for (int i = 0; i < 3; ++i) hit.location[i] = from[i] + t * d[i];
    std::copy(pos, pos + 3, hit.pos);
    return true;
}

} // namespace

mcpp::world::Stats player_stats(const mcpp::Session& session) {
    mcpp::world::Stats s;
    const int32_t me = session.entity_id();
    const auto& attributes = session.attributes(me);
    if (const auto found = attributes.find("minecraft:movement_speed"); found != attributes.end()) {
        s.movement_speed =
            value_without("minecraft:movement_speed", found->second, "minecraft:sprinting");
    } else {
        s.movement_speed = session.abilities().walking_speed;
    }
    s.jump_strength = session.attribute(me, "minecraft:jump_strength", 0.42);
    s.step_height = static_cast<float>(session.attribute(me, "minecraft:step_height", 0.6));
    s.gravity = session.attribute(me, "minecraft:gravity", 0.08);
    s.sneaking_speed = session.attribute(me, "minecraft:sneaking_speed", 0.3);
    s.water_movement_efficiency =
        session.attribute(me, "minecraft:water_movement_efficiency", 0.0);
    s.movement_efficiency = session.attribute(me, "minecraft:movement_efficiency", 0.0);
    s.jump_boost = session.effect_level("minecraft:jump_boost");
    s.levitation = session.effect_level("minecraft:levitation");
    s.slow_falling = session.effect_level("minecraft:slow_falling") > 0;
    s.dolphins_grace = session.effect_level("minecraft:dolphins_grace") > 0;
    s.blind = session.effect_level("minecraft:blindness") > 0;
    s.weaving = session.effect_level("minecraft:weaving") > 0;
    const auto& abilities = session.abilities();
    s.may_fly = abilities.may_fly;
    s.flying_speed = abilities.flying_speed;
    s.spectator = session.vitals().game_mode == 3;
    s.food = session.vitals().food;
    s.fast_lava = session.dimension().type == "minecraft:the_nether";
    return s;
}

void player_tags(const mcpp::Session& session, const mcpp::world::BlockShapes& shapes,
                 mcpp::world::BlockTags& out) {
    using mcpp::world::BlockTags;
    out.bits.assign(shapes.size(), 0);
    // Registry id to the shape table's block index, through any state.
    std::vector<int32_t> by_registry;
    for (size_t state = 0; state < shapes.size();) {
        const auto& block = shapes.block(static_cast<int32_t>(state));
        const auto id = static_cast<size_t>(block.registry_id);
        if (by_registry.size() <= id) by_registry.resize(id + 1, -1);
        by_registry[id] = shapes.state(static_cast<int32_t>(state)).block;
        state += static_cast<size_t>(std::max(1, block.count));
        if (block.name.ends_with("_fence_gate")) {
            out.bits[by_registry[id]] |= BlockTags::kFenceGate;
        }
    }
    const auto mark = [&](std::string_view tag, uint8_t bit) {
        const auto* ids = session.tags().find("minecraft:block", tag);
        if (ids == nullptr) return;
        for (const int32_t id : *ids) {
            if (id >= 0 && static_cast<size_t>(id) < by_registry.size() && by_registry[id] >= 0) {
                out.bits[static_cast<size_t>(by_registry[id])] |= bit;
            }
        }
    };
    mark("minecraft:climbable", BlockTags::kClimbable);
    mark("minecraft:fences", BlockTags::kFences);
    mark("minecraft:walls", BlockTags::kWalls);
}

// `PositionMoveRotation.calculateAbsolute`, `ClientPacketListener.setValuesFromPositionPacket`: a teleport, relative parts kept.
void apply_teleport(mcpp::world::Body& body, const mcpp::SessionEvents::Player::Teleport& t) {
    const uint32_t r = t.relative;
    const auto pick = [r](uint32_t bit, double was) { return (r & bit) != 0 ? was : 0.0; };
    const double x = pick(0x01, body.x) + t.x;
    const double y = pick(0x02, body.y) + t.y;
    const double z = pick(0x04, body.z) + t.z;
    const float yaw = ((r & 0x08) != 0 ? body.yaw : 0.0f) + t.yaw;
    const float pitch = std::clamp(((r & 0x10) != 0 ? body.pitch : 0.0f) + t.pitch, -90.0f, 90.0f);
    double v[3] = {body.vx, body.vy, body.vz};
    if ((r & 0x100) != 0) {
        const auto x_turn = static_cast<float>((body.pitch - pitch) * (3.141592653589793 / 180.0));
        const auto y_turn = static_cast<float>((body.yaw - yaw) * (3.141592653589793 / 180.0));
        float c = mcpp::jmath::cos(x_turn);
        float s = mcpp::jmath::sin(x_turn);
        const double vy = v[1] * c + v[2] * s;
        const double vz = v[2] * c - v[1] * s;
        v[1] = vy;
        v[2] = vz;
        c = mcpp::jmath::cos(y_turn);
        s = mcpp::jmath::sin(y_turn);
        const double vx = v[0] * c + v[2] * s;
        v[2] = v[2] * c - v[0] * s;
        v[0] = vx;
    }
    body.vx = (r & 0x20) != 0 ? v[0] + t.vx : t.vx;
    body.vy = (r & 0x40) != 0 ? v[1] + t.vy : t.vy;
    body.vz = (r & 0x80) != 0 ? v[2] + t.vz : t.vz;
    body.xo = pick(0x01, body.xo) + t.x;
    body.yo = pick(0x02, body.yo) + t.y;
    body.zo = pick(0x04, body.zo) + t.z;
    body.x = x;
    body.y = y;
    body.z = z;
    body.yaw = yaw;
    body.pitch = pitch;
}

void send_tick(mcpp::Session& session, const mcpp::world::Body& body) {
    if (body.flying_changed) session.send_flying(body.flying);
    mcpp::Session::Movement m;
    m.x = body.x;
    m.y = body.y;
    m.z = body.z;
    m.yaw = body.yaw;
    m.pitch = body.pitch;
    m.on_ground = body.on_ground;
    m.horizontal_collision = body.horizontal_collision;
    m.sprinting = body.sprinting;
    m.keys = body.keys.bits();
    session.send_movement(m);
    session.send_tick_end();
}

double mouse_scale(double sensitivity) {
    const double d = sensitivity * static_cast<double>(0.6f) + static_cast<double>(0.2f);
    return d * d * d * 8.0;
}

// `Entity.turn`: the mouse turns the view.
void turn(mcpp::world::Body& body, double dx, double dy) {
    const float pitch_change = static_cast<float>(dy) * 0.15f;
    const float yaw_change = static_cast<float>(dx) * 0.15f;
    body.pitch += pitch_change;
    body.yaw += yaw_change;
    body.pitch = std::clamp(body.pitch, -90.0f, 90.0f);
}

// `Entity.getEyePosition`: the eye between two ticks.
void eye_position(const mcpp::world::Body& body, double partial, double out[3]) {
    out[0] = body.xo + partial * (body.x - body.xo);
    out[1] = body.yo + partial * (body.y - body.yo) + static_cast<double>(body.eye_height());
    out[2] = body.zo + partial * (body.z - body.zo);
}

// `Entity.calculateViewVector`: the look direction from pitch and yaw.
void view_vector(float pitch, float yaw, double out[3]) {
    const float p = pitch * kRadians;
    const float y = -yaw * kRadians;
    const float cy = mcpp::jmath::cos(y);
    const float sy = mcpp::jmath::sin(y);
    const float cp = mcpp::jmath::cos(p);
    const float sp = mcpp::jmath::sin(p);
    out[0] = sy * cp;
    out[1] = -sp;
    out[2] = cy * cp;
}

// `BlockGetter.clip`, `BlockGetter.traverseBlocks`, `BlockGetter.clipWithInteractionOverride`: the block a ray hits first.
Hit pick_block(const mcpp::world::BlockShapes& shapes, const mcpp::world::BlockAt& at,
               const double from[3], const double to[3]) {
    Hit hit;
    if (from[0] == to[0] && from[1] == to[1] && from[2] == to[2]) return hit;
    const auto lerp = [](double t, double a, double b) { return a + t * (b - a); };
    const double end[3] = {lerp(-1.0e-7, to[0], from[0]), lerp(-1.0e-7, to[1], from[1]),
                           lerp(-1.0e-7, to[2], from[2])};
    const double start[3] = {lerp(-1.0e-7, from[0], to[0]), lerp(-1.0e-7, from[1], to[1]),
                             lerp(-1.0e-7, from[2], to[2])};
    int32_t cell[3] = {floor_i(start[0]), floor_i(start[1]), floor_i(start[2])};
    const auto visit = [&]() {
        const int32_t state = at(cell[0], cell[1], cell[2]);
        if (state <= 0) return false;
        const auto& info = shapes.state(state);
        const auto boxes = shapes.boxes(info.outline);
        if (boxes.empty()) return false;
        double offset[3];
        shapes.offset(state, cell[0], cell[2], offset);
        const double f[3] = {from[0] - offset[0], from[1] - offset[1], from[2] - offset[2]};
        const double t[3] = {to[0] - offset[0], to[1] - offset[1], to[2] - offset[2]};
        Hit h;
        if (!clip_shape(boxes, f, t, cell, h)) return false;
        for (int i = 0; i < 3; ++i) h.location[i] += offset[i];
        const auto interaction = shapes.boxes(info.interaction);
        Hit over;
        if (clip_shape(interaction, f, t, cell, over)) {
            for (int i = 0; i < 3; ++i) over.location[i] += offset[i];
            const auto dist = [&](const double p[3]) {
                const double dx = p[0] - from[0];
                const double dy = p[1] - from[1];
                const double dz = p[2] - from[2];
                return dx * dx + dy * dy + dz * dz;
            };
            if (dist(over.location) < dist(h.location)) h.face = over.face;
        }
        hit = h;
        return true;
    };
    if (visit()) return hit;
    const double d[3] = {end[0] - start[0], end[1] - start[1], end[2] - start[2]};
    int step[3];
    double delta[3];
    double next[3];
    for (int i = 0; i < 3; ++i) {
        step[i] = d[i] > 0.0 ? 1 : d[i] < 0.0 ? -1 : 0;
        delta[i] = step[i] == 0 ? std::numeric_limits<double>::max() : step[i] / d[i];
        const double frac = start[i] - std::floor(start[i]);
        next[i] = delta[i] * (step[i] > 0 ? 1.0 - frac : frac);
    }
    while (next[0] <= 1.0 || next[1] <= 1.0 || next[2] <= 1.0) {
        int axis;
        if (next[0] < next[1]) axis = next[0] < next[2] ? 0 : 2;
        else axis = next[1] < next[2] ? 1 : 2;
        cell[axis] += step[axis];
        next[axis] += delta[axis];
        if (visit()) return hit;
    }
    return hit;
}

// `BlockBehaviour.getDestroyProgress`, `Player.getDestroySpeed`: how far one tick breaks a block.
float destroy_progress(float hardness, bool needs_tool, const DigContext& dig) {
    if (hardness == -1.0f) return 0.0f;
    float speed = dig.tool_speed;
    if (speed > 1.0f) speed += static_cast<float>(dig.mining_efficiency);
    if (dig.haste > 0 || dig.conduit_power > 0) {
        const int amplifier = std::max(dig.haste, dig.conduit_power) - 1;
        speed *= 1.0f + static_cast<float>(amplifier + 1) * 0.2f;
    }
    if (dig.mining_fatigue > 0) {
        switch (dig.mining_fatigue - 1) {
            case 0: speed *= 0.3f; break;
            case 1: speed *= 0.09f; break;
            case 2: speed *= 0.0027f; break;
            default: speed *= 8.1e-4f; break;
        }
    }
    speed *= static_cast<float>(dig.block_break_speed);
    if (dig.eye_in_water) speed *= static_cast<float>(dig.submerged_mining_speed);
    if (!dig.on_ground) speed /= 5.0f;
    const int divisor = !needs_tool || dig.correct_tool ? 30 : 100;
    return speed / hardness / static_cast<float>(divisor);
}

} // namespace client
