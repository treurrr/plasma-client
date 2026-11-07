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

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
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

// The sprite sets from tools/bake_particle_sprites.py: which atlas layers each
// particle type's `SpriteSet` walks, in the order the game lists them.
class ParticleSprites {
public:
    // `blocks` is the block table's texture count, which the sprites follow.
    bool open(const std::filesystem::path& path, uint32_t blocks);
    bool is_open() const { return !sets_.empty(); }

    // -1 when the game has no sprite set of that name.
    int32_t find(std::string_view name) const;
    size_t size(int32_t set) const {
        return set >= 0 && static_cast<size_t>(set) < sets_.size() ? sets_[static_cast<size_t>(set)].size() : 0;
    }
    uint16_t layer(int32_t set, size_t index) const {
        const size_t count = size(set);
        return count == 0 ? 0 : sets_[static_cast<size_t>(set)][std::min(index, count - 1)];
    }
    // `SpriteSet.get(int, int)`: how far through the set the age has walked.
    uint16_t at_age(int32_t set, int32_t age, int32_t lifetime) const {
        const size_t count = size(set);
        if (count == 0 || lifetime == 0) return layer(set, 0);
        return layer(set, static_cast<size_t>(static_cast<int64_t>(age) *
                                              static_cast<int64_t>(count - 1) / lifetime));
    }
    // `SpriteSet.first`: the set's first sprite.
    uint16_t first(int32_t set) const { return layer(set, 0); }

    static std::filesystem::path default_path(int32_t protocol);

private:
    std::vector<std::vector<uint16_t>> sets_;
    std::map<std::string, int32_t, std::less<>> names_;
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
    // Which atlas layers each particle type's sprites are; without it the
    // sprite-based kinds have nothing to draw and are not made.
    const ParticleSprites* sprites = nullptr;
};

// The game's SingleQuadParticle.Layer: which atlas a quad samples, and how it blends.
enum class QuadLayer : uint8_t { Terrain, Items, Opaque, Translucent };

// One per particle class whose tick, size or light differs from the base.
// Several of the game's classes share one: the ash, smoke and campfire smokes
// are all `BaseAshSmokeParticle` with other constructor arguments, and every
// flavour of drip is `DripParticle` with another mode.
enum class ParticleKind : uint8_t {
    Terrain,
    AshSmoke,      // BaseAshSmokeParticle: smoke, large smoke, ash, dust plume
    CampfireSmoke, // CampfireSmokeParticle: the cosy and signal columns
    Rising,        // RisingParticle: flame, small flame, soul fire flame
    Explode,       // ExplodeParticle: poof
    HugeExplosion, // HugeExplosionParticle: explosion
    ExplosionSeed, // HugeExplosionSeedParticle: explosion_emitter
    Crit,          // CritParticle: crit, enchanted hit, damage indicator
    BreakingItem,  // BreakingItemParticle: item, item_slime, item_snowball
    Spell,         // SpellParticle: effect, entity effect, witch, omen
    Animated,      // SimpleAnimatedParticle: totem of undying
    WaterDrop,     // WaterDropParticle and SplashParticle: rain, splash
    Bubble,        // BubbleParticle
    BubbleColumn,  // BubbleColumnUpParticle
    BubblePop,     // BubblePopParticle
    Note,          // NoteParticle
    Heart,         // HeartParticle
    Portal,        // PortalParticle
    ReversePortal, // ReversePortalParticle
    Dust,          // DustParticleBase: redstone dust
    DustTransition,// DustColorTransitionParticle, which lerps as it is drawn
    DustPlume,     // DustPlumeParticle, whose fall and drag ease off
    Drip,          // DripParticle in all its modes
};

// `DripParticle`'s subclasses: what a drop does before and after it moves.
enum class DripMode : uint8_t { Hang, CoolingHang, Falling, FallAndLand, DripstoneFall, HoneyFall, Land };
// The fluid a drip watches for, which ends it when it reaches the surface.
enum class DripFluid : uint8_t { Empty, Water, Lava };

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

    // The sprite set it walks as it ages, -1 for a sprite that never changes.
    int32_t sprite_set = -1;
    // PortalParticle: where it set out from, which its whole path is measured
    // against. ExplosionSeed also keeps its centre here.
    double start[3] = {0.0, 0.0, 0.0};
    // SimpleAnimatedParticle's fade, and whether it has one. The colour
    // transition dust keeps the colour it starts from here too, because its
    // own r, g and b stay white until it is first drawn.
    float fade_r = 0.0f, fade_g = 0.0f, fade_b = 0.0f;
    float from_r = 1.0f, from_g = 1.0f, from_b = 1.0f;
    bool has_fade = false;
    // SpellParticle: the alpha it returns to once nothing hides it.
    float original_alpha = 1.0f;
    // DripParticle: what it does around its move, what it watches, and
    // whether it lights itself.
    DripMode drip_mode = DripMode::Falling;
    DripFluid drip_fluid = DripFluid::Empty;
    bool glowing = false;
    // The particle this one leaves behind: a hanging drop's fall, a landing
    // drop's splash, an explosion seed's explosions. -1 for none.
    int16_t spawns = -1;
    // HugeExplosionParticle: how far through its emitter's life it was made,
    // which fixes its size.
    float seed_age = 0.0f;

    bool alive() const { return !removed; }
    void set_power(float power);
    void scale(float by);
    void set_size(float width, float height);
    void set_pos(double x, double y, double z);
    void set_colour(float r, float g, float b);
    // `SingleQuadParticle.setSpriteFromAge`: the sprite the age has reached.
    void sprite_from_age(const ParticleSprites& sprites);
    // What a particle asks for as it ticks: a hanging drop's fall, a landing
    // drop's splash, an explosion seed's explosions.
    struct Spawn {
        int16_t type = -1;
        double x = 0.0, y = 0.0, z = 0.0;
        double xd = 0.0, yd = 0.0, zd = 0.0;
    };
    using Emit = std::function<void(const Spawn&)>;
    void tick(BoxMover& mover, const ParticleLevel& level, const Emit& emit);
    void move(BoxMover& mover, double dx, double dy, double dz);
    float u0() const;
    float u1() const;
    float v0() const;
    float v1() const;
    // `SingleQuadParticle.getQuadSize`: the size this far between two ticks,
    // which several classes grow or shrink with age.
    float size_at(float partial) const;
    // The colour it is drawn in, which only the colour-transition dust moves
    // between two ticks.
    void colour_at(float partial, float out[3]) const;
    int32_t light_colour(const ParticleLevel& level, float partial) const;

