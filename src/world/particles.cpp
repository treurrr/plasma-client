#include "mcpp/world/particles.hpp"

#include "mcpp/protocol_versions.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>

namespace mcpp::world {
namespace {

std::filesystem::path data_directory() {
    if (const char* home = std::getenv("XDG_DATA_HOME"); home != nullptr && *home != '\0') {
        return std::filesystem::path(home) / "plasma-client" / "data";
    }
    const char* home = std::getenv("HOME");
    return std::filesystem::path(home != nullptr ? home : ".") / ".local" / "share" /
           "plasma-client" / "data";
}

// `Mth.ceil`: rounds a double up to an int.
int32_t ceil_i(double v) {
    const auto i = static_cast<int32_t>(v);
    return v > static_cast<double>(i) ? i + 1 : i;
}

// `Mth.floor`: rounds a double down to an int.
int32_t floor_i(double v) {
    const auto i = static_cast<int32_t>(v);
    return v < static_cast<double>(i) ? i - 1 : i;
}

// `ARGB.as8BitChannel`: a channel from 0 to 1 as a byte.
uint32_t channel(float f) { return static_cast<uint32_t>(floor_i(static_cast<double>(f * 255.0f))) & 0xFFu; }

// `RandomSupport.generateUniqueSeed`: the uniquifier stepped, mixed with the clock.
int64_t unique_seed() {
    static uint64_t uniquifier = 8682522807148012ull;
    uniquifier *= 1181783497276652981ull;
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<int64_t>(uniquifier ^
                                static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count()));
}

// `BlockPos.containing`: the block a point is in.
void containing(double x, double y, double z, int32_t out[3]) {
    out[0] = floor_i(x);
    out[1] = floor_i(y);
    out[2] = floor_i(z);
}

} // namespace

std::filesystem::path ParticleTable::default_path(int32_t protocol) {
    return data_directory() / ("particles-" + std::to_string(protocol) + ".txt");
}

std::filesystem::path ParticleTable::cracks_path(int32_t protocol) {
    return data_directory() / ("cracks-" + std::to_string(protocol));
}

bool ParticleTable::open(const std::filesystem::path& path, const BlockTable& table) {
    std::ifstream in(path);
    if (!in) return false;
    std::unordered_map<std::string_view, uint16_t> textures;
    for (uint32_t id = 0; id < 65535; ++id) {
        const auto name = table.texture_name(static_cast<uint16_t>(id));
        if (name.empty()) break;
        textures.emplace(name, static_cast<uint16_t>(id));
    }
    states_.clear();
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream row(line);
        int32_t id = 0;
        int terrain = 0;
        int invisible = 0;
        std::string colour;
        std::string texture;
        if (!(row >> id >> terrain >> invisible >> colour >> texture) || id < 0) continue;
        if (static_cast<size_t>(id) >= states_.size()) states_.resize(static_cast<size_t>(id) + 1);
        State& s = states_[static_cast<size_t>(id)];
        s.terrain = terrain != 0;
        s.invisible = invisible != 0;
        s.below = colour.ends_with("@below");
        const std::string kind = colour.substr(0, colour.find('@'));
        if (kind == "-") s.colour = Colour::None;
        else if (kind == "grass") s.colour = Colour::Grass;
        else if (kind == "foliage") s.colour = Colour::Foliage;
        else if (kind == "dry_foliage") s.colour = Colour::DryFoliage;
        else if (kind == "water") s.colour = Colour::Water;
        else {
            s.colour = Colour::Fixed;
            s.argb = static_cast<uint32_t>(std::stoul(kind, nullptr, 16));
        }
        if (const auto found = textures.find(texture); found != textures.end()) {
            s.texture = found->second;
        }
    }
    for (int32_t state = 0; state < static_cast<int32_t>(table.state_count()); ++state) {
        if (table.block_name(state) != "minecraft:grass_block") continue;
        if (grass_first_ < 0) grass_first_ = state;
        ++grass_count_;
    }
    return !states_.empty();
}

// `Particle.setPower`: the speed scaled, the upward kick kept.
void Particle::set_power(float power) {
    xd *= power;
    yd = (yd - static_cast<double>(0.1f)) * power + static_cast<double>(0.1f);
    zd *= power;
}

