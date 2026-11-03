#pragma once
#include "mcpp/nbt.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mcpp {
class Registries;
class Tags;
namespace json { class Value; }
}

namespace mcpp::world {

// The game's packed-colour arithmetic. Colours are 0xAARRGGBB in a uint32_t
// and every step rounds the way the game's integer maths does, because the
// sky and fog colours are worked out with it and a rounding of our own would
// leave the sky one step off.
namespace argb {
inline uint32_t alpha(uint32_t c) { return c >> 24; }
inline uint32_t red(uint32_t c) { return (c >> 16) & 0xFFu; }
inline uint32_t green(uint32_t c) { return (c >> 8) & 0xFFu; }
inline uint32_t blue(uint32_t c) { return c & 0xFFu; }
inline uint32_t pack(int32_t a, int32_t r, int32_t g, int32_t b) {
    return (static_cast<uint32_t>(a & 0xFF) << 24) | (static_cast<uint32_t>(r & 0xFF) << 16) |
           (static_cast<uint32_t>(g & 0xFF) << 8) | static_cast<uint32_t>(b & 0xFF);
}
inline int32_t floor_to_int(float value) {
    const auto truncated = static_cast<int32_t>(value);
    return value < static_cast<float>(truncated) ? truncated - 1 : truncated;
}
inline int32_t as_8bit(float value) { return floor_to_int(value * 255.0f); }
inline uint32_t from_floats(float a, float r, float g, float b) {
    return pack(as_8bit(a), as_8bit(r), as_8bit(g), as_8bit(b));
}
inline float red_float(uint32_t c) { return static_cast<float>(red(c)) / 255.0f; }
inline float green_float(uint32_t c) { return static_cast<float>(green(c)) / 255.0f; }
inline float blue_float(uint32_t c) { return static_cast<float>(blue(c)) / 255.0f; }
inline float alpha_float(uint32_t c) { return static_cast<float>(alpha(c)) / 255.0f; }
inline uint32_t opaque(uint32_t c) { return c | 0xFF000000u; }
inline int32_t lerp_int(float t, int32_t from, int32_t to) {
    return from + floor_to_int(t * static_cast<float>(to - from));
}
uint32_t multiply(uint32_t a, uint32_t b);
uint32_t add_rgb(uint32_t a, uint32_t b);
uint32_t subtract_rgb(uint32_t a, uint32_t b);
uint32_t scale_rgb(uint32_t c, float r, float g, float b);
uint32_t greyscale(uint32_t c);
uint32_t alpha_blend(uint32_t under, uint32_t over);
uint32_t srgb_lerp(float t, uint32_t from, uint32_t to);
} // namespace argb

// Since 1.21.11 the sky, fog, clouds, sun, moon, stars, sky light, music and
// ambient sound are data. A dimension type and a biome each set some of the
// minecraft:environment_attribute entries, the timelines the dimension names
// change them over the day, and rain and thunder change them again. The client
// works out every one of them at the camera, at the current tick, the way the
// game does, instead of keeping curves of its own.
enum class AttributeType : uint8_t {
    Boolean, TriState, Float, Angle, Rgb, Argb, MoonPhase, Other,
};

// In the registry's own order.
namespace attribute {
enum Id : uint8_t {
    FogColor, FogStartDistance, FogEndDistance, SkyFogEndDistance, CloudFogEndDistance,
    WaterFogColor, WaterFogStartDistance, WaterFogEndDistance, SkyColor, SunriseSunsetColor,
    CloudColor, CloudHeight, SunAngle, MoonAngle, StarAngle, MoonPhase, StarBrightness,
    SkyLightColor, SkyLightFactor, DefaultDripstoneParticle, AmbientParticles,
    BackgroundMusic, MusicVolume, AmbientSounds, FireflyBushSounds,
    SkyLightLevel, CanStartRaid, WaterEvaporates, BedRule, RespawnAnchorWorks,
    NetherPortalSpawnsPiglin, FastLava, IncreasedFireBurnout, EyeblossomOpen,
    TurtleEggHatchChance, PiglinsZombify, SnowGolemMelts, CreakingActive,
    SurfaceSlimeSpawnChance, CatWakingUpGiftChance, BeesStayInHive, MonstersBurn,
    CanPillagerPatrolSpawn, VillagerActivity, BabyVillagerActivity,
    Count
};
}
constexpr size_t kAttributeCount = attribute::Count;

struct AttributeInfo {
    std::string_view name;   // without the minecraft: prefix
    AttributeType type;
    // Blended across a biome edge rather than taken from the biome underfoot.
    bool spatial;
    // The range a value is clamped into after every layer; min > max for none.
    float min = 1.0f;
    float max = 0.0f;
};
const AttributeInfo& attribute_info(size_t id);
// kAttributeCount for a name this version does not have.
size_t attribute_id(std::string_view name);

// One attribute's value. Only the member its type uses is meaningful.
struct AttributeValue {
    float number = 0.0f;      // Float and Angle; Boolean as 0 or 1
    uint32_t argb = 0;        // Rgb and Argb
    std::string text;         // TriState, MoonPhase and the named ones
    nbt::Value object;        // the rest, kept as it came

