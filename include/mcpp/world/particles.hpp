#pragma once
// The game's particles: what each one does from tick to tick, which the game
// decides on the cpu, and what each one shows in a frame, a quad facing the
// camera. Only how the quads reach the gpu is ours (src/gpu/particles.cpp).
//
// Every particle lives in one fixed pool per render group, as many as the
// game keeps (16384); a new one past that pushes out the oldest, the way the
// game's evicting queue does. Nothing is allocated per particle.
#include "mcpp/jmath.hpp"
#include "mcpp/world/block_table.hpp"
#include "mcpp/world/physics.hpp"
#include "mcpp/world/shapes.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <vector>

namespace mcpp::world {

// What a block state's terrain particles look like, from
// tools/bake_particles.py: whether it makes any, its particle texture as an
// atlas layer, and which colour the game tints them with.
class ParticleTable {
public:
    enum class Colour : uint8_t { None, Fixed, Grass, Foliage, DryFoliage, Water };
    struct State {
        bool terrain = false;     // BlockState.shouldSpawnTerrainParticles
        bool invisible = true;    // its render shape is INVISIBLE
        Colour colour = Colour::None;
        bool below = false;       // the colour is read one block down
        uint32_t argb = 0xFFFFFFFFu;
        uint16_t texture = 0;     // the block table's texture id, which is the atlas layer
    };

    bool open(const std::filesystem::path& path, const BlockTable& table);
    bool is_open() const { return !states_.empty(); }
    const State& at(int32_t state) const {
        return state >= 0 && static_cast<size_t>(state) < states_.size()
                   ? states_[static_cast<size_t>(state)]
                   : none_;
    }
    // Which states are the grass block, which the game never tints.
    bool grass_block(int32_t state) const {
        return state >= grass_first_ && state < grass_first_ + grass_count_;
    }

    static std::filesystem::path default_path(int32_t protocol);
    // The ten crack textures, destroy_stage_0.png to destroy_stage_9.png.
    static std::filesystem::path cracks_path(int32_t protocol);

private:
    std::vector<State> states_;
    State none_;
    int32_t grass_first_ = -1;
    int32_t grass_count_ = 0;
};

// The level as a particle sees it.
struct ParticleLevel {
    const BlockAt& at;
    const BlockShapes& shapes;
    const ParticleTable& table;
    // `LevelRenderer.getLightColor` at a block: block light << 4 | sky light
    // << 20, or -1 where no chunk is loaded.
    std::function<int32_t(int32_t, int32_t, int32_t)> light;
    // A biome map's colour, 0xRRGGBB, at a block.
    std::function<uint32_t(ParticleTable::Colour, int32_t, int32_t, int32_t)> biome;
    int32_t min_y = -64;
};

// The game's SingleQuadParticle.Layer: which atlas a quad samples, and how it blends.
enum class QuadLayer : uint8_t { Terrain, Items, Opaque, Translucent };
enum class ParticleKind : uint8_t { Terrain };

// One particle: Particle, SingleQuadParticle and the subclass's own fields.
struct Particle {
    double xo = 0.0, yo = 0.0, zo = 0.0;
    double x = 0.0, y = 0.0, z = 0.0;
    double xd = 0.0, yd = 0.0, zd = 0.0;
    Aabb bb{{0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}};
    bool on_ground = false;
    bool has_physics = true;
    bool stopped_by_collision = false;
    bool removed = false;
    bool speed_up_when_y_motion_is_blocked = false;
    float bb_width = 0.6f;
    float bb_height = 1.8f;
    int32_t age = 0;
    int32_t lifetime = 0;
    float gravity = 0.0f;
    float friction = 0.98f;
    jmath::LegacyRandom random{0};

    float quad_size = 0.0f;
    float r = 1.0f, g = 1.0f, b = 1.0f, alpha = 1.0f;
    float roll = 0.0f, o_roll = 0.0f;
    uint16_t texture = 0;   // the sprite: an atlas layer, sampled across 0..1

    ParticleKind kind = ParticleKind::Terrain;
    QuadLayer layer = QuadLayer::Terrain;
    int32_t pos[3] = {0, 0, 0};   // TerrainParticle: the block it came from
    float uo = 0.0f, vo = 0.0f;

    bool alive() const { return !removed; }
    void set_power(float power);
    void scale(float by);
    void set_size(float width, float height);
    void set_pos(double x, double y, double z);
    void tick(BoxMover& mover);
    void move(BoxMover& mover, double dx, double dy, double dz);
    float u0() const;
    float u1() const;
    float v0() const;
    float v1() const;
    int32_t light_colour(const ParticleLevel& level) const;
};

// A terrain particle the way the game makes one, its random draws seeded.
Particle terrain_particle(const ParticleLevel& level, int64_t seed, double x, double y, double z,
                          double xd, double yd, double zd, int32_t state, const int32_t pos[3]);

// One quad as it goes to the gpu: the game's QuadParticleRenderState row.
struct ParticleQuad {
    float x, y, z;      // relative to the camera
    float size;
    float rotation[4];  // a quaternion, x y z w
    float u0, u1, v0, v1;
    uint32_t colour;    // ARGB
    uint32_t light;     // block light << 4 | sky light << 20
    uint32_t texture;   // atlas layer
    uint32_t layer;     // QuadLayer
};
static_assert(sizeof(ParticleQuad) == 64, "the instance layout must match the shader");

// What a frame sees: the camera, its rotation, and the planes of its view.
struct ParticleView {
    double camera[3];
    float rotation[4];
    float partial = 0.0f;
    // The four sides and the near plane, a x + b y + c z + d >= 0 inside,
    // for a point relative to `apex`.
    float planes[5][4];
    double apex[3];
};
// `Camera.setRotation`: the quaternion a camera at this yaw and pitch turns by.
void camera_rotation(float yaw, float pitch, float out[4]);

class ParticleEngine {
public:
    static constexpr size_t kGroupLimit = 16384;

    ParticleEngine();

    void add(const Particle& particle);
    void tick(const ParticleLevel& level);
    void clear();
    size_t count() const;
    // The quads of every live particle in the view, opaque layers first.
    void extract(const ParticleView& view, const ParticleLevel& level,
                 std::vector<ParticleQuad>& out) const;

    // A block broken: its shape filled with falling bits.
    void destroy(const ParticleLevel& level, const int32_t pos[3], int32_t state);
    // A block being dug: one bit off the face that is hit.
    void crack(const ParticleLevel& level, const int32_t pos[3], int face);

    // The seed of each new particle's own generator. The game's mixes the
    // clock in; a test sets its own.
    std::function<int64_t()> next_seed;
    // The level's generator, which crack draws from.
    jmath::LegacyRandom level_random;

    // In tick order, for tests.
    template <typename Visit> void each(Visit&& visit) const {
        for (size_t i = 0; i < quads_.size; ++i) visit(quads_.ring[(quads_.head + i) % kGroupLimit]);
    }

private:
    struct Group {
        std::vector<Particle> ring;
        size_t head = 0;
        size_t size = 0;
        void add(const Particle& particle);
        void tick(BoxMover& mover);
    };
    Group quads_;
    std::vector<Particle> to_add_;
};

} // namespace mcpp::world