// `SingleQuadParticle.scale`, `Particle.scale`: the quad and the box scaled.
void Particle::scale(float by) {
    quad_size *= by;
    set_size(0.2f * by, 0.2f * by);
}

// `Particle.setSize`: a new box size about the same centre and bottom.
void Particle::set_size(float width, float height) {
    if (width != bb_width || height != bb_height) {
        bb_width = width;
        bb_height = height;
        const double cx = (bb.min[0] + bb.max[0] - static_cast<double>(width)) / 2.0;
        const double cz = (bb.min[2] + bb.max[2] - static_cast<double>(width)) / 2.0;
        bb = {{cx, bb.min[1], cz},
              {cx + static_cast<double>(bb_width), bb.min[1] + static_cast<double>(bb_height),
               cz + static_cast<double>(bb_width)}};
    }
}

// `Particle.setPos`: the position and the box around it.
void Particle::set_pos(double px, double py, double pz) {
    x = px;
    y = py;
    z = pz;
    const float half = bb_width / 2.0f;
    const float height = bb_height;
    bb = {{px - static_cast<double>(half), py, pz - static_cast<double>(half)},
          {px + static_cast<double>(half), py + static_cast<double>(height),
           pz + static_cast<double>(half)}};
}

// `Particle.tick`: age, fall, move, slow down.
void Particle::tick(BoxMover& mover) {
    xo = x;
    yo = y;
    zo = z;
    if (age++ >= lifetime) {
        removed = true;
        return;
    }
    yd = yd - 0.04 * static_cast<double>(gravity);
    move(mover, xd, yd, zd);
    if (speed_up_when_y_motion_is_blocked && y == yo) {
        xd *= 1.1;
        zd *= 1.1;
    }
    xd = xd * static_cast<double>(friction);
    yd = yd * static_cast<double>(friction);
    zd = zd * static_cast<double>(friction);
    if (on_ground) {
        xd *= static_cast<double>(0.7f);
        zd *= static_cast<double>(0.7f);
    }
}

// `Particle.move`, `Particle.setLocationFromBoundingbox`: the move against the blocks.
void Particle::move(BoxMover& mover, double dx, double dy, double dz) {
    if (stopped_by_collision) return;
    const double wanted_x = dx;
    const double wanted_y = dy;
    const double wanted_z = dz;
    if (has_physics && (dx != 0.0 || dy != 0.0 || dz != 0.0) && dx * dx + dy * dy + dz * dz < 10000.0) {
        double m[3] = {dx, dy, dz};
        mover.collide(bb, m);
        dx = m[0];
        dy = m[1];
        dz = m[2];
    }
    if (dx != 0.0 || dy != 0.0 || dz != 0.0) {
        bb = {{bb.min[0] + dx, bb.min[1] + dy, bb.min[2] + dz},
              {bb.max[0] + dx, bb.max[1] + dy, bb.max[2] + dz}};
        x = (bb.min[0] + bb.max[0]) / 2.0;
        y = bb.min[1];
        z = (bb.min[2] + bb.max[2]) / 2.0;
    }
    if (std::abs(wanted_y) >= static_cast<double>(1.0e-5f) && std::abs(dy) < static_cast<double>(1.0e-5f)) {
        stopped_by_collision = true;
    }
    on_ground = wanted_y != dy && wanted_y < 0.0;
    if (wanted_x != dx) xd = 0.0;
    if (wanted_z != dz) zd = 0.0;
}

// `TerrainParticle.getU0`: a quarter of the sprite, picked at random.
float Particle::u0() const { return (uo + 1.0f) / 4.0f; }
// `TerrainParticle.getU1`: the other edge of that quarter.
float Particle::u1() const { return uo / 4.0f; }
// `TerrainParticle.getV0`: the top of that quarter.
float Particle::v0() const { return vo / 4.0f; }
// `TerrainParticle.getV1`: the bottom of that quarter.
float Particle::v1() const { return (vo + 1.0f) / 4.0f; }

// `Particle.getLightColor`, `TerrainParticle.getLightColor`: the light where it is, else where it came from.
int32_t Particle::light_colour(const ParticleLevel& level) const {
    int32_t at[3];
    containing(x, y, z, at);
    int32_t light = std::max(0, level.light(at[0], at[1], at[2]));
    if (light == 0 && kind == ParticleKind::Terrain) {
        light = std::max(0, level.light(pos[0], pos[1], pos[2]));
    }
    return light;
}