private:
    void base_tick(BoxMover& mover);
};

// A terrain particle the way the game makes one, its random draws seeded.
Particle terrain_particle(const ParticleLevel& level, int64_t seed, double x, double y, double z,
                          double xd, double yd, double zd, int32_t state, const int32_t pos[3]);

// The particle types we make, named as `ParticleTypes` registers them. The
// order is ours; the server's own ids are looked up by name.
enum class ParticleType : int16_t {
    None = -1,
    Smoke, LargeSmoke, WhiteSmoke, CampfireCosySmoke, CampfireSignalSmoke, Ash, WhiteAsh,
    Flame, SmallFlame, SoulFireFlame, CopperFireFlame,
    Poof, Explosion, ExplosionEmitter,
    Crit, EnchantedHit, DamageIndicator,
    Item, ItemSlime, ItemSnowball, ItemCobweb, Block, BlockCrumble, FallingDust, DustPillar,
    Effect, InstantEffect, EntityEffect, Witch, Infested, RaidOmen, TrialOmen,
    Splash, Rain, Bubble, BubbleColumnUp, BubblePop,
    Note, Heart, AngryVillager, TotemOfUndying, Portal, ReversePortal,
    Dust, DustColorTransition, DustPlume,
    DrippingWater, FallingWater, DrippingLava, FallingLava, LandingLava,
    DrippingHoney, FallingHoney, LandingHoney, FallingNectar,
    DrippingObsidianTear, FallingObsidianTear, LandingObsidianTear,
    DrippingDripstoneWater, FallingDripstoneWater, DrippingDripstoneLava, FallingDripstoneLava,
    Count
};

// The registry name, so a server's id can be matched to it; empty past the
// end. It is also the name of the type's sprite set.
std::string_view particle_type_name(ParticleType type);
// -1 when it is a type we do not make.
ParticleType particle_type_from_name(std::string_view name);

// What the packet carries beside the type: the fields the game's
// `ParticleOptions` subclasses add.
struct ParticleOptions {
    int32_t block_state = -1;   // BlockParticleOption
    uint32_t colour = 0xFFFFFFFFu;   // ColorParticleOption, DustParticleOptions: ARGB
    uint32_t to_colour = 0xFFFFFFFFu;   // DustColorTransitionOptions
    float scale = 1.0f;         // ScalableParticleOptionsBase
    float power = 1.0f;         // SpellParticleOption, PowerParticleOption
    // BreakingItemParticle: the sprite its item resolves to, and which atlas
    // that sprite is on. No texture means the item has no icon we can draw.
    bool has_texture = false;
    uint16_t texture = 0;
    QuadLayer texture_layer = QuadLayer::Items;
};

// `ParticleProvider.createParticle` for every type above: the particle the
// game would make, or nothing for a type with no provider of ours. `seed`
// seeds the particle's own generator, `level_random` is the engine's, which
// the providers draw a sprite and a colour from.
std::optional<Particle> make_particle(const ParticleLevel& level, ParticleType type,
                                      const ParticleOptions& options, int64_t seed,
                                      double x, double y, double z, double xd, double yd, double zd,
                                      jmath::LegacyRandom& level_random,
                                      jmath::LegacyRandom& spell_random);

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
    // `ClientLevel.doAddParticle`: one particle of a type, if we make that
    // type at all. True when one was put in.
    bool spawn(const ParticleLevel& level, ParticleType type, const ParticleOptions& options,
               double x, double y, double z, double xd, double yd, double zd);
    // `ClientPacketListener.handleParticleEvent`: the level_particles packet,
    // which is either one particle aimed by the speed or a cloud of `count`
    // scattered around the point. Returns how many went in.
    int32_t spawn_cloud(const ParticleLevel& level, ParticleType type, const ParticleOptions& options,
                        double x, double y, double z, float x_dist, float y_dist, float z_dist,
                        float max_speed, int32_t count);
    // `LevelEventHandler.levelEvent`: the particles a level_event shows. The
    // sounds are src/audio/level_events.cpp's; this is only what is seen.
    // Returns how many went in, zero for an event that shows none.
    int32_t level_event(const ParticleLevel& level, int32_t type, const int32_t pos[3], int32_t data);

    // The seed of each new particle's own generator. The game's mixes the
    // clock in; a test sets its own.
    std::function<int64_t()> next_seed;
    // The level's generator, which crack draws from.
    jmath::LegacyRandom level_random;
    // The static generator SpellParticle keeps to itself, which nothing else
    // in the game draws from, so it is a stream of its own here as well.
    jmath::LegacyRandom spell_random;

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
        void tick(BoxMover& mover, const ParticleLevel& level, const Particle::Emit& emit);
    };
    Group quads_;
    std::vector<Particle> to_add_;
};

} // namespace mcpp::world
