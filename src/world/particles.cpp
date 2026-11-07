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

std::filesystem::path ParticleSprites::default_path(int32_t protocol) {
    return data_directory() / ("particle_sprites-" + std::to_string(protocol) + ".txt");
}

bool ParticleSprites::open(const std::filesystem::path& path, uint32_t blocks) {
    std::ifstream in(path);
    if (!in) return false;
    sets_.clear();
    names_.clear();
    uint32_t textures = 0;
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream row(line);
        std::string kind;
        if (!(row >> kind)) continue;
        if (kind == "T") {
            ++textures;
            continue;
        }
        if (kind != "S") continue;
        std::string name;
        if (!(row >> name)) continue;
        std::vector<uint16_t> layers;
        int32_t index = 0;
        while (row >> index) {
            if (index < 0 || static_cast<uint32_t>(index) >= textures) continue;
            layers.push_back(static_cast<uint16_t>(blocks + static_cast<uint32_t>(index)));
        }
        if (layers.empty()) continue;
        names_.emplace(name, static_cast<int32_t>(sets_.size()));
        sets_.push_back(std::move(layers));
    }
    return !sets_.empty();
}

int32_t ParticleSprites::find(std::string_view name) const {
    const auto found = names_.find(name);
    return found == names_.end() ? -1 : found->second;
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
void Particle::base_tick(BoxMover& mover) {
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

// `SingleQuadParticle.setColor`: the three channels at once.
void Particle::set_colour(float red, float green, float blue) {
    r = red;
    g = green;
    b = blue;
}

// `SingleQuadParticle.setSpriteFromAge`: the sprite the age has reached.
void Particle::sprite_from_age(const ParticleSprites& sprites) {
    if (!removed && sprite_set >= 0) texture = sprites.at_age(sprite_set, age, lifetime);
}

// `Particle.move`, `Particle.setLocationFromBoundingbox`: the move against the
// blocks, and `FlameParticle.move`, `PortalParticle.move`, which pass through.
void Particle::move(BoxMover& mover, double dx, double dy, double dz) {
    if (kind == ParticleKind::Rising || kind == ParticleKind::Portal ||
        kind == ParticleKind::ReversePortal) {
        bb = {{bb.min[0] + dx, bb.min[1] + dy, bb.min[2] + dz},
              {bb.max[0] + dx, bb.max[1] + dy, bb.max[2] + dz}};
        x = (bb.min[0] + bb.max[0]) / 2.0;
        y = bb.min[1];
        z = (bb.min[2] + bb.max[2]) / 2.0;
        return;
    }
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

namespace {

// `Mth.clamp`: a float held between two bounds.
float clamp_f(float value, float low, float high) { return value < low ? low : (value > high ? high : value); }

// `Mth.lerp`: the point this far from one value to another.
float lerp_f(float by, float from, float to) { return from + by * (to - from); }

// `FluidState.getType`: which fluid is in the cell, if any.
Fluid fluid_at(const ParticleLevel& level, int32_t x, int32_t y, int32_t z) {
    const int32_t state = level.at(x, y, z);
    return state < 0 ? Fluid::None : level.shapes.state(state).fluid;
}

// `FlowingFluid.getHeight`: a full cell when the same fluid stands above it,
// else the fluid's own height.
double fluid_height(const ParticleLevel& level, int32_t x, int32_t y, int32_t z) {
    const int32_t state = level.at(x, y, z);
    if (state < 0) return 0.0;
    const auto& info = level.shapes.state(state);
    if (info.fluid == Fluid::None) return 0.0;
    if (fluid_at(level, x, y + 1, z) == info.fluid) return 1.0;
    return static_cast<double>(info.fluid_height());
}

// `VoxelShape.max(Axis.Y, x, z)`: how high the block's collision reaches over
// the point, zero where nothing of it is there.
double collision_top(const ParticleLevel& level, int32_t x, int32_t y, int32_t z, double px, double pz) {
    const int32_t state = level.at(x, y, z);
    if (state < 0) return 0.0;
    double top = 0.0;
    for (const auto& box : level.shapes.boxes(level.shapes.state(state).collision)) {
        if (px < box.min[0] || px > box.max[0] || pz < box.min[2] || pz > box.max[2]) continue;
        top = std::max(top, box.max[1]);
    }
    return top;
}

} // namespace

// `Particle.tick` and the tick of every class that replaces it.
void Particle::tick(BoxMover& mover, const ParticleLevel& level, const Emit& emit) {
    const auto* sprites = level.sprites;
    int32_t at[3];
    switch (kind) {
    case ParticleKind::Terrain:
    case ParticleKind::BreakingItem:
    case ParticleKind::Heart:
    case ParticleKind::Note:
        base_tick(mover);
        break;

    // `BaseAshSmokeParticle.tick`, `ExplodeParticle.tick`, `DustParticleBase.tick`: the sprite walks with the age.
    case ParticleKind::AshSmoke:
    case ParticleKind::Explode:
    case ParticleKind::Dust:
    case ParticleKind::DustTransition:
        base_tick(mover);
        if (sprites != nullptr) sprite_from_age(*sprites);
        break;

    // `DustPlumeParticle.tick`: the pull and the drag both ease off each tick.
    case ParticleKind::DustPlume:
        gravity = 0.88f * gravity;
        friction = 0.92f * friction;
        base_tick(mover);
        if (sprites != nullptr) sprite_from_age(*sprites);
        break;

    // `RisingParticle` has no tick of its own; `FlameParticle.move` is what differs.
    case ParticleKind::Rising:
        base_tick(mover);
        break;

    // `CampfireSmokeParticle.tick`: it wanders sideways and fades at the end.
    case ParticleKind::CampfireSmoke:
        xo = x;
        yo = y;
        zo = z;
        if (age++ >= lifetime || alpha <= 0.0f) {
            removed = true;
            break;
        }
        xd = xd + static_cast<double>(random.next_float() / 5000.0f * (random.next_bool() ? 1 : -1));
        zd = zd + static_cast<double>(random.next_float() / 5000.0f * (random.next_bool() ? 1 : -1));
        yd = yd - static_cast<double>(gravity);
        move(mover, xd, yd, zd);
        if (age >= lifetime - 60 && alpha > 0.01f) alpha -= 0.015f;
        break;

    // `CritParticle.tick`: the green and blue drain away.
    case ParticleKind::Crit:
        base_tick(mover);
        g *= 0.96f;
        b *= 0.9f;
        break;

    // `SimpleAnimatedParticle.tick`: it fades out over its second half.
    case ParticleKind::Animated:
        base_tick(mover);
        if (sprites != nullptr) sprite_from_age(*sprites);
        if (age > lifetime / 2) {
            alpha = 1.0f - (static_cast<float>(age) - static_cast<float>(lifetime / 2)) /
                               static_cast<float>(lifetime);
            if (has_fade) {
                r = r + (fade_r - r) * 0.2f;
                g = g + (fade_g - g) * 0.2f;
                b = b + (fade_b - b) * 0.2f;
            }
        }
        break;

    // `SpellParticle.tick`: the alpha creeps back to what it was.
    case ParticleKind::Spell:
        base_tick(mover);
        if (sprites != nullptr) sprite_from_age(*sprites);
        alpha = lerp_f(0.05f, alpha, original_alpha);
        break;

    // `HugeExplosionParticle.tick`: it only ages and changes sprite.
    case ParticleKind::HugeExplosion:
        xo = x;
        yo = y;
        zo = z;
        if (age++ >= lifetime) {
            removed = true;
        } else if (sprites != nullptr) {
            sprite_from_age(*sprites);
        }
        break;

    // `HugeExplosionSeedParticle.tick`: six explosions a tick, scattered.
    case ParticleKind::ExplosionSeed: {
        for (int i = 0; i < 6; ++i) {
            Spawn spawn;
            spawn.type = static_cast<int16_t>(ParticleType::Explosion);
            spawn.x = x + (random.next_double() - random.next_double()) * 4.0;
            spawn.y = y + (random.next_double() - random.next_double()) * 4.0;
            spawn.z = z + (random.next_double() - random.next_double()) * 4.0;
            // The explosion reads its size off the speed it is given.
            spawn.xd = static_cast<double>(static_cast<float>(age) / static_cast<float>(lifetime));
            emit(spawn);
        }
        ++age;
        if (age == lifetime) removed = true;
        break;
    }

    // `WaterDropParticle.tick`: it falls, and ends on the ground or in a fluid.
    case ParticleKind::WaterDrop:
        xo = x;
        yo = y;
        zo = z;
        if (lifetime-- <= 0) {
            removed = true;
            break;
        }
        yd = yd - static_cast<double>(gravity);
        move(mover, xd, yd, zd);
        xd *= static_cast<double>(0.98f);
        yd *= static_cast<double>(0.98f);
        zd *= static_cast<double>(0.98f);
        if (on_ground) {
            if (random.next_float() < 0.5f) removed = true;
            xd *= static_cast<double>(0.7f);
            zd *= static_cast<double>(0.7f);
        }
        containing(x, y, z, at);
        if (const double top = std::max(collision_top(level, at[0], at[1], at[2], x - at[0], z - at[2]),
                                        fluid_height(level, at[0], at[1], at[2]));
            top > 0.0 && y < at[1] + top) {
            removed = true;
        }
        break;

    // `BubbleParticle.tick`: it rises, slows, and pops out of water.
    case ParticleKind::Bubble:
        xo = x;
        yo = y;
        zo = z;
        if (lifetime-- <= 0) {
            removed = true;
            break;
        }
        yd += 0.002;
        move(mover, xd, yd, zd);
        xd *= static_cast<double>(0.85f);
        yd *= static_cast<double>(0.85f);
        zd *= static_cast<double>(0.85f);
        containing(x, y, z, at);
        if (fluid_at(level, at[0], at[1], at[2]) != Fluid::Water) removed = true;
        break;

    // `BubbleColumnUpParticle.tick`: the base tick, then out of water it goes.
    case ParticleKind::BubbleColumn:
        base_tick(mover);
        if (!removed) {
            containing(x, y, z, at);
            if (fluid_at(level, at[0], at[1], at[2]) != Fluid::Water) removed = true;
        }
        break;

    // `BubblePopParticle.tick`: four ticks of falling through its sprites.
    case ParticleKind::BubblePop:
        xo = x;
        yo = y;
        zo = z;
        if (age++ >= lifetime) {
            removed = true;
            break;
        }
        yd = yd - static_cast<double>(gravity);
        move(mover, xd, yd, zd);
        if (sprites != nullptr) sprite_from_age(*sprites);
        break;

    // `PortalParticle.tick`: it travels its whole path from where it set out.
    case ParticleKind::Portal:
        xo = x;
        yo = y;
        zo = z;
        if (age++ >= lifetime) {
            removed = true;
            break;
        }
        {
            float through = static_cast<float>(age) / static_cast<float>(lifetime);
            const float raw = through;
            through = -through + through * through * 2.0f;
            through = 1.0f - through;
            x = start[0] + xd * static_cast<double>(through);
            y = start[1] + yd * static_cast<double>(through) + static_cast<double>(1.0f - raw);
            z = start[2] + zd * static_cast<double>(through);
        }
        break;

    // `ReversePortalParticle.tick`: it walks its own speed, faster as it ages.
    case ParticleKind::ReversePortal:
        xo = x;
        yo = y;
        zo = z;
        if (age++ >= lifetime) {
            removed = true;
            break;
        }
        {
            const auto through = static_cast<double>(static_cast<float>(age) /
                                                     static_cast<float>(lifetime));
            x = x + xd * through;
            y = y + yd * through;
            z = z + zd * through;
        }
        break;

    // `DripParticle.tick`: what it does before the move, the move, what it
    // does after, and the fluid it may fall back into.
    case ParticleKind::Drip:
        xo = x;
        yo = y;
        zo = z;
        // `DripParticle.preMoveUpdate`, `CoolingDripHangParticle.preMoveUpdate`.
        if (drip_mode == DripMode::CoolingHang) {
            r = 1.0f;
            g = 16.0f / static_cast<float>(40 - lifetime + 16);
            b = 4.0f / static_cast<float>(40 - lifetime + 8);
        }
        if (lifetime-- <= 0) {
            removed = true;
            if (drip_mode == DripMode::Hang || drip_mode == DripMode::CoolingHang) {
                emit({spawns, x, y, z, xd, yd, zd});
            }
        }
        if (removed) break;
        yd = yd - static_cast<double>(gravity);
        move(mover, xd, yd, zd);
        // `FallingParticle.postMoveUpdate`, `FallAndLandParticle.postMoveUpdate`,
        // `DripHangParticle.postMoveUpdate`.
        if (drip_mode == DripMode::Hang || drip_mode == DripMode::CoolingHang) {
            xd *= 0.02;
            yd *= 0.02;
            zd *= 0.02;
        } else if (on_ground && drip_mode != DripMode::Land) {
            removed = true;
            if (drip_mode != DripMode::Falling) emit({spawns, x, y, z, 0.0, 0.0, 0.0});
            // `Mth.randomBetween`: the volume the honey and dripstone drops
            // land at. The sound is the sound area's; the draw is ours, and
            // leaving it out would move every later tick.
            if (drip_mode == DripMode::HoneyFall || drip_mode == DripMode::DripstoneFall) {
                random.next_float();
            }
        }
        if (removed) break;
        xd *= static_cast<double>(0.98f);
        yd *= static_cast<double>(0.98f);
        zd *= static_cast<double>(0.98f);
        if (drip_fluid != DripFluid::Empty) {
            containing(x, y, z, at);
            const Fluid wanted = drip_fluid == DripFluid::Water ? Fluid::Water : Fluid::Lava;
            if (fluid_at(level, at[0], at[1], at[2]) == wanted &&
                y < at[1] + fluid_height(level, at[0], at[1], at[2])) {
                removed = true;
            }
        }
        break;
    }
}

// `SingleQuadParticle.getQuadSize` and the classes that grow or shrink it.
float Particle::size_at(float partial) const {
    const float through = (static_cast<float>(age) + partial) / static_cast<float>(lifetime);
    switch (kind) {
    // `BaseAshSmokeParticle.getQuadSize`, `CritParticle.getQuadSize`,
    // `NoteParticle.getQuadSize`, `HeartParticle.getQuadSize`,
    // `DustParticleBase.getQuadSize`: full size after a thirty-second of its life.
    case ParticleKind::AshSmoke:
    case ParticleKind::Crit:
    case ParticleKind::Note:
    case ParticleKind::Heart:
    case ParticleKind::Dust:
    case ParticleKind::DustTransition:
    case ParticleKind::DustPlume:
        return quad_size * clamp_f(through * 32.0f, 0.0f, 1.0f);
    // `FlameParticle.getQuadSize`: it shrinks to half as it burns down.
    case ParticleKind::Rising:
        return quad_size * (1.0f - through * through * 0.5f);
    // `PortalParticle.getQuadSize`: it opens out and closes again.
    case ParticleKind::Portal: {
        float size = 1.0f - through;
        size *= size;
        return quad_size * (1.0f - size);
    }
    // `ReversePortalParticle.getQuadSize`: it shrinks over half again its life.
    case ParticleKind::ReversePortal:
        return quad_size * (1.0f - (static_cast<float>(age) + partial) /
                                       (static_cast<float>(lifetime) * 1.5f));
    default:
        return quad_size;
    }
}

namespace {

// `TerrainParticle` and `BreakingItemParticle` show a quarter of their sprite;
// every other class shows the whole of it.
bool quarters_sprite(ParticleKind kind) {
    return kind == ParticleKind::Terrain || kind == ParticleKind::BreakingItem;
}

} // namespace

// `TerrainParticle.getU0`, `BreakingItemParticle.getU0`: a quarter of the sprite, picked at random.
float Particle::u0() const { return quarters_sprite(kind) ? (uo + 1.0f) / 4.0f : 0.0f; }
// `TerrainParticle.getU1`, `BreakingItemParticle.getU1`: the other edge of that quarter.
float Particle::u1() const { return quarters_sprite(kind) ? uo / 4.0f : 1.0f; }
// `TerrainParticle.getV0`, `BreakingItemParticle.getV0`: the top of that quarter.
float Particle::v0() const { return quarters_sprite(kind) ? vo / 4.0f : 0.0f; }
// `TerrainParticle.getV1`, `BreakingItemParticle.getV1`: the bottom of that quarter.
float Particle::v1() const { return quarters_sprite(kind) ? (vo + 1.0f) / 4.0f : 1.0f; }

// `DustColorTransitionParticle.lerpColors`: the colour on its way from one to
// the other; every other class keeps the colour it has.
void Particle::colour_at(float partial, float out[3]) const {
    if (kind != ParticleKind::DustTransition) {
        out[0] = r;
        out[1] = g;
        out[2] = b;
        return;
    }
    const float through = (static_cast<float>(age) + partial) / (static_cast<float>(lifetime) + 1.0f);
    out[0] = lerp_f(through, from_r, fade_r);
    out[1] = lerp_f(through, from_g, fade_g);
    out[2] = lerp_f(through, from_b, fade_b);
}

// `Particle.getLightColor`, `TerrainParticle.getLightColor` and the other
// classes that light themselves:
// `TerrainParticle`, `FlameParticle`, `PortalParticle`, `DripParticle`,
// `SimpleAnimatedParticle` and `HugeExplosionParticle`.
int32_t Particle::light_colour(const ParticleLevel& level, float partial) const {
    // 15728880 is block light 15 and sky light 15, the game's full bright.
    constexpr int32_t kFullBright = 15728880;
    if (kind == ParticleKind::Animated || kind == ParticleKind::HugeExplosion) return kFullBright;
    if (kind == ParticleKind::Drip && glowing) return 240;

    int32_t at[3];
    containing(x, y, z, at);
    int32_t light = std::max(0, level.light(at[0], at[1], at[2]));
    if (light == 0 && kind == ParticleKind::Terrain) {
        light = std::max(0, level.light(pos[0], pos[1], pos[2]));
    }
    if (kind == ParticleKind::Rising) {
        const float through = clamp_f((static_cast<float>(age) + partial) / static_cast<float>(lifetime),
                                      0.0f, 1.0f);
        int32_t block = light & 0xFF;
        const int32_t sky = light >> 16 & 0xFF;
        block += static_cast<int32_t>(through * 15.0f * 16.0f);
        if (block > 240) block = 240;
        return block | sky << 16;
    }
    if (kind == ParticleKind::Portal || kind == ParticleKind::ReversePortal) {
        float through = static_cast<float>(age) / static_cast<float>(lifetime);
        through *= through;
        through *= through;
        const int32_t block = light & 0xFF;
        int32_t sky = light >> 16 & 0xFF;
        sky += static_cast<int32_t>(through * 15.0f * 16.0f);
        if (sky > 240) sky = 240;
        return block | sky << 16;
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

namespace {

// `ParticleTypes`: the registry name of each type we make, in the enum's order.
constexpr std::string_view kTypeNames[] = {
    "minecraft:smoke", "minecraft:large_smoke", "minecraft:white_smoke",
    "minecraft:campfire_cosy_smoke", "minecraft:campfire_signal_smoke", "minecraft:ash",
    "minecraft:white_ash",
    "minecraft:flame", "minecraft:small_flame", "minecraft:soul_fire_flame",
    "minecraft:copper_fire_flame",
    "minecraft:poof", "minecraft:explosion", "minecraft:explosion_emitter",
    "minecraft:crit", "minecraft:enchanted_hit", "minecraft:damage_indicator",
    "minecraft:item", "minecraft:item_slime", "minecraft:item_snowball", "minecraft:item_cobweb",
    "minecraft:block", "minecraft:block_crumble", "minecraft:falling_dust", "minecraft:dust_pillar",
    "minecraft:effect", "minecraft:instant_effect", "minecraft:entity_effect", "minecraft:witch",
    "minecraft:infested", "minecraft:raid_omen", "minecraft:trial_omen",
    "minecraft:splash", "minecraft:rain", "minecraft:bubble", "minecraft:bubble_column_up",
    "minecraft:bubble_pop",
    "minecraft:note", "minecraft:heart", "minecraft:angry_villager",
    "minecraft:totem_of_undying", "minecraft:portal", "minecraft:reverse_portal",
    "minecraft:dust", "minecraft:dust_color_transition", "minecraft:dust_plume",
    "minecraft:dripping_water", "minecraft:falling_water", "minecraft:dripping_lava",
    "minecraft:falling_lava", "minecraft:landing_lava",
    "minecraft:dripping_honey", "minecraft:falling_honey", "minecraft:landing_honey",
    "minecraft:falling_nectar",
    "minecraft:dripping_obsidian_tear", "minecraft:falling_obsidian_tear",
    "minecraft:landing_obsidian_tear",
    "minecraft:dripping_dripstone_water", "minecraft:falling_dripstone_water",
    "minecraft:dripping_dripstone_lava", "minecraft:falling_dripstone_lava",
};
static_assert(std::size(kTypeNames) == static_cast<size_t>(ParticleType::Count),
              "every particle type needs its registry name");

// `ARGB.red`, `ARGB.green`, `ARGB.blue`, `ARGB.alpha`: one channel as a float.
float channel_of(uint32_t argb, int shift) {
    return static_cast<float>(argb >> shift & 0xFFu) / 255.0f;
}

// The start every `SingleQuadParticle` shares: the box, the position and the
// lifetime of `Particle`, then the quad size.
Particle quad_particle(int64_t seed, double x, double y, double z) {
    Particle p;
    p.random = jmath::LegacyRandom(seed);
    p.set_size(0.2f, 0.2f);
    p.set_pos(x, y, z);
    p.xo = x;
    p.yo = y;
    p.zo = z;
    p.lifetime = static_cast<int32_t>(4.0f / (p.random.next_float() * 0.9f + 0.1f));
    p.quad_size = 0.1f * (p.random.next_float() * 0.5f + 0.5f) * 2.0f;
    p.layer = QuadLayer::Opaque;
    return p;
}

// `Particle.Particle` with a speed: the scatter around what was asked for.
void scatter_speed(Particle& p, double xd, double yd, double zd) {
    p.xd = xd + static_cast<double>((p.random.next_float() * 2.0f - 1.0f) * 0.4f);
    p.yd = yd + static_cast<double>((p.random.next_float() * 2.0f - 1.0f) * 0.4f);
    p.zd = zd + static_cast<double>((p.random.next_float() * 2.0f - 1.0f) * 0.4f);
    const auto speed = static_cast<double>((p.random.next_float() + p.random.next_float() + 1.0f) * 0.15f);
    const double length = std::sqrt(p.xd * p.xd + p.yd * p.yd + p.zd * p.zd);
    p.xd = p.xd / length * speed * static_cast<double>(0.4f);
    p.yd = p.yd / length * speed * static_cast<double>(0.4f) + static_cast<double>(0.1f);
    p.zd = p.zd / length * speed * static_cast<double>(0.4f);
}

// The quad start with a speed, which is the three-argument constructor
// followed by the six-argument one.
Particle quad_particle(int64_t seed, double x, double y, double z, double xd, double yd, double zd) {
    Particle p;
    p.random = jmath::LegacyRandom(seed);
    p.set_size(0.2f, 0.2f);
    p.set_pos(x, y, z);
    p.xo = x;
    p.yo = y;
    p.zo = z;
    p.lifetime = static_cast<int32_t>(4.0f / (p.random.next_float() * 0.9f + 0.1f));
    scatter_speed(p, xd, yd, zd);
    p.quad_size = 0.1f * (p.random.next_float() * 0.5f + 0.5f) * 2.0f;
    p.layer = QuadLayer::Opaque;
    return p;
}

// `BaseAshSmokeParticle.BaseAshSmokeParticle`: the smokes and ashes, which
// differ only in what they are handed.
Particle ash_smoke(const ParticleSprites& sprites, int32_t set, int64_t seed, double x, double y,
                   double z, float sx, float sy, float sz, double xd, double yd, double zd,
                   float scale, float brightness, int32_t base_lifetime, float gravity, bool physics) {
    Particle p = quad_particle(seed, x, y, z, 0.0, 0.0, 0.0);
    p.kind = ParticleKind::AshSmoke;
    p.sprite_set = set;
    p.friction = 0.96f;
    p.gravity = gravity;
    p.speed_up_when_y_motion_is_blocked = true;
    p.xd *= static_cast<double>(sx);
    p.yd *= static_cast<double>(sy);
    p.zd *= static_cast<double>(sz);
    p.xd += xd;
    p.yd += yd;
    p.zd += zd;
    const float shade = p.random.next_float() * brightness;
    p.set_colour(shade, shade, shade);
    p.quad_size *= 0.75f * scale;
    p.lifetime = static_cast<int32_t>(static_cast<float>(base_lifetime) /
                                      static_cast<float>(p.random.next_float() * 0.8 + 0.2) * scale);
    p.lifetime = std::max(p.lifetime, 1);
    p.sprite_from_age(sprites);
    p.has_physics = physics;
    return p;
}

// `RisingParticle.RisingParticle`, `FlameParticle`: the flames, which drift up
// and pass through blocks.
Particle rising(uint16_t sprite, int64_t seed, double x, double y, double z, double xd, double yd,
                double zd) {
    Particle p = quad_particle(seed, x, y, z, xd, yd, zd);
    p.kind = ParticleKind::Rising;
    p.texture = sprite;
    p.friction = 0.96f;
    p.xd = p.xd * static_cast<double>(0.01f) + xd;
    p.yd = p.yd * static_cast<double>(0.01f) + yd;
    p.zd = p.zd * static_cast<double>(0.01f) + zd;
    p.x = p.x + static_cast<double>((p.random.next_float() - p.random.next_float()) * 0.05f);
    p.y = p.y + static_cast<double>((p.random.next_float() - p.random.next_float()) * 0.05f);
    p.z = p.z + static_cast<double>((p.random.next_float() - p.random.next_float()) * 0.05f);
    p.lifetime = static_cast<int32_t>(8.0 / (p.random.next_float() * 0.8 + 0.2)) + 4;
    return p;
}

// `CritParticle.CritParticle`: the hit sparks, which tick once on the spot.
Particle crit(const ParticleLevel& level, uint16_t sprite, int64_t seed, double x, double y,
              double z, double xd, double yd, double zd) {
    Particle p = quad_particle(seed, x, y, z, 0.0, 0.0, 0.0);
    p.kind = ParticleKind::Crit;
    p.texture = sprite;
    p.friction = 0.7f;
    p.gravity = 0.5f;
    p.xd *= static_cast<double>(0.1f);
    p.yd *= static_cast<double>(0.1f);
    p.zd *= static_cast<double>(0.1f);
    p.xd += xd * 0.4;
    p.yd += yd * 0.4;
    p.zd += zd * 0.4;
    const float shade = p.random.next_float() * 0.3f + 0.6f;
    p.set_colour(shade, shade, shade);
    p.quad_size *= 0.75f;
    p.lifetime = std::max(static_cast<int32_t>(6.0 / (p.random.next_float() * 0.8 + 0.6)), 1);
    p.has_physics = false;
    // The constructor's last line is a tick of its own, so a crit is already
    // one tick old the moment it is made.
    BoxMover mover(World{level.at, level.shapes, nullptr, level.min_y});
    p.tick(mover, level, [](const Particle::Spawn&) {});
    return p;
}

// `SpellParticle.SpellParticle`: an effect's swirl. Its sideways drift comes
// from the class's own static generator, which nothing else draws from; here
// that is the engine's, which no spell type touches otherwise.
Particle spell(const ParticleSprites& sprites, int32_t set, int64_t seed, double x, double y,
               double z, double xd, double yd, double zd, jmath::LegacyRandom& spell_random) {
    const double sx = 0.5 - spell_random.next_double();
    const double sz = 0.5 - spell_random.next_double();
    Particle p = quad_particle(seed, x, y, z, sx, yd, sz);
    p.kind = ParticleKind::Spell;
    p.sprite_set = set;
    p.texture = sprites.first(set);
    p.layer = QuadLayer::Translucent;
    p.friction = 0.96f;
    p.gravity = -0.1f;
    p.speed_up_when_y_motion_is_blocked = true;
    p.yd *= static_cast<double>(0.2f);
    if (xd == 0.0 && zd == 0.0) {
        p.xd *= static_cast<double>(0.1f);
        p.zd *= static_cast<double>(0.1f);
    }
    p.quad_size *= 0.75f;
    p.lifetime = static_cast<int32_t>(8.0 / (p.random.next_float() * 0.8 + 0.2));
    p.has_physics = false;
    p.sprite_from_age(sprites);
    return p;
}

// `DripParticle.DripParticle`: a drop of something, before its mode is set.
Particle drip(uint16_t sprite, int64_t seed, double x, double y, double z, DripFluid fluid) {
    Particle p = quad_particle(seed, x, y, z);
    p.kind = ParticleKind::Drip;
    p.texture = sprite;
    p.set_size(0.01f, 0.01f);
    p.gravity = 0.06f;
    p.drip_fluid = fluid;
    return p;
}

// `WaterDropParticle.WaterDropParticle`: a drop of rain or a splash.
Particle water_drop(uint16_t sprite, int64_t seed, double x, double y, double z) {
    Particle p = quad_particle(seed, x, y, z, 0.0, 0.0, 0.0);
    p.kind = ParticleKind::WaterDrop;
    p.texture = sprite;
    p.xd *= static_cast<double>(0.3f);
    p.yd = static_cast<double>(p.random.next_float() * 0.2f + 0.1f);
    p.zd *= static_cast<double>(0.3f);
    p.set_size(0.01f, 0.01f);
    p.gravity = 0.06f;
    p.lifetime = static_cast<int32_t>(8.0 / (p.random.next_float() * 0.8 + 0.2));
    return p;
}

} // namespace

std::string_view particle_type_name(ParticleType type) {
    const auto index = static_cast<size_t>(type);
    return index < std::size(kTypeNames) ? kTypeNames[index] : std::string_view();
}

ParticleType particle_type_from_name(std::string_view name) {
    for (size_t i = 0; i < std::size(kTypeNames); ++i) {
        if (kTypeNames[i] == name) return static_cast<ParticleType>(i);
    }
    return ParticleType::None;
}

namespace {

// `ParticleProvider.createParticle` for every type we make.
std::optional<Particle> provide(const ParticleLevel& level, ParticleType type,
                                const ParticleOptions& options, int64_t seed,
                                double x, double y, double z, double xd, double yd, double zd,
                                jmath::LegacyRandom& level_random,
                                jmath::LegacyRandom& spell_random) {
    if (level.sprites == nullptr) return std::nullopt;
    const ParticleSprites& sprites = *level.sprites;
    const int32_t set = sprites.find(particle_type_name(type));
    // A sprite-based type with no sprite set has nothing to draw.
    const bool needs_sprite = type != ParticleType::Block && type != ParticleType::BlockCrumble &&
                              type != ParticleType::FallingDust && type != ParticleType::DustPillar &&
                              type != ParticleType::Item && type != ParticleType::ItemSlime &&
                              type != ParticleType::ItemSnowball && type != ParticleType::ItemCobweb &&
                              type != ParticleType::ExplosionEmitter;
    if (needs_sprite && set < 0) return std::nullopt;
    // `SpriteSet.get(RandomSource)`: the providers that pick one at random do
    // so from the engine's generator, not the particle's own.
    const auto pick = [&]() {
        return sprites.layer(set, static_cast<size_t>(level_random.next_int(
                                      static_cast<int32_t>(sprites.size(set)))));
    };

    switch (type) {
    // `SmokeParticle.Provider.createParticle`, `LargeSmokeParticle.Provider.createParticle`,
    // `WhiteSmokeParticle.Provider.createParticle`, `DustPlumeParticle.Provider.createParticle`.
    case ParticleType::Smoke:
        return ash_smoke(sprites, set, seed, x, y, z, 0.1f, 0.1f, 0.1f, xd, yd, zd, 1.0f, 0.3f, 8,
                         -0.1f, true);
    case ParticleType::LargeSmoke:
        return ash_smoke(sprites, set, seed, x, y, z, 0.1f, 0.1f, 0.1f, xd, yd, zd, 2.5f, 0.3f, 8,
                         -0.1f, true);
    case ParticleType::WhiteSmoke: {
        Particle p = ash_smoke(sprites, set, seed, x, y, z, 0.1f, 0.1f, 0.1f, xd, yd, zd, 1.0f, 0.3f,
                               8, -0.1f, true);
        p.set_colour(0.7294118f, 0.69411767f, 0.7607843f);
        return p;
    }
    // `AshParticle.Provider.createParticle`: it drifts on its own, whatever speed was asked.
    case ParticleType::Ash:
        return ash_smoke(sprites, set, seed, x, y, z, 0.1f, -0.1f, 0.1f, 0.0, 0.0, 0.0, 1.0f, 0.5f,
                         20, 0.1f, false);
    // `WhiteAshParticle.Provider.createParticle`: its drift is drawn, not given.
    case ParticleType::WhiteAsh: {
        const float ax0 = level_random.next_float();
        const float ax1 = level_random.next_float();
        const float ay0 = level_random.next_float();
        const float ay1 = level_random.next_float();
        const float az0 = level_random.next_float();
        const float az1 = level_random.next_float();
        const double ax = ax0 * -1.9 * ax1 * 0.1;
        const double ay = ay0 * -0.5 * ay1 * 0.1 * 5.0;
        const double az = az0 * -1.9 * az1 * 0.1;
        Particle p = ash_smoke(sprites, set, seed, x, y, z, 0.1f, -0.1f, 0.1f, ax, ay, az, 1.0f, 0.0f,
                               20, 0.0125f, false);
        p.set_colour(channel_of(12235202u, 16), channel_of(12235202u, 8), channel_of(12235202u, 0));
        return p;
    }
    case ParticleType::DustPlume: {
        Particle p = ash_smoke(sprites, set, seed, x, y, z, 0.7f, 0.6f, 0.7f, xd,
                               yd + static_cast<double>(0.15f), zd, 1.0f, 0.5f, 7, 0.5f, false);
        p.kind = ParticleKind::DustPlume;
        const float dim = p.random.next_float() * 0.2f;
        p.set_colour(channel_of(12235202u, 16) - dim, channel_of(12235202u, 8) - dim,
                     channel_of(12235202u, 0) - dim);
        return p;
    }

    // `CampfireSmokeParticle.CosyProvider.createParticle`,
    // `CampfireSmokeParticle.SignalProvider.createParticle`.
    case ParticleType::CampfireCosySmoke:
    case ParticleType::CampfireSignalSmoke: {
        const bool signal = type == ParticleType::CampfireSignalSmoke;
        Particle p = quad_particle(seed, x, y, z);
        p.kind = ParticleKind::CampfireSmoke;
        p.texture = pick();
        p.layer = QuadLayer::Translucent;
        p.scale(3.0f);
        p.set_size(0.25f, 0.25f);
        p.lifetime = p.random.next_int(50) + (signal ? 280 : 80);
        p.gravity = 3.0e-6f;
        p.xd = xd;
        p.yd = yd + static_cast<double>(p.random.next_float() / 500.0f);
        p.zd = zd;
        p.alpha = signal ? 0.95f : 0.9f;
        return p;
    }

    // `FlameParticle.Provider.createParticle`, `FlameParticle.SmallFlameProvider.createParticle`.
    case ParticleType::Flame:
    case ParticleType::SoulFireFlame:
    case ParticleType::CopperFireFlame:
        return rising(pick(), seed, x, y, z, xd, yd, zd);
    case ParticleType::SmallFlame: {
        Particle p = rising(pick(), seed, x, y, z, xd, yd, zd);
        p.scale(0.5f);
        return p;
    }

    // `ExplodeParticle.ExplodeParticle`: the puff a mob leaves.
    case ParticleType::Poof: {
        Particle p = quad_particle(seed, x, y, z);
        p.kind = ParticleKind::Explode;
        p.sprite_set = set;
        p.texture = sprites.first(set);
        p.gravity = -0.1f;
        p.friction = 0.9f;
        p.xd = xd + static_cast<double>((p.random.next_float() * 2.0f - 1.0f) * 0.05f);
        p.yd = yd + static_cast<double>((p.random.next_float() * 2.0f - 1.0f) * 0.05f);
        p.zd = zd + static_cast<double>((p.random.next_float() * 2.0f - 1.0f) * 0.05f);
        const float shade = p.random.next_float() * 0.3f + 0.7f;
        p.set_colour(shade, shade, shade);
        p.quad_size = 0.1f * (p.random.next_float() * p.random.next_float() * 6.0f + 1.0f);
        p.lifetime = static_cast<int32_t>(16.0 / (p.random.next_float() * 0.8 + 0.2)) + 2;
        p.sprite_from_age(sprites);
        return p;
    }

    // `HugeExplosionParticle.HugeExplosionParticle`: one ball of the blast,
    // its size set by how far through its emitter's life it was made.
    case ParticleType::Explosion: {
        Particle p = quad_particle(seed, x, y, z, 0.0, 0.0, 0.0);
        p.kind = ParticleKind::HugeExplosion;
        p.sprite_set = set;
        p.texture = sprites.first(set);
        p.lifetime = 6 + p.random.next_int(4);
        const float shade = p.random.next_float() * 0.6f + 0.4f;
        p.set_colour(shade, shade, shade);
        // Its emitter passes how far through its own life it is as the speed.
        p.quad_size = 2.0f * (1.0f - static_cast<float>(xd) * 0.5f);
        p.seed_age = static_cast<float>(xd);
        p.sprite_from_age(sprites);
        return p;
    }

    // `HugeExplosionSeedParticle.HugeExplosionSeedParticle`: eight ticks that
    // throw explosions about and draw nothing themselves.
    case ParticleType::ExplosionEmitter: {
        Particle p;
        p.random = jmath::LegacyRandom(seed);
        p.set_size(0.2f, 0.2f);
        p.set_pos(x, y, z);
        p.xo = x;
        p.yo = y;
        p.zo = z;
        p.lifetime = static_cast<int32_t>(4.0f / (p.random.next_float() * 0.9f + 0.1f));
        scatter_speed(p, 0.0, 0.0, 0.0);
        p.kind = ParticleKind::ExplosionSeed;
        p.quad_size = 0.0f;
        p.lifetime = 8;
        return p;
    }

    // `CritParticle.Provider.createParticle`, `CritParticle.MagicProvider.createParticle`,
    // `CritParticle.DamageIndicatorProvider.createParticle`.
    case ParticleType::Crit:
        return crit(level, pick(), seed, x, y, z, xd, yd, zd);
    case ParticleType::EnchantedHit: {
        Particle p = crit(level, pick(), seed, x, y, z, xd, yd, zd);
        p.r *= 0.3f;
        p.g *= 0.8f;
        return p;
    }
    case ParticleType::DamageIndicator: {
        Particle p = crit(level, pick(), seed, x, y, z, xd, yd + 1.0, zd);
        p.lifetime = 20;
        return p;
    }

    // `BreakingItemParticle.BreakingItemParticle`: a chip off an item.
    case ParticleType::Item:
    case ParticleType::ItemSlime:
    case ParticleType::ItemSnowball:
    case ParticleType::ItemCobweb: {
        if (!options.has_texture) return std::nullopt;
        const bool aimed = type == ParticleType::Item;
        Particle p = quad_particle(seed, x, y, z, 0.0, 0.0, 0.0);
        p.kind = ParticleKind::BreakingItem;
        p.texture = options.texture;
        p.gravity = 1.0f;
        p.quad_size /= 2.0f;
        p.uo = p.random.next_float() * 3.0f;
        p.vo = p.random.next_float() * 3.0f;
        p.layer = options.texture_layer;
        if (aimed) {
            p.xd *= static_cast<double>(0.1f);
            p.yd *= static_cast<double>(0.1f);
            p.zd *= static_cast<double>(0.1f);
            p.xd += xd;
            p.yd += yd;
            p.zd += zd;
        }
        return p;
    }

    // `TerrainParticle.Provider.createParticle` and the block types that reuse it.
    case ParticleType::Block:
    case ParticleType::BlockCrumble:
    case ParticleType::FallingDust:
    case ParticleType::DustPillar: {
        if (options.block_state < 0) return std::nullopt;
        int32_t at[3];
        containing(x, y, z, at);
        return terrain_particle(level, seed, x, y, z, xd, yd, zd, options.block_state, at);
    }

    // `SpellParticle.Provider.createParticle` and its flavours: the swirls an effect leaves.
    case ParticleType::Effect:
    case ParticleType::InstantEffect:
    case ParticleType::EntityEffect:
    case ParticleType::Witch:
    case ParticleType::Infested:
    case ParticleType::RaidOmen:
    case ParticleType::TrialOmen: {
        Particle p = spell(sprites, set, seed, x, y, z, xd, yd, zd, spell_random);
        if (type == ParticleType::Effect || type == ParticleType::InstantEffect) {
            p.set_colour(channel_of(options.colour, 16), channel_of(options.colour, 8),
                         channel_of(options.colour, 0));
            p.set_power(options.power);
        } else if (type == ParticleType::EntityEffect) {
            p.set_colour(channel_of(options.colour, 16), channel_of(options.colour, 8),
                         channel_of(options.colour, 0));
            p.alpha = channel_of(options.colour, 24);
            p.original_alpha = p.alpha;
        } else if (type == ParticleType::Witch) {
            const float shade = level_random.next_float() * 0.5f + 0.35f;
            p.set_colour(1.0f * shade, 0.0f, 1.0f * shade);
        }
        return p;
    }

    // `TotemParticle.TotemParticle`: the green and gold of a totem going off.
    case ParticleType::TotemOfUndying: {
        Particle p = quad_particle(seed, x, y, z);
        p.kind = ParticleKind::Animated;
        p.sprite_set = set;
        p.texture = sprites.first(set);
        p.layer = QuadLayer::Translucent;
        p.friction = 0.6f;
        p.gravity = 1.25f;
        p.xd = xd;
        p.yd = yd;
        p.zd = zd;
        p.quad_size *= 0.75f;
        p.lifetime = 60 + p.random.next_int(12);
        p.sprite_from_age(sprites);
        const bool gold = p.random.next_int(4) == 0;
        const float c0 = p.random.next_float();
        const float c1 = p.random.next_float();
        const float c2 = p.random.next_float();
        if (gold) {
            p.set_colour(0.6f + c0 * 0.2f, 0.6f + c1 * 0.3f, c2 * 0.2f);
        } else {
            p.set_colour(0.1f + c0 * 0.2f, 0.4f + c1 * 0.3f, c2 * 0.2f);
        }
        return p;
    }

    // `WaterDropParticle.Provider.createParticle`, `SplashParticle.SplashParticle`.
    case ParticleType::Rain:
        return water_drop(pick(), seed, x, y, z);
    case ParticleType::Splash: {
        Particle p = water_drop(pick(), seed, x, y, z);
        p.gravity = 0.04f;
        if (yd == 0.0 && (xd != 0.0 || zd != 0.0)) {
            p.xd = xd;
            p.yd = 0.1;
            p.zd = zd;
        }
        return p;
    }

    // `BubbleParticle.BubbleParticle`, `BubbleColumnUpParticle.BubbleColumnUpParticle`.
    case ParticleType::Bubble:
    case ParticleType::BubbleColumnUp: {
        const bool column = type == ParticleType::BubbleColumnUp;
        Particle p = quad_particle(seed, x, y, z);
        p.kind = column ? ParticleKind::BubbleColumn : ParticleKind::Bubble;
        p.texture = pick();
        if (column) {
            p.gravity = -0.125f;
            p.friction = 0.85f;
        }
        p.set_size(0.02f, 0.02f);
        p.quad_size = p.quad_size * (p.random.next_float() * 0.6f + 0.2f);
        p.xd = xd * static_cast<double>(0.2f) +
               static_cast<double>((p.random.next_float() * 2.0f - 1.0f) * 0.02f);
        p.yd = yd * static_cast<double>(0.2f) +
               static_cast<double>((p.random.next_float() * 2.0f - 1.0f) * 0.02f);
        p.zd = zd * static_cast<double>(0.2f) +
               static_cast<double>((p.random.next_float() * 2.0f - 1.0f) * 0.02f);
        p.lifetime = static_cast<int32_t>((column ? 40.0 : 8.0) / (p.random.next_float() * 0.8 + 0.2));
        return p;
    }

    // `BubblePopParticle.BubblePopParticle`: four ticks of a bubble bursting.
    case ParticleType::BubblePop: {
        Particle p = quad_particle(seed, x, y, z);
        p.kind = ParticleKind::BubblePop;
        p.sprite_set = set;
        p.texture = sprites.first(set);
        p.lifetime = 4;
        p.gravity = 0.008f;
        p.xd = xd;
        p.yd = yd;
        p.zd = zd;
        p.sprite_from_age(sprites);
        return p;
    }

    // `NoteParticle.NoteParticle`: the colour is the note, around the circle.
    case ParticleType::Note: {
        Particle p = quad_particle(seed, x, y, z, 0.0, 0.0, 0.0);
        p.kind = ParticleKind::Note;
        p.texture = pick();
        p.friction = 0.66f;
        p.speed_up_when_y_motion_is_blocked = true;
        p.xd *= static_cast<double>(0.01f);
        p.yd *= static_cast<double>(0.01f);
        p.zd *= static_cast<double>(0.01f);
        p.yd += 0.2;
        const auto note = static_cast<float>(xd);
        p.r = std::max(0.0f, jmath::sin((note + 0.0f) * 6.2831855f) * 0.65f + 0.35f);
        p.g = std::max(0.0f, jmath::sin((note + 0.33333334f) * 6.2831855f) * 0.65f + 0.35f);
        p.b = std::max(0.0f, jmath::sin((note + 0.6666667f) * 6.2831855f) * 0.65f + 0.35f);
        p.quad_size *= 1.5f;
        p.lifetime = 6;
        return p;
    }

    // `HeartParticle.Provider.createParticle`, `HeartParticle.AngryVillagerProvider.createParticle`.
    case ParticleType::Heart:
    case ParticleType::AngryVillager: {
        const bool angry = type == ParticleType::AngryVillager;
        Particle p = quad_particle(seed, x, angry ? y + 0.5 : y, z, 0.0, 0.0, 0.0);
        p.kind = ParticleKind::Heart;
        p.texture = pick();
        p.speed_up_when_y_motion_is_blocked = true;
        p.friction = 0.86f;
        p.xd *= static_cast<double>(0.01f);
        p.yd *= static_cast<double>(0.01f);
        p.zd *= static_cast<double>(0.01f);
        p.yd += 0.1;
        p.quad_size *= 1.5f;
        p.lifetime = 16;
        p.has_physics = false;
        if (angry) p.set_colour(1.0f, 1.0f, 1.0f);
        return p;
    }

    // `PortalParticle.PortalParticle`, `ReversePortalParticle.ReversePortalParticle`.
    case ParticleType::Portal:
    case ParticleType::ReversePortal: {
        const bool reverse = type == ParticleType::ReversePortal;
        Particle p = quad_particle(seed, x, y, z);
        p.kind = reverse ? ParticleKind::ReversePortal : ParticleKind::Portal;
        p.texture = pick();
        p.xd = xd;
        p.yd = yd;
        p.zd = zd;
        p.x = x;
        p.y = y;
        p.z = z;
        p.start[0] = x;
        p.start[1] = y;
        p.start[2] = z;
        p.quad_size = 0.1f * (p.random.next_float() * 0.2f + 0.5f);
        const float shade = p.random.next_float() * 0.6f + 0.4f;
        p.set_colour(shade * 0.9f, shade * 0.3f, shade);
        p.lifetime = static_cast<int32_t>(p.random.next_float() * 10.0f) + 40;
        if (reverse) {
            p.quad_size *= 1.5f;
            p.lifetime = static_cast<int32_t>(p.random.next_float() * 2.0f) + 60;
        }
        return p;
    }

    // `DustParticle.DustParticle`, `DustColorTransitionParticle`: redstone and
    // the sculk's colour change, both `DustParticleBase` underneath.
    case ParticleType::Dust:
    case ParticleType::DustColorTransition: {
        Particle p = quad_particle(seed, x, y, z, xd, yd, zd);
        p.kind = ParticleKind::Dust;
        p.sprite_set = set;
        p.texture = sprites.first(set);
        p.friction = 0.96f;
        p.speed_up_when_y_motion_is_blocked = true;
        p.xd *= static_cast<double>(0.1f);
        p.yd *= static_cast<double>(0.1f);
        p.zd *= static_cast<double>(0.1f);
        p.quad_size = p.quad_size * (0.75f * options.scale);
        const auto base = static_cast<int32_t>(8.0 / (p.random.next_double() * 0.8 + 0.2));
        p.lifetime = static_cast<int32_t>(
            std::max(static_cast<float>(base) * options.scale, 1.0f));
        p.sprite_from_age(sprites);
        // `DustParticleBase.randomizeColor`: the colour dimmed by a draw of its own.
        const float shade = p.random.next_float() * 0.4f + 0.6f;
        const auto randomize = [&](float channel) {
            return (p.random.next_float() * 0.2f + 0.8f) * channel * shade;
        };
        if (type == ParticleType::DustColorTransition) {
            // `DustColorTransitionParticle`: it keeps both colours and stays
            // white until the first frame lerps between them.
            p.kind = ParticleKind::DustTransition;
            p.has_fade = true;
            p.from_r = randomize(channel_of(options.colour, 16));
            p.from_g = randomize(channel_of(options.colour, 8));
            p.from_b = randomize(channel_of(options.colour, 0));
            p.fade_r = randomize(channel_of(options.to_colour, 16));
            p.fade_g = randomize(channel_of(options.to_colour, 8));
            p.fade_b = randomize(channel_of(options.to_colour, 0));
        } else {
            p.r = randomize(channel_of(options.colour, 16));
            p.g = randomize(channel_of(options.colour, 8));
            p.b = randomize(channel_of(options.colour, 0));
        }
        return p;
    }

    // `DripParticle`'s providers: which fluid, which mode, what it leaves.
    case ParticleType::DrippingWater: {
        Particle p = drip(pick(), seed, x, y, z, DripFluid::Water);
        p.drip_mode = DripMode::Hang;
        p.gravity *= 0.02f;
        p.lifetime = 40;
        p.spawns = static_cast<int16_t>(ParticleType::FallingWater);
        p.set_colour(0.2f, 0.3f, 1.0f);
        return p;
    }
    case ParticleType::FallingWater: {
        Particle p = drip(pick(), seed, x, y, z, DripFluid::Water);
        p.drip_mode = DripMode::FallAndLand;
        p.lifetime = static_cast<int32_t>(64.0 / (p.random.next_float() * 0.8 + 0.2));
        p.spawns = static_cast<int16_t>(ParticleType::Splash);
        p.set_colour(0.2f, 0.3f, 1.0f);
        return p;
    }
    case ParticleType::DrippingLava: {
        Particle p = drip(pick(), seed, x, y, z, DripFluid::Lava);
        p.drip_mode = DripMode::CoolingHang;
        p.gravity *= 0.02f;
        p.lifetime = 40;
        p.spawns = static_cast<int16_t>(ParticleType::FallingLava);
        return p;
    }
    case ParticleType::FallingLava: {
        Particle p = drip(pick(), seed, x, y, z, DripFluid::Lava);
        p.drip_mode = DripMode::FallAndLand;
        p.lifetime = static_cast<int32_t>(64.0 / (p.random.next_float() * 0.8 + 0.2));
        p.spawns = static_cast<int16_t>(ParticleType::LandingLava);
        p.set_colour(1.0f, 0.2857143f, 0.083333336f);
        return p;
    }
    case ParticleType::LandingLava: {
        Particle p = drip(pick(), seed, x, y, z, DripFluid::Lava);
        p.drip_mode = DripMode::Land;
        p.lifetime = static_cast<int32_t>(16.0 / (p.random.next_float() * 0.8 + 0.2));
        p.set_colour(1.0f, 0.2857143f, 0.083333336f);
        return p;
    }
    case ParticleType::DrippingHoney: {
        Particle p = drip(pick(), seed, x, y, z, DripFluid::Empty);
        p.drip_mode = DripMode::Hang;
        p.gravity *= 0.02f;
        p.gravity *= 0.01f;
        p.lifetime = 100;
        p.spawns = static_cast<int16_t>(ParticleType::FallingHoney);
        p.set_colour(0.622f, 0.508f, 0.082f);
        return p;
    }
    case ParticleType::FallingHoney: {
        Particle p = drip(pick(), seed, x, y, z, DripFluid::Empty);
        p.drip_mode = DripMode::HoneyFall;
        p.lifetime = static_cast<int32_t>(64.0 / (p.random.next_float() * 0.8 + 0.2));
        p.gravity = 0.01f;
        p.spawns = static_cast<int16_t>(ParticleType::LandingHoney);
        p.set_colour(0.582f, 0.448f, 0.082f);
        return p;
    }
    case ParticleType::LandingHoney: {
        Particle p = drip(pick(), seed, x, y, z, DripFluid::Empty);
        p.drip_mode = DripMode::Land;
        // `DripLandParticle` draws its own lifetime before the provider
        // replaces it, so the draw has to happen either way.
        p.lifetime = static_cast<int32_t>(16.0 / (p.random.next_float() * 0.8 + 0.2));
        p.lifetime = static_cast<int32_t>(128.0 / (level_random.next_float() * 0.8 + 0.2));
        p.set_colour(0.522f, 0.408f, 0.082f);
        return p;
    }
    case ParticleType::FallingNectar: {
        Particle p = drip(pick(), seed, x, y, z, DripFluid::Empty);
        p.drip_mode = DripMode::Falling;
        p.lifetime = static_cast<int32_t>(16.0 / (level_random.next_float() * 0.8 + 0.2));
        p.gravity = 0.007f;
        p.set_colour(0.92f, 0.782f, 0.72f);
        return p;
    }
    case ParticleType::DrippingObsidianTear: {
        Particle p = drip(pick(), seed, x, y, z, DripFluid::Empty);
        p.drip_mode = DripMode::Hang;
        p.glowing = true;
        p.gravity *= 0.02f;
        p.gravity *= 0.01f;
        p.lifetime = 100;
        p.spawns = static_cast<int16_t>(ParticleType::FallingObsidianTear);
        p.set_colour(0.51171875f, 0.03125f, 0.890625f);
        return p;
    }
    case ParticleType::FallingObsidianTear: {
        Particle p = drip(pick(), seed, x, y, z, DripFluid::Empty);
        p.drip_mode = DripMode::FallAndLand;
        p.lifetime = static_cast<int32_t>(64.0 / (p.random.next_float() * 0.8 + 0.2));
        p.glowing = true;
        p.gravity = 0.01f;
        p.spawns = static_cast<int16_t>(ParticleType::LandingObsidianTear);
        p.set_colour(0.51171875f, 0.03125f, 0.890625f);
        return p;
    }
    case ParticleType::LandingObsidianTear: {
        Particle p = drip(pick(), seed, x, y, z, DripFluid::Empty);
        p.drip_mode = DripMode::Land;
        p.glowing = true;
        p.lifetime = static_cast<int32_t>(16.0 / (p.random.next_float() * 0.8 + 0.2));
        p.lifetime = static_cast<int32_t>(28.0 / (level_random.next_float() * 0.8 + 0.2));
        p.set_colour(0.51171875f, 0.03125f, 0.890625f);
        return p;
    }
    case ParticleType::DrippingDripstoneWater: {
        Particle p = drip(pick(), seed, x, y, z, DripFluid::Water);
        p.drip_mode = DripMode::Hang;
        p.gravity *= 0.02f;
        p.lifetime = 40;
        p.spawns = static_cast<int16_t>(ParticleType::FallingDripstoneWater);
        p.set_colour(0.2f, 0.3f, 1.0f);
        return p;
    }
    case ParticleType::FallingDripstoneWater: {
        Particle p = drip(pick(), seed, x, y, z, DripFluid::Water);
        p.drip_mode = DripMode::DripstoneFall;
        p.lifetime = static_cast<int32_t>(64.0 / (p.random.next_float() * 0.8 + 0.2));
        p.spawns = static_cast<int16_t>(ParticleType::Splash);
        p.set_colour(0.2f, 0.3f, 1.0f);
        return p;
    }
    case ParticleType::DrippingDripstoneLava: {
        Particle p = drip(pick(), seed, x, y, z, DripFluid::Lava);
        p.drip_mode = DripMode::CoolingHang;
        p.gravity *= 0.02f;
        p.lifetime = 40;
        p.spawns = static_cast<int16_t>(ParticleType::FallingDripstoneLava);
        return p;
    }
    case ParticleType::FallingDripstoneLava: {
        Particle p = drip(pick(), seed, x, y, z, DripFluid::Lava);
        p.drip_mode = DripMode::DripstoneFall;
        p.lifetime = static_cast<int32_t>(64.0 / (p.random.next_float() * 0.8 + 0.2));
        p.spawns = static_cast<int16_t>(ParticleType::LandingLava);
        p.set_colour(1.0f, 0.2857143f, 0.083333336f);
        return p;
    }

    case ParticleType::None:
    case ParticleType::Count:
        break;
    }
    return std::nullopt;
}

} // namespace

// `ParticleEngine.makeParticle`: the type's provider asked for a particle.
std::optional<Particle> make_particle(const ParticleLevel& level, ParticleType type,
                                      const ParticleOptions& options, int64_t seed,
                                      double x, double y, double z, double xd, double yd, double zd,
                                      jmath::LegacyRandom& level_random,
                                      jmath::LegacyRandom& spell_random) {
    auto particle = provide(level, type, options, seed, x, y, z, xd, yd, zd, level_random, spell_random);
    // Which set the sprite came from is the type's own, whether the particle
    // walks it with age or picked one sprite and kept it.
    if (particle && particle->sprite_set < 0 && level.sprites != nullptr) {
        particle->sprite_set = level.sprites->find(particle_type_name(type));
    }
    return particle;
}

// `Camera.setRotation`: the camera's turn, as JOML's rotationYXZ builds it.
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

ParticleEngine::ParticleEngine()
    : next_seed(unique_seed), level_random(unique_seed()), spell_random(unique_seed()) {}

// `ParticleGroup.add`: in at the back of an evicting queue, the oldest out when full.
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
void ParticleEngine::Group::tick(BoxMover& mover, const ParticleLevel& level,
                                 const Particle::Emit& emit) {
    size_t kept = 0;
    for (size_t i = 0; i < size; ++i) {
        Particle& p = ring[(head + i) % kGroupLimit];
        p.tick(mover, level, emit);
        if (!p.alive()) continue;
        if (kept != i) ring[(head + kept) % kGroupLimit] = p;
        ++kept;
    }
    size = kept;
}

// `ParticleEngine.add`: queued until the next tick.
void ParticleEngine::add(const Particle& particle) { to_add_.push_back(particle); }

// `ParticleEngine.tick`: every group ticked, then the new ones put in. What a
// particle asks for as it ticks is made right away and queued with the rest.
void ParticleEngine::tick(const ParticleLevel& level) {
    if (quads_.size > 0) {
        BoxMover mover(World{level.at, level.shapes, nullptr, level.min_y});
        quads_.tick(mover, level, [&](const Particle::Spawn& spawn) {
            if (spawn.type < 0) return;
            this->spawn(level, static_cast<ParticleType>(spawn.type), {}, spawn.x, spawn.y, spawn.z,
                        spawn.xd, spawn.yd, spawn.zd);
        });
    }
    for (const auto& p : to_add_) quads_.add(p);
    to_add_.clear();
}

// `ClientLevel.doAddParticle`: the particle of this type, if we make it.
bool ParticleEngine::spawn(const ParticleLevel& level, ParticleType type,
                           const ParticleOptions& options, double x, double y, double z, double xd,
                           double yd, double zd) {
    auto particle =
        make_particle(level, type, options, next_seed(), x, y, z, xd, yd, zd, level_random, spell_random);
    if (!particle) return false;
    add(*particle);
    return true;
}

// `ClientPacketListener.handleParticleEvent`: one aimed particle, or a cloud
// of them scattered around the point by a gaussian each.
int32_t ParticleEngine::spawn_cloud(const ParticleLevel& level, ParticleType type,
                                    const ParticleOptions& options, double x, double y, double z,
                                    float x_dist, float y_dist, float z_dist, float max_speed,
                                    int32_t count) {
    if (count == 0) {
        const double sx = static_cast<double>(max_speed) * static_cast<double>(x_dist);
        const double sy = static_cast<double>(max_speed) * static_cast<double>(y_dist);
        const double sz = static_cast<double>(max_speed) * static_cast<double>(z_dist);
        return spawn(level, type, options, x, y, z, sx, sy, sz) ? 1 : 0;
    }
    int32_t made = 0;
    for (int32_t i = 0; i < count; ++i) {
        const double ox = level_random.next_gaussian() * static_cast<double>(x_dist);
        const double oy = level_random.next_gaussian() * static_cast<double>(y_dist);
        const double oz = level_random.next_gaussian() * static_cast<double>(z_dist);
        const double sx = level_random.next_gaussian() * static_cast<double>(max_speed);
        const double sy = level_random.next_gaussian() * static_cast<double>(max_speed);
        const double sz = level_random.next_gaussian() * static_cast<double>(max_speed);
        if (spawn(level, type, options, x + ox, y + oy, z + oz, sx, sy, sz)) ++made;
    }
    return made;
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
        // `NoRenderParticle`: an emitter is ticked but never drawn.
        if (p.kind == ParticleKind::ExplosionSeed) continue;
        if (!inside(p)) continue;
        ParticleQuad q{};
        q.x = static_cast<float>(lerp(p.xo, p.x) - view.camera[0]);
        q.y = static_cast<float>(lerp(p.yo, p.y) - view.camera[1]);
        q.z = static_cast<float>(lerp(p.zo, p.z) - view.camera[2]);
        std::copy(view.rotation, view.rotation + 4, q.rotation);
        if (p.roll != 0.0f) {
            // JOML's rotateZ: the particle's own roll on top of the camera's turn.
            const float angle = p.o_roll + view.partial * (p.roll - p.o_roll);
            const auto s = static_cast<float>(std::sin(static_cast<double>(angle * 0.5f)));
            const auto c = static_cast<float>(std::cos(static_cast<double>(angle * 0.5f)));
            const float* r = view.rotation;
            q.rotation[0] = r[0] * c + r[1] * s;
            q.rotation[1] = r[1] * c - r[0] * s;
            q.rotation[2] = r[3] * s + r[2] * c;
            q.rotation[3] = r[3] * c - r[2] * s;
        }
        q.size = p.size_at(view.partial);
        q.u0 = p.u0();
        q.u1 = p.u1();
        q.v0 = p.v0();
        q.v1 = p.v1();
        float rgb[3];
        p.colour_at(view.partial, rgb);
        q.colour = channel(p.alpha) << 24 | channel(rgb[0]) << 16 | channel(rgb[1]) << 8 | channel(rgb[2]);
        q.light = static_cast<uint32_t>(p.light_colour(level, view.partial));
        q.texture = p.texture;
        q.layer = static_cast<uint32_t>(p.layer);
        out.push_back(q);
    }
    std::stable_partition(out.begin(), out.end(), [](const ParticleQuad& q) {
        return q.layer == static_cast<uint32_t>(QuadLayer::Opaque);
    });
}

// `LevelEventHandler.shootParticles`: ten of them out of one face of a block.
namespace {

const int32_t kStep[6][3] = {{0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}};

} // namespace

// `LevelEventHandler.levelEvent`: what each event shows.
int32_t ParticleEngine::level_event(const ParticleLevel& level, int32_t type, const int32_t pos[3],
                                   int32_t data) {
    auto& random = level_random;
    const ParticleOptions none;
    int32_t made = 0;
    const auto one = [&](ParticleType kind, double x, double y, double z, double xd, double yd,
                         double zd) {
        if (spawn(level, kind, none, x, y, z, xd, yd, zd)) ++made;
    };
    // `LevelEventHandler.shootParticles`: ten out of one face, aimed along it.
    const auto shoot = [&](ParticleType kind) {
        const int32_t face = data & 7;
        if (face > 5) return;
        const int32_t sx = kStep[face][0];
        const int32_t sy = kStep[face][1];
        const int32_t sz = kStep[face][2];
        for (int i = 0; i < 10; ++i) {
            const double speed = random.next_double() * 0.2 + 0.01;
            const double x = pos[0] + sx * 0.6 + 0.5 + sx * 0.01 + (random.next_double() - 0.5) * sz * 0.5;
            const double y = pos[1] + sy * 0.6 + 0.5 + sy * 0.01 + (random.next_double() - 0.5) * sy * 0.5;
            const double z = pos[2] + sz * 0.6 + 0.5 + sz * 0.01 + (random.next_double() - 0.5) * sx * 0.5;
            const double xd = sx * speed + random.next_gaussian() * 0.01;
            const double yd = sy * speed + random.next_gaussian() * 0.01;
            const double zd = sz * speed + random.next_gaussian() * 0.01;
            one(kind, x, y, z, xd, yd, zd);
        }
    };

    switch (type) {
    case 1501:   // lava put out
        for (int i = 0; i < 8; ++i) {
            one(ParticleType::LargeSmoke, pos[0] + random.next_double(), pos[1] + 1.2,
                pos[2] + random.next_double(), 0.0, 0.0, 0.0);
        }
        break;
    case 1502:   // a redstone torch burning out
        for (int i = 0; i < 5; ++i) {
            one(ParticleType::Smoke, pos[0] + random.next_double() * 0.6 + 0.2,
                pos[1] + random.next_double() * 0.6 + 0.2, pos[2] + random.next_double() * 0.6 + 0.2,
                0.0, 0.0, 0.0);
        }
        break;
    case 1503:   // an end portal frame taking its eye
        for (int i = 0; i < 16; ++i) {
            one(ParticleType::Smoke, pos[0] + (5.0 + random.next_double() * 6.0) / 16.0,
                pos[1] + 0.8125, pos[2] + (5.0 + random.next_double() * 6.0) / 16.0, 0.0, 0.0, 0.0);
        }
        break;
    case 2000:   // a dispenser firing
        shoot(ParticleType::Smoke);
        break;
    case 2003: {   // an eye of ender landing
        const double x = pos[0] + 0.5;
        const double y = pos[1];
        const double z = pos[2] + 0.5;
        for (int i = 0; i < 8; ++i) {
            one(ParticleType::Item, x, y, z, random.next_gaussian() * 0.15,
                random.next_double() * 0.2, random.next_gaussian() * 0.15);
        }
        for (double angle = 0.0; angle < 6.283185307179586; angle += 0.15707963267948966) {
            const double cosine = std::cos(angle);
            const double sine = std::sin(angle);
            one(ParticleType::Portal, x + cosine * 5.0, y - 0.4, z + sine * 5.0, cosine * -5.0, 0.0,
                sine * -5.0);
            one(ParticleType::Portal, x + cosine * 5.0, y - 0.4, z + sine * 5.0, cosine * -7.0, 0.0,
                sine * -7.0);
        }
        break;
    }
    case 2004:   // a mob spawner's puff of fire
        for (int i = 0; i < 20; ++i) {
            const double x = pos[0] + 0.5 + (random.next_double() - 0.5) * 2.0;
            const double y = pos[1] + 0.5 + (random.next_double() - 0.5) * 2.0;
            const double z = pos[2] + 0.5 + (random.next_double() - 0.5) * 2.0;
            one(ParticleType::Smoke, x, y, z, 0.0, 0.0, 0.0);
            one(ParticleType::Flame, x, y, z, 0.0, 0.0, 0.0);
        }
        break;
    case 2008:   // a bed exploding in the nether, and the end crystal
        one(ParticleType::Explosion, pos[0] + 0.5, pos[1] + 0.5, pos[2] + 0.5, 0.0, 0.0, 0.0);
        break;
    case 2010:   // a crafter crafting
        shoot(ParticleType::WhiteSmoke);
        break;
    default:
        break;
    }
    return made;
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