// `TerrainParticle.TerrainParticle`, `SingleQuadParticle.SingleQuadParticle`, `Particle.Particle`: a bit of a block, thrown.
Particle terrain_particle(const ParticleLevel& level, int64_t seed, double x, double y, double z,
                          double xd, double yd, double zd, int32_t state, const int32_t pos[3]) {
    Particle p;
    p.random = jmath::LegacyRandom(seed);
    p.set_size(0.2f, 0.2f);
    p.set_pos(x, y, z);
    p.xo = x;
    p.yo = y;
    p.zo = z;
    p.lifetime = static_cast<int32_t>(4.0f / (p.random.next_float() * 0.9f + 0.1f));
    p.xd = xd + static_cast<double>((p.random.next_float() * 2.0f - 1.0f) * 0.4f);
    p.yd = yd + static_cast<double>((p.random.next_float() * 2.0f - 1.0f) * 0.4f);
    p.zd = zd + static_cast<double>((p.random.next_float() * 2.0f - 1.0f) * 0.4f);
    const auto speed = static_cast<double>((p.random.next_float() + p.random.next_float() + 1.0f) * 0.15f);
    const double length = std::sqrt(p.xd * p.xd + p.yd * p.yd + p.zd * p.zd);
    p.xd = p.xd / length * speed * static_cast<double>(0.4f);
    p.yd = p.yd / length * speed * static_cast<double>(0.4f) + static_cast<double>(0.1f);
    p.zd = p.zd / length * speed * static_cast<double>(0.4f);
    p.quad_size = 0.1f * (p.random.next_float() * 0.5f + 0.5f) * 2.0f;

    const auto& info = level.table.at(state);
    p.kind = ParticleKind::Terrain;
    p.texture = info.texture;
    std::copy(pos, pos + 3, p.pos);
    p.gravity = 1.0f;
    p.r = 0.6f;
    p.g = 0.6f;
    p.b = 0.6f;
    if (!level.table.grass_block(state)) {
        // `BlockColors.getColor`: tint index 0 of the block at its own position.
        uint32_t colour = 0xFFFFFFFFu;
        if (info.colour == ParticleTable::Colour::Fixed) {
            colour = info.argb;
        } else if (info.colour != ParticleTable::Colour::None) {
            colour = 0xFF000000u | level.biome(info.colour, pos[0], pos[1] - (info.below ? 1 : 0), pos[2]);
        }
        p.r *= static_cast<float>(colour >> 16 & 0xFFu) / 255.0f;
        p.g *= static_cast<float>(colour >> 8 & 0xFFu) / 255.0f;
        p.b *= static_cast<float>(colour & 0xFFu) / 255.0f;
    }
    p.quad_size /= 2.0f;
    p.uo = p.random.next_float() * 3.0f;
    p.vo = p.random.next_float() * 3.0f;
    p.layer = QuadLayer::Terrain;
    return p;
}

// `Camera.setRotation`, `Quaternionf.rotationYXZ`: the camera's turn.
void camera_rotation(float yaw, float pitch, float out[4]) {
    const float angle_y = 3.1415927f - yaw * 0.017453292f;
    const float angle_x = -pitch * 0.017453292f;
    const auto sx = static_cast<float>(std::sin(static_cast<double>(angle_x * 0.5f)));
    const auto cx = static_cast<float>(std::cos(static_cast<double>(angle_x * 0.5f)));
    const auto sy = static_cast<float>(std::sin(static_cast<double>(angle_y * 0.5f)));
    const auto cy = static_cast<float>(std::cos(static_cast<double>(angle_y * 0.5f)));
    out[0] = cy * sx;
    out[1] = sy * cx;
    out[2] = -sy * sx;
    out[3] = cy * cx;
}

ParticleEngine::ParticleEngine() : next_seed(unique_seed), level_random(unique_seed()) {}

// `EvictingQueue.add`: in at the back, the oldest out when full.
void ParticleEngine::Group::add(const Particle& particle) {
    if (ring.empty()) ring.resize(kGroupLimit);
    if (size == kGroupLimit) {
        head = (head + 1) % kGroupLimit;
        --size;
    }
    ring[(head + size) % kGroupLimit] = particle;
    ++size;
}