    bool flag() const { return number != 0.0f; }
};
AttributeValue default_value(size_t id);

enum class Modifier : uint8_t {
    Override, AlphaBlend, Add, Subtract, Multiply, BlendToGray, Minimum, Maximum,
    And, Nand, Or, Nor, Xor, Xnor,
};
std::optional<Modifier> modifier_named(std::string_view name);

// What a modifier combines with: a value of the attribute's own type, a float
// with an alpha for alpha_blend on a float, or a brightness and a factor for
// blend_to_gray.
struct Argument {
    AttributeValue value;
    float alpha = 1.0f;
    float brightness = 1.0f;
    float factor = 1.0f;
};

struct AttributeLayer {
    Modifier modifier = Modifier::Override;
    Argument argument;
};

AttributeValue apply(size_t id, const AttributeValue& value, Modifier modifier,
                     const Argument& argument);
inline AttributeValue apply(size_t id, const AttributeValue& value, const AttributeLayer& layer) {
    return apply(id, value, layer.modifier, layer.argument);
}

// The four ways the game blends two values of an attribute: between two
// keyframes, into a weather state, across biomes, and between two ticks.
enum class Lerp : uint8_t { Keyframe, StateChange, Spatial, PartialTick };
AttributeValue lerp(size_t id, Lerp kind, float t, const AttributeValue& from,
                    const AttributeValue& to);

// What a dimension type or a biome sets; unset attributes pass through.
struct AttributeLayers {
    std::array<std::optional<AttributeLayer>, kAttributeCount> layers{};
    // Reads an "attributes" compound. Names this version does not know are
    // skipped rather than fatal, so data from a later version still loads.
    static AttributeLayers parse(const nbt::Value& attributes);
};

struct Ease {
    enum class Kind : uint8_t {
        Linear, Constant, CubicBezier,
        InBack, OutBack, InOutBack, InBounce, OutBounce, InOutBounce,
        InCirc, OutCirc, InOutCirc, InElastic, OutElastic, InOutElastic,
        InSine, OutSine, InOutSine, InQuad, OutQuad, InOutQuad,
        InCubic, OutCubic, InOutCubic, InQuart, OutQuart, InOutQuart,
        InQuint, OutQuint, InOutQuint, InExpo, OutExpo, InOutExpo,
    };
    Kind kind = Kind::Linear;
    float x1 = 0.0f, y1 = 0.0f, x2 = 1.0f, y2 = 1.0f;

    static Ease parse(const nbt::Value* value);
    static Ease bezier(float x1, float y1, float x2, float y2);
    float operator()(float t) const;
};

struct Keyframe {
    int32_t ticks = 0;
    Argument argument;
};

struct Track {
    size_t attribute = kAttributeCount;
    Modifier modifier = Modifier::Override;
    Ease ease;
    std::vector<Keyframe> keyframes;

    // The argument at a tick, before the modifier applies it.
    Argument sample(int64_t ticks, std::optional<int32_t> period) const;

private:
    friend struct Timeline;
    struct Segment {
        Ease ease;
        size_t from = 0;
        int32_t from_ticks = 0;
        size_t to = 0;
        int32_t to_ticks = 0;
    };
    std::vector<Segment> segments_;
    void bake(std::optional<int32_t> period);
};

struct Timeline {
    std::string name;
    std::optional<int32_t> period;
    std::vector<Track> tracks;

    static Timeline parse(std::string name, const nbt::Value& body);
};

// Every attribute, worked out.
struct AttributeValues {
    std::array<AttributeValue, kAttributeCount> values;

    AttributeValues();
    float number(size_t id) const { return values[id].number; }
    uint32_t colour(size_t id) const { return values[id].argb; }
    bool flag(size_t id) const { return values[id].flag(); }
    const std::string& text(size_t id) const { return values[id].text; }
};

// One biome around the camera and how much it counts, in the order the game
// meets them, which decides how the blend rounds.
struct BiomeWeight {
    size_t biome = 0;
    double weight = 1.0;
};

// Rain and thunder as the world has them this tick, each 0 to 1.
struct WeatherLevels {
    float rain = 0.0f;
    float thunder = 0.0f;
};

// The server's dimension types, biomes and timelines, ready to evaluate.
class Environment {
public:
    void load(const Registries& registries, const Tags& tags);
    // From the JSON of a datapack, which is what the server itself reads: the
    // same evaluation, checkable without a server.
    void add_timeline_json(std::string name, const json::Value& body);
    // A tag of timelines, in the order a datapack lists them; "#name" pulls in
    // a tag added before it. Add timeline tags before the dimensions naming them.
    void add_timeline_tag(std::string name, std::vector<std::string> members);
    void add_dimension_json(std::string name, const json::Value& body);
    void add_biome_json(std::string name, const json::Value& body);

