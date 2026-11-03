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

// Since 1.21.11 the sky, fog, clouds, sun, moon, stars, sky light, music and
// ambient sound are data. A dimension type and a biome each set some of the
// minecraft:environment_attribute entries, and the timelines the dimension
// names change them over the day. The client works out every one of them at
// the camera, at the current tick, instead of keeping curves of its own.
enum class AttributeKind : uint8_t { Float, Rgb, Argb, Bool, Text, Object };

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
    AttributeKind kind;
    // Whether a value between two keyframes, or between two biomes, is a blend
    // of both or simply the earlier one.
    bool interpolated;
};
const AttributeInfo& attribute_info(size_t id);
// kAttributeCount for a name this version does not have.
size_t attribute_id(std::string_view name);

struct AttributeValue {
    double number = 0.0;      // Float, and Bool as 0 or 1
    uint32_t argb = 0;        // Rgb with a full alpha, and Argb
    std::string text;         // Text
    nbt::Value object;        // Object, kept as it came

    bool flag() const { return number != 0.0; }
};
AttributeValue default_value(size_t id);

enum class Modifier : uint8_t {
    Override, Add, Subtract, Multiply, Minimum, Maximum, AlphaBlend, BlendToGray,
    And, Nand, Or, Nor, Xor, Xnor,
};
std::optional<Modifier> modifier_named(std::string_view name);

// What one layer does to an attribute: replace it, or combine with it.
struct AttributeLayer {
    Modifier modifier = Modifier::Override;
    AttributeValue argument;
    // alpha_blend on a number carries its weight; blend_to_gray carries a
    // brightness and a strength.
    double alpha = 1.0;
    double brightness = 1.0;
};

// What a dimension type or a biome sets; unset attributes pass through.
struct AttributeLayers {
    std::array<std::optional<AttributeLayer>, kAttributeCount> layers{};
    // Reads an "attributes" compound. Names this version does not know are
    // skipped rather than fatal, so a datapack from a later version still loads.
    static AttributeLayers parse(const nbt::Value& attributes);
};

AttributeValue apply(size_t id, const AttributeValue& value, const AttributeLayer& layer);
AttributeValue mix(size_t id, const AttributeValue& from, const AttributeValue& to, double t);

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
    double bezier[4] = {0.0, 0.0, 1.0, 1.0};

    static Ease parse(const nbt::Value* value);
    // From 0..1 of the way between two keyframes to 0..1 of the way between
    // their values.
    double operator()(double t) const;
};

struct Keyframe {
    int64_t ticks = 0;
    AttributeValue value;
};

struct Track {
    size_t attribute = kAttributeCount;
    Modifier modifier = Modifier::Override;
    Ease ease;
    std::vector<Keyframe> keyframes;

    // The track's own value at a tick, before its modifier is applied.
    AttributeValue sample(double ticks, std::optional<int64_t> period) const;
};

struct Timeline {
    std::string name;
    std::optional<int64_t> period;
    std::vector<Track> tracks;

    static Timeline parse(std::string name, const nbt::Value& body);
};

// Every attribute, worked out.
struct AttributeValues {
    std::array<AttributeValue, kAttributeCount> values;

    AttributeValues();
    float number(size_t id) const { return static_cast<float>(values[id].number); }
    uint32_t colour(size_t id) const { return values[id].argb; }
    bool flag(size_t id) const { return values[id].flag(); }
    const std::string& text(size_t id) const { return values[id].text; }
};

// One biome at the camera and how much it counts, for the attributes that
// blend across a biome edge.
struct BiomeWeight {
    size_t biome = 0;
    double weight = 1.0;
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

    // The dimension type by registry name; its timelines come along.
    bool select(std::string_view dimension_type);
    const std::string& selected() const { return selected_name_; }

    size_t timeline_count() const { return timelines_.size(); }
    const Timeline* timeline(std::string_view name) const;
    const std::vector<size_t>& active_timelines() const { return active_; }

    AttributeValues evaluate(double day_time, std::span<const BiomeWeight> biomes) const;

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
    std::vector<Dimension> dimensions_;
    std::vector<AttributeLayers> biomes_;
    std::vector<std::string> biome_names_;
    std::vector<Timeline> timelines_;
    // Timeline tags given as JSON, by name, as indices into timelines_.
    std::vector<std::pair<std::string, std::vector<size_t>>> json_tags_;

    const Dimension* dimension_ = nullptr;
    std::string selected_name_;
    std::vector<size_t> active_;
};

// JSON, as a datapack holds it, in the shape the network sends it.
nbt::Value to_nbt(const json::Value& value);

// "#rrggbb", "#aarrggbb", a packed int or a list of floats.
std::optional<uint32_t> parse_colour(const nbt::Value& value, bool with_alpha);

} // namespace mcpp::world