// `ParticleGroup.tickParticles`: each one ticked in order, the dead let go.
void ParticleEngine::Group::tick(BoxMover& mover) {
    size_t kept = 0;
    for (size_t i = 0; i < size; ++i) {
        Particle& p = ring[(head + i) % kGroupLimit];
        p.tick(mover);
        if (!p.alive()) continue;
        if (kept != i) ring[(head + kept) % kGroupLimit] = p;
        ++kept;
    }
    size = kept;
}

// `ParticleEngine.add`: queued until the next tick.
void ParticleEngine::add(const Particle& particle) { to_add_.push_back(particle); }

// `ParticleEngine.tick`: every group ticked, then the new ones put in.
void ParticleEngine::tick(const ParticleLevel& level) {
    if (quads_.size > 0) {
        BoxMover mover(World{level.at, level.shapes, nullptr, level.min_y});
        quads_.tick(mover);
    }
    for (const auto& p : to_add_) quads_.add(p);
    to_add_.clear();
}

// `ParticleEngine.clearParticles`: everything gone.
void ParticleEngine::clear() {
    quads_.head = 0;
    quads_.size = 0;
    to_add_.clear();
}

// `ParticleEngine.countParticles`: how many are alive.
size_t ParticleEngine::count() const { return quads_.size; }

// `ParticleEngine.extract`, `QuadParticleGroup.extractRenderState`, `SingleQuadParticle.extract`: the quads in view.
void ParticleEngine::extract(const ParticleView& view, const ParticleLevel& level,
                             std::vector<ParticleQuad>& out) const {
    out.clear();
    const auto inside = [&](const Particle& p) {
        const auto rx = static_cast<float>(p.x - view.apex[0]);
        const auto ry = static_cast<float>(p.y - view.apex[1]);
        const auto rz = static_cast<float>(p.z - view.apex[2]);
        for (const auto& plane : view.planes) {
            if (plane[0] * rx + plane[1] * ry + plane[2] * rz + plane[3] < 0.0f) return false;
        }
        return true;
    };
    const auto lerp = [&](double from, double to) {
        return from + static_cast<double>(view.partial) * (to - from);
    };
    for (size_t i = 0; i < quads_.size; ++i) {
        const Particle& p = quads_.ring[(quads_.head + i) % kGroupLimit];
        if (!inside(p)) continue;
        ParticleQuad q{};
        q.x = static_cast<float>(lerp(p.xo, p.x) - view.camera[0]);
        q.y = static_cast<float>(lerp(p.yo, p.y) - view.camera[1]);
        q.z = static_cast<float>(lerp(p.zo, p.z) - view.camera[2]);
        std::copy(view.rotation, view.rotation + 4, q.rotation);
        if (p.roll != 0.0f) {
            // `Quaternionf.rotateZ`: the particle's own roll on top of the camera's turn.
            const float angle = p.o_roll + view.partial * (p.roll - p.o_roll);
            const auto s = static_cast<float>(std::sin(static_cast<double>(angle * 0.5f)));
            const auto c = static_cast<float>(std::cos(static_cast<double>(angle * 0.5f)));
            const float* r = view.rotation;
            q.rotation[0] = r[0] * c + r[1] * s;
            q.rotation[1] = r[1] * c - r[0] * s;
            q.rotation[2] = r[3] * s + r[2] * c;
            q.rotation[3] = r[3] * c - r[2] * s;
        }
        q.size = p.quad_size;
        q.u0 = p.u0();
        q.u1 = p.u1();
        q.v0 = p.v0();
        q.v1 = p.v1();
        q.colour = channel(p.alpha) << 24 | channel(p.r) << 16 | channel(p.g) << 8 | channel(p.b);
        q.light = static_cast<uint32_t>(p.light_colour(level));
        q.texture = p.texture;
        q.layer = static_cast<uint32_t>(p.layer);
        out.push_back(q);
    }
    std::stable_partition(out.begin(), out.end(), [](const ParticleQuad& q) {
        return q.layer == static_cast<uint32_t>(QuadLayer::Opaque);
    });
}