    // The dimension type by registry name; its timelines come along. Weather
    // changes the sky only where the game lets it rain: a dimension with sky
    // light, no ceiling, and not the end.
    bool select(std::string_view dimension_type, bool has_weather = true);
    const std::string& selected() const { return selected_name_; }

    size_t timeline_count() const { return timelines_.size(); }
    const Timeline* timeline(std::string_view name) const;
    const std::vector<size_t>& active_timelines() const { return active_; }

    // The game's layers in the game's order: the dimension, the biomes, each
    // timeline at the day time, then rain and thunder.
    AttributeValues evaluate(int64_t day_time, std::span<const BiomeWeight> biomes,
                             size_t biome_underfoot = SIZE_MAX,
                             WeatherLevels weather = {}) const;

    // Turn a dimension's "timelines" field into timeline indices: a tag, a
    // single id, or a list of ids.
    std::vector<size_t> resolve_timelines(const nbt::Value& field, const Tags* tags) const;

private:
    struct Dimension {
        std::string name;
        AttributeLayers layers;
        nbt::Value timelines;
        std::vector<size_t> active;
    };
    struct Biome {
        AttributeLayers layers;
        // Biomes the server sent without an attributes field share one empty
        // set, and the blend across biomes counts a shared set once.
        size_t source = 0;
    };
    void add_biome(std::string name, const nbt::Value* attributes);

    std::vector<Dimension> dimensions_;
    std::vector<Biome> biomes_;
    std::vector<std::string> biome_names_;
    std::array<bool, kAttributeCount> biome_sets_{};
    std::vector<Timeline> timelines_;
    std::vector<std::pair<std::string, std::vector<size_t>>> json_tags_;

    const Dimension* dimension_ = nullptr;
    std::string selected_name_;
    std::vector<size_t> active_;
    bool weather_ = true;
};

// Values at the last two ticks, so a frame between them can blend the two the
// way the game does: smoothly for numbers and colours, the newer one for the rest.
class AttributeProbe {
public:
    void reset(const AttributeValues& values) { last_ = values; next_ = values; }
    void tick(AttributeValues values) { last_ = std::move(next_); next_ = std::move(values); }
    float number(size_t id, float partial) const;
    uint32_t colour(size_t id, float partial) const;
    const AttributeValue& value(size_t id) const { return next_.values[id]; }
    const AttributeValues& current() const { return next_; }

private:
    AttributeValues last_;
    AttributeValues next_;
};

// The 6x6x6 Gaussian over the biome cells around a point: biome_at takes cell
// (quarter-block) coordinates and returns a biome id or -1.
template <typename BiomeAt>
std::vector<BiomeWeight> gaussian_biomes(double x, double y, double z, BiomeAt&& biome_at);

// JSON, as a datapack holds it, in the shape the network sends it.
nbt::Value to_nbt(const json::Value& value);

// "#rrggbb", "#aarrggbb", a packed int or a list of floats.
std::optional<uint32_t> parse_colour(const nbt::Value& value, bool with_alpha);

int32_t moon_phase_index(std::string_view name);

template <typename BiomeAt>
std::vector<BiomeWeight> gaussian_biomes(double x, double y, double z, BiomeAt&& biome_at) {
    static constexpr double kKernel[7] = {0.0, 1.0, 4.0, 6.0, 4.0, 1.0, 0.0};
    const double qx = x * 0.25 - 0.5;
    const double qy = y * 0.25 - 0.5;
    const double qz = z * 0.25 - 0.5;
    const auto floor_int = [](double v) {
        const auto t = static_cast<int32_t>(v);
        return v < t ? t - 1 : t;
    };
    const int32_t ix = floor_int(qx);
    const int32_t iy = floor_int(qy);
    const int32_t iz = floor_int(qz);
    const double fx = qx - ix;
    const double fy = qy - iy;
    const double fz = qz - iz;
    std::vector<BiomeWeight> out;
    for (int dz = 0; dz < 6; ++dz) {
        const double wz = kKernel[dz + 1] + fz * (kKernel[dz] - kKernel[dz + 1]);
        for (int dx = 0; dx < 6; ++dx) {
            const double wx = kKernel[dx + 1] + fx * (kKernel[dx] - kKernel[dx + 1]);
            for (int dy = 0; dy < 6; ++dy) {
                const double wy = kKernel[dy + 1] + fy * (kKernel[dy] - kKernel[dy + 1]);
                const double w = wx * wy * wz;
                const int32_t biome = biome_at(ix - 2 + dx, iy - 2 + dy, iz - 2 + dz);
                if (biome < 0) continue;
                bool merged = false;
                for (auto& entry : out) {
                    if (entry.biome == static_cast<size_t>(biome)) {
                        entry.weight += w;
                        merged = true;
                        break;
                    }
                }
                if (!merged) out.push_back({static_cast<size_t>(biome), w});
            }
        }
    }
    return out;
}

} // namespace mcpp::world