// `ClientLevel.addDestroyBlockEffect`: every box of the shape filled with bits flying out of its centre.
void ParticleEngine::destroy(const ParticleLevel& level, const int32_t pos[3], int32_t state) {
    const auto& info = level.shapes.state(state);
    if ((info.flags & kAir) != 0 || !level.table.at(state).terrain) return;
    double offset[3] = {0.0, 0.0, 0.0};
    if (level.shapes.block(state).offset != 0) level.shapes.offset(state, pos[0], pos[2], offset);
    for (const auto& box : level.shapes.boxes(info.outline)) {
        const double x0 = box.min[0] + offset[0];
        const double y0 = box.min[1] + offset[1];
        const double z0 = box.min[2] + offset[2];
        const double dx = std::min(1.0, (box.max[0] + offset[0]) - x0);
        const double dy = std::min(1.0, (box.max[1] + offset[1]) - y0);
        const double dz = std::min(1.0, (box.max[2] + offset[2]) - z0);
        const int32_t nx = std::max(2, ceil_i(dx / 0.25));
        const int32_t ny = std::max(2, ceil_i(dy / 0.25));
        const int32_t nz = std::max(2, ceil_i(dz / 0.25));
        for (int32_t i = 0; i < nx; ++i) {
            for (int32_t j = 0; j < ny; ++j) {
                for (int32_t k = 0; k < nz; ++k) {
                    const double fx = (i + 0.5) / nx;
                    const double fy = (j + 0.5) / ny;
                    const double fz = (k + 0.5) / nz;
                    const double x = fx * dx + x0;
                    const double y = fy * dy + y0;
                    const double z = fz * dz + z0;
                    add(terrain_particle(level, next_seed(), pos[0] + x, pos[1] + y, pos[2] + z,
                                         fx - 0.5, fy - 0.5, fz - 0.5, state, pos));
                }
            }
        }
    }
}

// `ClientLevel.addBreakingBlockEffect`: one slow bit off the face being dug.
void ParticleEngine::crack(const ParticleLevel& level, const int32_t pos[3], int face) {
    const int32_t state = std::max(0, level.at(pos[0], pos[1], pos[2]));
    const auto& particles = level.table.at(state);
    if (particles.invisible || !particles.terrain) return;
    const auto& info = level.shapes.state(state);
    const auto boxes = level.shapes.boxes(info.outline);
    if (boxes.empty()) return;
    // `VoxelShape.bounds`: the box around all of the shape.
    Aabb bounds = boxes[0];
    for (const auto& box : boxes) {
        for (int a = 0; a < 3; ++a) {
            bounds.min[a] = std::min(bounds.min[a], box.min[a]);
            bounds.max[a] = std::max(bounds.max[a], box.max[a]);
        }
    }
    double offset[3] = {0.0, 0.0, 0.0};
    if (level.shapes.block(state).offset != 0) level.shapes.offset(state, pos[0], pos[2], offset);
    for (int a = 0; a < 3; ++a) {
        bounds.min[a] += offset[a];
        bounds.max[a] += offset[a];
    }
    const auto edge = static_cast<double>(0.1f);
    const auto inset = static_cast<double>(0.2f);
    double x = pos[0] + level_random.next_double() * (bounds.max[0] - bounds.min[0] - inset) + edge + bounds.min[0];
    double y = pos[1] + level_random.next_double() * (bounds.max[1] - bounds.min[1] - inset) + edge + bounds.min[1];
    double z = pos[2] + level_random.next_double() * (bounds.max[2] - bounds.min[2] - inset) + edge + bounds.min[2];
    if (face == 0) y = pos[1] + bounds.min[1] - edge;
    if (face == 1) y = pos[1] + bounds.max[1] + edge;
    if (face == 2) z = pos[2] + bounds.min[2] - edge;
    if (face == 3) z = pos[2] + bounds.max[2] + edge;
    if (face == 4) x = pos[0] + bounds.min[0] - edge;
    if (face == 5) x = pos[0] + bounds.max[0] + edge;
    Particle p = terrain_particle(level, next_seed(), x, y, z, 0.0, 0.0, 0.0, state, pos);
    p.set_power(0.2f);
    p.scale(0.6f);
    add(p);
}

} // namespace mcpp::world
