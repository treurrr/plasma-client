#include "mcpp/world/environment.hpp"

#include "mcpp/jmath.hpp"
#include "mcpp/json.hpp"
#include "mcpp/registries.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <numbers>

namespace mcpp::world {

namespace argb {

uint32_t multiply(uint32_t a, uint32_t b) {
    if (a == 0xFFFFFFFFu) return b;
    if (b == 0xFFFFFFFFu) return a;
    return pack(static_cast<int32_t>(alpha(a) * alpha(b) / 255), static_cast<int32_t>(red(a) * red(b) / 255),
                static_cast<int32_t>(green(a) * green(b) / 255),
                static_cast<int32_t>(blue(a) * blue(b) / 255));
}

uint32_t add_rgb(uint32_t a, uint32_t b) {
    return pack(static_cast<int32_t>(alpha(a)), static_cast<int32_t>(std::min(red(a) + red(b), 255u)),
                static_cast<int32_t>(std::min(green(a) + green(b), 255u)),
                static_cast<int32_t>(std::min(blue(a) + blue(b), 255u)));
}

uint32_t subtract_rgb(uint32_t a, uint32_t b) {
    const auto sub = [](uint32_t x, uint32_t y) {
        return std::max(static_cast<int32_t>(x) - static_cast<int32_t>(y), 0);
    };
    return pack(static_cast<int32_t>(alpha(a)), sub(red(a), red(b)), sub(green(a), green(b)),
                sub(blue(a), blue(b)));
}

uint32_t scale_rgb(uint32_t c, float r, float g, float b) {
    const auto scaled = [](uint32_t channel, float by) {
        return std::clamp(static_cast<int32_t>(static_cast<float>(channel) * by), 0, 255);
    };
    return pack(static_cast<int32_t>(alpha(c)), scaled(red(c), r), scaled(green(c), g),
                scaled(blue(c), b));
}

uint32_t greyscale(uint32_t c) {
    const auto grey = static_cast<int32_t>(static_cast<float>(red(c)) * 0.3f +
                                           static_cast<float>(green(c)) * 0.59f +
                                           static_cast<float>(blue(c)) * 0.11f);
    return pack(static_cast<int32_t>(alpha(c)), grey, grey, grey);
}

uint32_t alpha_blend(uint32_t under, uint32_t over) {
    const auto a_under = static_cast<int32_t>(alpha(under));
    const auto a_over = static_cast<int32_t>(alpha(over));
    if (a_over == 255) return over;
    if (a_over == 0) return under;
    const int32_t total = a_over + a_under * (255 - a_over) / 255;
    const auto channel = [&](uint32_t x, uint32_t y) {
        return (static_cast<int32_t>(y) * a_over + static_cast<int32_t>(x) * (total - a_over)) / total;
    };
    return pack(total, channel(red(under), red(over)), channel(green(under), green(over)),
                channel(blue(under), blue(over)));
}

uint32_t srgb_lerp(float t, uint32_t from, uint32_t to) {
    const auto c = [&](uint32_t x, uint32_t y) {
        return lerp_int(t, static_cast<int32_t>(x), static_cast<int32_t>(y));
    };
    return pack(c(alpha(from), alpha(to)), c(red(from), red(to)), c(green(from), green(to)),
                c(blue(from), blue(to)));
}

} // namespace argb

namespace {

using Type = AttributeType;
constexpr float kInf = std::numeric_limits<float>::infinity();

constexpr AttributeInfo kAttributes[kAttributeCount] = {
    {"visual/fog_color", Type::Rgb, true},
    {"visual/fog_start_distance", Type::Float, true},
    {"visual/fog_end_distance", Type::Float, true, 0.0f, kInf},
    {"visual/sky_fog_end_distance", Type::Float, true, 0.0f, kInf},
    {"visual/cloud_fog_end_distance", Type::Float, true, 0.0f, kInf},
    {"visual/water_fog_color", Type::Rgb, true},
    {"visual/water_fog_start_distance", Type::Float, true},
    {"visual/water_fog_end_distance", Type::Float, true, 0.0f, kInf},
    {"visual/sky_color", Type::Rgb, true},
    {"visual/sunrise_sunset_color", Type::Argb, true},
    {"visual/cloud_color", Type::Argb, true},
    {"visual/cloud_height", Type::Float, true},
    {"visual/sun_angle", Type::Angle, true},
    {"visual/moon_angle", Type::Angle, true},
    {"visual/star_angle", Type::Angle, true},
    {"visual/moon_phase", Type::MoonPhase, false},
    {"visual/star_brightness", Type::Float, true, 0.0f, 1.0f},
    {"visual/sky_light_color", Type::Rgb, true},
    {"visual/sky_light_factor", Type::Float, true, 0.0f, 1.0f},
    {"visual/default_dripstone_particle", Type::Other, false},
    {"visual/ambient_particles", Type::Other, false},
    {"audio/background_music", Type::Other, false},
    {"audio/music_volume", Type::Float, false, 0.0f, 1.0f},
    {"audio/ambient_sounds", Type::Other, false},
    {"audio/firefly_bush_sounds", Type::Boolean, false},
    {"gameplay/sky_light_level", Type::Float, false, 0.0f, 15.0f},
    {"gameplay/can_start_raid", Type::Boolean, false},
    {"gameplay/water_evaporates", Type::Boolean, false},
    {"gameplay/bed_rule", Type::Other, false},
    {"gameplay/respawn_anchor_works", Type::Boolean, false},
    {"gameplay/nether_portal_spawns_piglin", Type::Boolean, false},
    {"gameplay/fast_lava", Type::Boolean, false},
    {"gameplay/increased_fire_burnout", Type::Boolean, false},
    {"gameplay/eyeblossom_open", Type::TriState, false},
    {"gameplay/turtle_egg_hatch_chance", Type::Float, false, 0.0f, 1.0f},
    {"gameplay/piglins_zombify", Type::Boolean, false},
    {"gameplay/snow_golem_melts", Type::Boolean, false},
    {"gameplay/creaking_active", Type::Boolean, false},
    {"gameplay/surface_slime_spawn_chance", Type::Float, false, 0.0f, 1.0f},
    {"gameplay/cat_waking_up_gift_chance", Type::Float, false, 0.0f, 1.0f},
    {"gameplay/bees_stay_in_hive", Type::Boolean, false},
    {"gameplay/monsters_burn", Type::Boolean, false},
    {"gameplay/can_pillager_patrol_spawn", Type::Boolean, false},
    {"gameplay/villager_activity", Type::Other, false},
    {"gameplay/baby_villager_activity", Type::Other, false},
};

bool is_colour(size_t id) {
    return kAttributes[id].type == Type::Rgb || kAttributes[id].type == Type::Argb;
}
bool is_number(size_t id) {
    return kAttributes[id].type == Type::Float || kAttributes[id].type == Type::Angle;
}

std::optional<float> number_of(const nbt::Value& value) {
    if (const auto number = value.as_double()) return static_cast<float>(*number);
    if (const auto number = value.as_int()) return static_cast<float>(*number);
    return std::nullopt;
}

std::optional<bool> bool_of(const nbt::Value& value) {
    if (const auto number = value.as_int()) return *number != 0;
    if (const auto* text = value.as_string()) {
        if (*text == "true") return true;
        if (*text == "false") return false;
    }
    return std::nullopt;
}

std::string_view strip_namespace(std::string_view name) {
    if (name.starts_with('#')) name.remove_prefix(1);
    if (name.starts_with("minecraft:")) name.remove_prefix(10);
    return name;
}

float wrap_degrees(float degrees) {
    float wrapped = std::fmod(degrees, 360.0f);
    if (wrapped >= 180.0f) wrapped -= 360.0f;
    if (wrapped < -180.0f) wrapped += 360.0f;
    return wrapped;
}

float lerp_float(float t, float from, float to) { return from + t * (to - from); }

// A value as the attribute's own codec reads it; nullopt if the shape is wrong.
std::optional<AttributeValue> read_value(size_t id, const nbt::Value& raw) {
    AttributeValue value = default_value(id);
    switch (kAttributes[id].type) {
    case Type::Float:
    case Type::Angle: {
        const auto number = number_of(raw);
        if (!number || raw.as_compound() != nullptr) return std::nullopt;
        value.number = *number;
        return value;
    }
    case Type::Boolean: {
        const auto flag = bool_of(raw);
        if (!flag) return std::nullopt;
        value.number = *flag ? 1.0f : 0.0f;
        return value;
    }
    case Type::Rgb:
    case Type::Argb: {
        const auto colour = parse_colour(raw, kAttributes[id].type == Type::Argb);
        if (!colour) return std::nullopt;
        value.argb = *colour;
        return value;
    }
    case Type::TriState:
        if (const auto flag = raw.as_int()) {
            value.text = *flag != 0 ? "true" : "false";
            return value;
        }
        [[fallthrough]];
    case Type::MoonPhase:
        if (const auto* text = raw.as_string()) {
            value.text = std::string(strip_namespace(*text));
            return value;
        }
        return std::nullopt;
    case Type::Other:
        if (const auto* text = raw.as_string()) {
            value.text = std::string(strip_namespace(*text));
        }
        value.object = raw;
        return value;
    }
    return std::nullopt;
}

// The argument of a modifier, in the shape that modifier's codec reads.
std::optional<Argument> read_argument(size_t id, Modifier modifier, const nbt::Value& raw) {
    Argument argument;
    argument.value = default_value(id);
    if (modifier == Modifier::Override) {
        auto value = read_value(id, raw);
        if (!value) return std::nullopt;
        argument.value = std::move(*value);
        return argument;
    }
    if (is_number(id)) {
        if (modifier == Modifier::AlphaBlend && raw.as_compound() != nullptr) {
            const auto* value = raw.find("value");
            if (value == nullptr) return std::nullopt;
            argument.value.number = number_of(*value).value_or(0.0f);
            if (const auto* alpha = raw.find("alpha")) argument.alpha = number_of(*alpha).value_or(1.0f);
            return argument;
        }
        const auto number = number_of(raw);
        if (!number) return std::nullopt;
        argument.value.number = *number;
        return argument;
    }
    if (is_colour(id)) {
        if (modifier == Modifier::BlendToGray) {
            const auto* brightness = raw.find("brightness");
            const auto* factor = raw.find("factor");
            if (brightness == nullptr || factor == nullptr) return std::nullopt;
            argument.brightness = number_of(*brightness).value_or(1.0f);
            argument.factor = number_of(*factor).value_or(1.0f);
            return argument;
        }
        const auto colour = parse_colour(raw, true);
        if (!colour) return std::nullopt;
        argument.value.argb = *colour;
        return argument;
    }
    if (kAttributes[id].type == Type::Boolean) {
        const auto flag = bool_of(raw);
        if (!flag) return std::nullopt;
        argument.value.number = *flag ? 1.0f : 0.0f;
        return argument;
    }
    return std::nullopt;
}

Argument lerp_argument(size_t id, Modifier modifier, float t, const Argument& from,
                       const Argument& to) {
    if (modifier == Modifier::Override) {
        Argument out = from;
        out.value = lerp(id, Lerp::Keyframe, t, from.value, to.value);
        return out;
    }
    Argument out = from;
    if (is_number(id)) {
        out.value.number = lerp_float(t, from.value.number, to.value.number);
        if (modifier == Modifier::AlphaBlend) out.alpha = lerp_float(t, from.alpha, to.alpha);
    } else if (is_colour(id)) {
        if (modifier == Modifier::BlendToGray) {
            out.brightness = lerp_float(t, from.brightness, to.brightness);
            out.factor = lerp_float(t, from.factor, to.factor);
        } else {
            out.value.argb = argb::srgb_lerp(t, from.value.argb, to.value.argb);
        }
    }
    return out;
}

AttributeValue sanitise(size_t id, AttributeValue value) {
    const auto& info = kAttributes[id];
    if (info.min <= info.max && is_number(id)) {
        if (!(value.number >= info.min && value.number <= info.max)) {
            value.number = value.number < info.min ? info.min : std::min(value.number, info.max);
        }
    }
    return value;
}

float bounce_out(float x) {
    if (x < 0.36363637f) return 7.5625f * x * x;
    if (x < 0.72727275f) {
        const float d = x - 0.54545456f;
        return 7.5625f * d * d + 0.75f;
    }
    if (static_cast<double>(x) < 0.9090909090909091) {
        const float d = x - 0.8181818f;
        return 7.5625f * d * d + 0.9375f;
    }
    const float d = x - 0.95454544f;
    return 7.5625f * d * d + 0.984375f;
}

float square(float x) { return x * x; }
float cube(float x) { return x * x * x; }

// Rain and thunder, as the game layers them over everything else.
struct WeatherLayer {
    size_t id;
    AttributeLayer rain;
    AttributeLayer thunder;
};

AttributeLayer colour_layer(Modifier modifier, uint32_t colour) {
    AttributeLayer layer;
    layer.modifier = modifier;
    layer.argument.value.argb = colour;
    return layer;
}

AttributeLayer grey_layer(float brightness, float factor) {
    AttributeLayer layer;
    layer.modifier = Modifier::BlendToGray;
    layer.argument.brightness = brightness;
    layer.argument.factor = factor;
    return layer;
}

AttributeLayer float_layer(Modifier modifier, float value, float alpha = 1.0f) {
    AttributeLayer layer;
    layer.modifier = modifier;
    layer.argument.value.number = value;
    layer.argument.alpha = alpha;
    return layer;
}

const std::vector<WeatherLayer>& weather_layers() {
    static const std::vector<WeatherLayer> layers = [] {
        using namespace attribute;
        const uint32_t night_light = argb::from_floats(1.0f, 0.48f, 0.48f, 1.0f);
        const auto with_alpha = [](float alpha, uint32_t rgb) {
            return (static_cast<uint32_t>(argb::as_8bit(alpha)) << 24) | (rgb & 0xFFFFFFu);
        };
        std::vector<WeatherLayer> out;
        out.push_back({SkyColor, grey_layer(0.6f, 0.75f), grey_layer(0.24f, 0.94f)});
        out.push_back({FogColor,
                       colour_layer(Modifier::Multiply, argb::from_floats(1.0f, 0.5f, 0.5f, 0.6f)),
                       colour_layer(Modifier::Multiply, argb::from_floats(1.0f, 0.25f, 0.25f, 0.3f))});
        out.push_back({CloudColor, grey_layer(0.24f, 0.5f), grey_layer(0.095f, 0.94f)});
        out.push_back({SkyLightLevel, float_layer(Modifier::AlphaBlend, 4.0f, 0.3125f),
                       float_layer(Modifier::AlphaBlend, 4.0f, 0.52734375f)});
        out.push_back({SkyLightColor,
                       colour_layer(Modifier::AlphaBlend, with_alpha(0.3125f, night_light)),
                       colour_layer(Modifier::AlphaBlend, with_alpha(0.52734375f, night_light))});
        out.push_back({SkyLightFactor, float_layer(Modifier::AlphaBlend, 0.24f, 0.3125f),
                       float_layer(Modifier::AlphaBlend, 0.24f, 0.52734375f)});
        out.push_back({StarBrightness, float_layer(Modifier::Override, 0.0f),
                       float_layer(Modifier::Override, 0.0f)});
        out.push_back({SunriseSunsetColor,
                       colour_layer(Modifier::Multiply, argb::from_floats(1.0f, 0.5f, 0.5f, 0.6f)),
                       colour_layer(Modifier::Multiply, argb::from_floats(1.0f, 0.25f, 0.25f, 0.3f))});
        out.push_back({BeesStayInHive, float_layer(Modifier::Override, 1.0f),
                       float_layer(Modifier::Override, 1.0f)});
        return out;
    }();
    return layers;
}

} // namespace

const AttributeInfo& attribute_info(size_t id) { return kAttributes[id]; }

size_t attribute_id(std::string_view name) {
    name = strip_namespace(name);
    for (size_t i = 0; i < kAttributeCount; ++i) {
        if (kAttributes[i].name == name) return i;
    }
    return kAttributeCount;
}

AttributeValue default_value(size_t id) {
    using namespace attribute;
    AttributeValue value;
    switch (id) {
    case FogEndDistance: value.number = 1024.0f; break;
    case SkyFogEndDistance: value.number = 512.0f; break;
    case CloudFogEndDistance: value.number = 2048.0f; break;
    case WaterFogColor: value.argb = 0xFF050533u; break;
    case WaterFogStartDistance: value.number = -8.0f; break;
    case WaterFogEndDistance: value.number = 96.0f; break;
    case CloudHeight: value.number = 192.33f; break;
    case MoonPhase: value.text = "full_moon"; break;
    case SkyLightColor: value.argb = 0xFFFFFFFFu; break;
    case SkyLightFactor: value.number = 1.0f; break;
    case MusicVolume: value.number = 1.0f; break;
    case SkyLightLevel: value.number = 15.0f; break;
    case CanStartRaid: case PiglinsZombify: case CanPillagerPatrolSpawn: value.number = 1.0f; break;
    case EyeblossomOpen: value.text = "default"; break;
    case VillagerActivity: case BabyVillagerActivity: value.text = "idle"; break;
    case DefaultDripstoneParticle: value.text = "dripping_dripstone_water"; break;
    default: break;
    }
    return value;
}

std::optional<uint32_t> parse_colour(const nbt::Value& value, bool with_alpha) {
    if (const auto number = value.as_int()) return static_cast<uint32_t>(*number);
    if (const auto* list = value.as_list()) {
        const size_t want = with_alpha ? 4 : 3;
        if (list->size() != want) return std::nullopt;
        float parts[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        for (size_t i = 0; i < want; ++i) parts[i] = number_of((*list)[i]).value_or(0.0f);
        return argb::from_floats(parts[3], parts[0], parts[1], parts[2]);
    }
    const std::string* text = value.as_string();
    if (text == nullptr || !text->starts_with('#')) return std::nullopt;
    const std::string_view digits = std::string_view(*text).substr(1);
    if (digits.size() != 6 && digits.size() != 8) return std::nullopt;
    uint32_t packed = 0;
    const char* end = digits.data() + digits.size();
    const auto parsed = std::from_chars(digits.data(), end, packed, 16);
    if (parsed.ec != std::errc{} || parsed.ptr != end) return std::nullopt;
    return digits.size() == 6 ? argb::opaque(packed) : packed;
}

std::optional<Modifier> modifier_named(std::string_view name) {
    static constexpr std::pair<std::string_view, Modifier> kNames[] = {
        {"override", Modifier::Override}, {"alpha_blend", Modifier::AlphaBlend},
        {"add", Modifier::Add}, {"subtract", Modifier::Subtract},
        {"multiply", Modifier::Multiply}, {"blend_to_gray", Modifier::BlendToGray},
        {"minimum", Modifier::Minimum}, {"maximum", Modifier::Maximum},
        {"and", Modifier::And}, {"nand", Modifier::Nand}, {"or", Modifier::Or},
        {"nor", Modifier::Nor}, {"xor", Modifier::Xor}, {"xnor", Modifier::Xnor},
    };
    name = strip_namespace(name);
    for (const auto& [text, modifier] : kNames) {
        if (text == name) return modifier;
    }
    return std::nullopt;
}

int32_t moon_phase_index(std::string_view name) {
    static constexpr std::string_view kPhases[8] = {
        "full_moon", "waning_gibbous", "third_quarter", "waning_crescent",
        "new_moon", "waxing_crescent", "first_quarter", "waxing_gibbous",
    };
    for (int32_t i = 0; i < 8; ++i) {
        if (kPhases[i] == name) return i;
    }
    return 0;
}

AttributeValue apply(size_t id, const AttributeValue& value, Modifier modifier,
                     const Argument& argument) {
    if (modifier == Modifier::Override) return argument.value;
    AttributeValue out = value;
    const AttributeValue& arg = argument.value;
    if (is_number(id)) {
        const float x = value.number;
        const float y = arg.number;
        switch (modifier) {
        case Modifier::Add: out.number = x + y; break;
        case Modifier::Subtract: out.number = x - y; break;
        case Modifier::Multiply: out.number = x * y; break;
        case Modifier::Minimum: out.number = std::min(x, y); break;
        case Modifier::Maximum: out.number = std::max(x, y); break;
        case Modifier::AlphaBlend: out.number = lerp_float(argument.alpha, x, y); break;
        default: break;
        }
        return out;
    }
    if (is_colour(id)) {
        switch (modifier) {
        case Modifier::AlphaBlend: out.argb = argb::alpha_blend(value.argb, arg.argb); break;
        case Modifier::Add: out.argb = argb::add_rgb(value.argb, arg.argb); break;
        case Modifier::Subtract: out.argb = argb::subtract_rgb(value.argb, arg.argb); break;
        case Modifier::Multiply: out.argb = argb::multiply(value.argb, arg.argb); break;
        case Modifier::BlendToGray: {
            const uint32_t grey = argb::greyscale(value.argb);
            const float b = argument.brightness;
            out.argb = argb::srgb_lerp(argument.factor, value.argb, argb::scale_rgb(grey, b, b, b));
            break;
        }
        default: break;
        }
        return out;
    }
    if (kAttributes[id].type == Type::Boolean) {
        const bool x = value.flag();
        const bool y = arg.flag();
        bool result = x;
        switch (modifier) {
        case Modifier::And: result = y && x; break;
        case Modifier::Nand: result = !y || !x; break;
        case Modifier::Or: result = y || x; break;
        case Modifier::Nor: result = !y && !x; break;
        case Modifier::Xor: result = y != x; break;
        case Modifier::Xnor: result = y == x; break;
        default: break;
        }
        out.number = result ? 1.0f : 0.0f;
    }
    return out;
}

AttributeValue lerp(size_t id, Lerp kind, float t, const AttributeValue& from,
                    const AttributeValue& to) {
    switch (kAttributes[id].type) {
    case Type::Float: {
        AttributeValue out = from;
        out.number = lerp_float(t, from.number, to.number);
        return out;
    }
    case Type::Angle: {
        AttributeValue out = from;
        if (kind == Lerp::PartialTick) {
            const float delta = wrap_degrees(to.number - from.number);
            out.number = std::abs(delta) >= 90.0f ? to.number : from.number + t * delta;
        } else {
            out.number = lerp_float(t, from.number, to.number);
        }
        return out;
    }
    case Type::Rgb:
    case Type::Argb: {
        AttributeValue out = from;
        out.argb = argb::srgb_lerp(t, from.argb, to.argb);
        return out;
    }
    default: {
        const float step = kind == Lerp::Keyframe ? 1.0f : kind == Lerp::Spatial ? 0.5f : 0.0f;
        return t >= step ? to : from;
    }
    }
}

AttributeLayers AttributeLayers::parse(const nbt::Value& attributes) {
    AttributeLayers out;
    const auto* compound = attributes.as_compound();
    if (compound == nullptr) return out;
    for (const auto& [name, raw] : *compound) {
        const size_t id = attribute_id(name);
        if (id == kAttributeCount) continue;
        if (auto value = read_value(id, raw)) {
            AttributeLayer layer;
            layer.argument.value = std::move(*value);
            out.layers[id] = std::move(layer);
            continue;
        }
        const auto* modifier_value = raw.find("modifier");
        const auto* argument_value = raw.find("argument");
        if (modifier_value == nullptr || argument_value == nullptr) continue;
        const auto* modifier_name = modifier_value->as_string();
        const auto modifier = modifier_name != nullptr ? modifier_named(*modifier_name) : std::nullopt;
        if (!modifier) continue;
        auto argument = read_argument(id, *modifier, *argument_value);
        if (!argument) continue;
        out.layers[id] = AttributeLayer{*modifier, std::move(*argument)};
    }
    return out;
}

Ease Ease::bezier(float x1, float y1, float x2, float y2) {
    Ease ease;
    ease.kind = Kind::CubicBezier;
    ease.x1 = x1;
    ease.y1 = y1;
    ease.x2 = x2;
    ease.y2 = y2;
    return ease;
}

Ease Ease::parse(const nbt::Value* value) {
    Ease ease;
    if (value == nullptr) return ease;
    if (const auto* points = value->find("cubic_bezier")) {
        if (const auto* list = points->as_list(); list != nullptr && list->size() == 4) {
            return bezier(number_of((*list)[0]).value_or(0.0f), number_of((*list)[1]).value_or(0.0f),
                          number_of((*list)[2]).value_or(1.0f), number_of((*list)[3]).value_or(1.0f));
        }
        return ease;
    }
    const std::string* name = value->as_string();
    if (name == nullptr) return ease;
    static constexpr std::pair<std::string_view, Kind> kNames[] = {
        {"linear", Kind::Linear}, {"constant", Kind::Constant},
        {"in_back", Kind::InBack}, {"out_back", Kind::OutBack}, {"in_out_back", Kind::InOutBack},
        {"in_bounce", Kind::InBounce}, {"out_bounce", Kind::OutBounce},
        {"in_out_bounce", Kind::InOutBounce}, {"in_circ", Kind::InCirc},
        {"out_circ", Kind::OutCirc}, {"in_out_circ", Kind::InOutCirc},
        {"in_elastic", Kind::InElastic}, {"out_elastic", Kind::OutElastic},
        {"in_out_elastic", Kind::InOutElastic}, {"in_sine", Kind::InSine},
        {"out_sine", Kind::OutSine}, {"in_out_sine", Kind::InOutSine},
        {"in_quad", Kind::InQuad}, {"out_quad", Kind::OutQuad}, {"in_out_quad", Kind::InOutQuad},
        {"in_cubic", Kind::InCubic}, {"out_cubic", Kind::OutCubic},
        {"in_out_cubic", Kind::InOutCubic}, {"in_quart", Kind::InQuart},
        {"out_quart", Kind::OutQuart}, {"in_out_quart", Kind::InOutQuart},
        {"in_quint", Kind::InQuint}, {"out_quint", Kind::OutQuint},
        {"in_out_quint", Kind::InOutQuint}, {"in_expo", Kind::InExpo},
        {"out_expo", Kind::OutExpo}, {"in_out_expo", Kind::InOutExpo},
    };
    const std::string_view plain = strip_namespace(*name);
    for (const auto& [text, kind] : kNames) {
        if (text == plain) ease.kind = kind;
    }
    return ease;
}

float Ease::operator()(float x) const {
    constexpr double kPi = std::numbers::pi;
    switch (kind) {
    case Kind::Linear: return x;
    case Kind::Constant: return 0.0f;
    case Kind::CubicBezier: {
        // The x curve inverted by four Newton steps, then the y curve read there.
        const auto curve = [](float p1, float p2, float s) {
            const float a = 3.0f * p1 - 3.0f * p2 + 1.0f;
            const float b = -6.0f * p1 + 3.0f * p2;
            const float c = 3.0f * p1;
            return ((a * s + b) * s + c) * s;
        };
        const auto gradient = [](float p1, float p2, float s) {
            const float a = 3.0f * p1 - 3.0f * p2 + 1.0f;
            const float b = -6.0f * p1 + 3.0f * p2;
            const float c = 3.0f * p1;
            return (3.0f * a * s + 2.0f * b) * s + c;
        };
        float s = x;
        for (int i = 0; i < 4; ++i) {
            const float slope = gradient(x1, x2, s);
            if (slope < 1.0e-5f) break;
            s -= (curve(x1, x2, s) - x) / slope;
        }
        return curve(y1, y2, s);
    }
    case Kind::InBack: return square(x) * (2.70158f * x - 1.70158f);
    case Kind::OutBack: return 1.0f + 2.70158f * cube(x - 1.0f) + 1.70158f * square(x - 1.0f);
    case Kind::InOutBack: {
        if (x < 0.5f) return 4.0f * x * x * (7.189819f * x - 2.5949094f) / 2.0f;
        const float d = 2.0f * x - 2.0f;
        return (d * d * (3.5949094f * d + 2.5949094f) + 2.0f) / 2.0f;
    }
    case Kind::InBounce: return 1.0f - bounce_out(1.0f - x);
    case Kind::OutBounce: return bounce_out(x);
    case Kind::InOutBounce:
        return x < 0.5f ? (1.0f - bounce_out(1.0f - 2.0f * x)) / 2.0f
                        : (1.0f + bounce_out(2.0f * x - 1.0f)) / 2.0f;
    case Kind::InCirc: return static_cast<float>(-std::sqrt(static_cast<double>(1.0f - x * x))) + 1.0f;
    case Kind::OutCirc: return static_cast<float>(std::sqrt(static_cast<double>(1.0f - square(x - 1.0f))));
    case Kind::InOutCirc: {
        const double d = x;
        return x < 0.5f ? static_cast<float>((1.0 - std::sqrt(1.0 - std::pow(2.0 * d, 2.0))) / 2.0)
                        : static_cast<float>((std::sqrt(1.0 - std::pow(-2.0 * d + 2.0, 2.0)) + 1.0) / 2.0);
    }
    case Kind::InElastic: {
        if (x == 0.0f) return 0.0f;
        if (x == 1.0f) return 1.0f;
        const double d = x;
        return static_cast<float>(-std::pow(2.0, 10.0 * d - 10.0) *
                                  std::sin((d * 10.0 - 10.75) *
                                           static_cast<double>(static_cast<float>(kPi * 2.0 / 3.0))));
    }
    case Kind::OutElastic: {
        if (x == 0.0f) return 0.0f;
        if (x == 1.0f) return 1.0f;
        const double d = x;
        return static_cast<float>(std::pow(2.0, -10.0 * d) *
                                      std::sin((d * 10.0 - 0.75) *
                                               static_cast<double>(static_cast<float>(kPi * 2.0 / 3.0))) +
                                  1.0);
    }
    case Kind::InOutElastic: {
        if (x == 0.0f) return 0.0f;
        if (x == 1.0f) return 1.0f;
        const double d = x;
        const double wave = std::sin((20.0 * d - 11.125) * static_cast<double>(static_cast<float>(kPi)) *
                                     4.0 / 9.0);
        return x < 0.5f ? static_cast<float>(-(std::pow(2.0, 20.0 * d - 10.0) * wave) / 2.0)
                        : static_cast<float>(std::pow(2.0, -20.0 * d + 10.0) * wave / 2.0 + 1.0);
    }
    case Kind::InSine: return 1.0f - jmath::cos(x * static_cast<float>(kPi / 2.0));
    case Kind::OutSine: return jmath::sin(x * static_cast<float>(kPi / 2.0));
    case Kind::InOutSine: return -(jmath::cos(static_cast<float>(kPi) * x) - 1.0f) / 2.0f;
    case Kind::InQuad: return x * x;
    case Kind::OutQuad: return 1.0f - square(1.0f - x);
    case Kind::InOutQuad:
        return x < 0.5f ? 2.0f * square(x)
                        : static_cast<float>(1.0 - std::pow(-2.0 * x + 2.0, 2.0) / 2.0);
    case Kind::InCubic: return cube(x);
    case Kind::OutCubic: return 1.0f - cube(1.0f - x);
    case Kind::InOutCubic:
        return x < 0.5f ? 4.0f * cube(x)
                        : static_cast<float>(1.0 - std::pow(-2.0 * x + 2.0, 3.0) / 2.0);
    case Kind::InQuart: return square(square(x));
    case Kind::OutQuart: return 1.0f - square(square(1.0f - x));
    case Kind::InOutQuart:
        return x < 0.5f ? 8.0f * square(square(x))
                        : static_cast<float>(1.0 - std::pow(-2.0 * x + 2.0, 4.0) / 2.0);
    case Kind::InQuint: return square(square(x)) * x;
    case Kind::OutQuint: return 1.0f - static_cast<float>(std::pow(1.0 - x, 5.0));
    case Kind::InOutQuint:
        return x < 0.5 ? 16.0f * x * x * x * x * x
                       : static_cast<float>(1.0 - std::pow(-2.0 * x + 2.0, 5.0) / 2.0);
    case Kind::InExpo: return x == 0.0f ? 0.0f : static_cast<float>(std::pow(2.0, 10.0 * x - 10.0));
    case Kind::OutExpo: return x == 1.0f ? 1.0f : 1.0f - static_cast<float>(std::pow(2.0, -10.0 * x));
    case Kind::InOutExpo:
        if (x < 0.5f) return x == 0.0f ? 0.0f : static_cast<float>(std::pow(2.0, 20.0 * x - 10.0) / 2.0);
        return x == 1.0f ? 1.0f : static_cast<float>((2.0 - std::pow(2.0, -20.0 * x + 10.0)) / 2.0);
    }
    return x;
}

void Track::bake(std::optional<int32_t> period) {
    segments_.clear();
    if (keyframes.empty()) return;
    if (keyframes.size() == 1) {
        Ease constant;
        constant.kind = Ease::Kind::Constant;
        segments_.push_back({constant, 0, 0, 0, 0});
        return;
    }
    const size_t last = keyframes.size() - 1;
    if (period) {
        segments_.push_back({ease, last, keyframes[last].ticks - *period, 0, keyframes[0].ticks});
    }
    for (size_t i = 0; i < last; ++i) {
        segments_.push_back({ease, i, keyframes[i].ticks, i + 1, keyframes[i + 1].ticks});
    }
    if (period) {
        segments_.push_back({ease, last, keyframes[last].ticks, 0, keyframes[0].ticks + *period});
    }
}

Argument Track::sample(int64_t ticks, std::optional<int32_t> period) const {
    if (segments_.empty()) return Argument{default_value(attribute)};
    int64_t at = ticks;
    if (period) {
        at = ticks % *period;
        if (at < 0) at += *period;
    }
    const Segment* segment = &segments_.back();
    for (const auto& candidate : segments_) {
        if (at < candidate.to_ticks) {
            segment = &candidate;
            break;
        }
    }
    const Argument& from = keyframes[segment->from].argument;
    const Argument& to = keyframes[segment->to].argument;
    if (at <= segment->from_ticks) return from;
    if (at >= segment->to_ticks) return to;
    const float t = static_cast<float>(at - segment->from_ticks) /
                    static_cast<float>(segment->to_ticks - segment->from_ticks);
    return lerp_argument(attribute, modifier, segment->ease(t), from, to);
}

Timeline Timeline::parse(std::string name, const nbt::Value& body) {
    Timeline timeline;
    timeline.name = std::move(name);
    if (const auto* period = body.find("period_ticks")) {
        if (const auto ticks = period->as_int(); ticks && *ticks > 0) timeline.period = *ticks;
    }
    const auto* tracks = body.find("tracks");
    const auto* compound = tracks != nullptr ? tracks->as_compound() : nullptr;
    if (compound == nullptr) return timeline;
    for (const auto& [attribute_name, raw] : *compound) {
        Track track;
        track.attribute = attribute_id(attribute_name);
        if (track.attribute == kAttributeCount) continue;
        track.ease = Ease::parse(raw.find("ease"));
        if (const auto* modifier = raw.find("modifier")) {
            if (const auto* text = modifier->as_string()) {
                const auto named = modifier_named(*text);
                if (!named) continue;
                track.modifier = *named;
            }
        }
        const auto* frames = raw.find("keyframes");
        const auto* list = frames != nullptr ? frames->as_list() : nullptr;
        if (list == nullptr || list->empty()) continue;
        bool valid = true;
        for (const auto& frame : *list) {
            const auto* ticks = frame.find("ticks");
            const auto* value = frame.find("value");
            if (ticks == nullptr || value == nullptr) {
                valid = false;
                break;
            }
            auto argument = read_argument(track.attribute, track.modifier, *value);
            if (!argument) {
                valid = false;
                break;
            }
            track.keyframes.push_back({static_cast<int32_t>(ticks->as_int().value_or(0)),
                                       std::move(*argument)});
        }
        if (!valid) continue;
        track.bake(timeline.period);
        timeline.tracks.push_back(std::move(track));
    }
    return timeline;
}

AttributeValues::AttributeValues() {
    for (size_t i = 0; i < kAttributeCount; ++i) values[i] = default_value(i);
}

void Environment::add_biome(std::string name, const nbt::Value* attributes) {
    Biome biome;
    if (attributes != nullptr) {
        biome.layers = AttributeLayers::parse(*attributes);
        biome.source = biomes_.size() + 1;
    }
    for (size_t id = 0; id < kAttributeCount; ++id) {
        if (biome.layers.layers[id]) biome_sets_[id] = true;
    }
    biomes_.push_back(std::move(biome));
    biome_names_.push_back(std::move(name));
}

void Environment::load(const Registries& registries, const Tags& tags) {
    dimensions_.clear();
    biomes_.clear();
    biome_names_.clear();
    biome_sets_ = {};
    timelines_.clear();
    json_tags_.clear();
    dimension_ = nullptr;
    active_.clear();

    if (const auto* entries = registries.find("minecraft:timeline")) {
        for (const auto& entry : *entries) {
            timelines_.push_back(entry.data ? Timeline::parse(entry.name, *entry.data)
                                            : Timeline{entry.name, std::nullopt, {}});
        }
    }
    if (const auto* entries = registries.find("minecraft:dimension_type")) {
        for (const auto& entry : *entries) {
            Dimension dimension;
            dimension.name = entry.name;
            if (entry.data) {
                if (const auto* attributes = entry.data->find("attributes")) {
                    dimension.layers = AttributeLayers::parse(*attributes);
                }
                if (const auto* timelines = entry.data->find("timelines")) {
                    dimension.timelines = *timelines;
                }
            }
            dimensions_.push_back(std::move(dimension));
        }
    }
    if (const auto* entries = registries.find("minecraft:worldgen/biome")) {
        for (const auto& entry : *entries) {
            add_biome(entry.name, entry.data ? entry.data->find("attributes") : nullptr);
        }
    }
    for (auto& dimension : dimensions_) {
        dimension.active = resolve_timelines(dimension.timelines, &tags);
    }
}

void Environment::add_timeline_json(std::string name, const json::Value& body) {
    timelines_.push_back(Timeline::parse(std::move(name), to_nbt(body)));
}

void Environment::add_timeline_tag(std::string name, std::vector<std::string> members) {
    std::vector<size_t> ids;
    for (const auto& member : members) {
        if (member.starts_with('#')) {
            for (const auto& [tag, inner] : json_tags_) {
                if (strip_namespace(tag) == strip_namespace(member)) {
                    ids.insert(ids.end(), inner.begin(), inner.end());
                }
            }
            continue;
        }
        for (size_t i = 0; i < timelines_.size(); ++i) {
            if (strip_namespace(timelines_[i].name) == strip_namespace(member)) ids.push_back(i);
        }
    }
    json_tags_.emplace_back(std::move(name), std::move(ids));
}

void Environment::add_dimension_json(std::string name, const json::Value& body) {
    const nbt::Value tag = to_nbt(body);
    Dimension dimension;
    dimension.name = std::move(name);
    if (const auto* attributes = tag.find("attributes")) {
        dimension.layers = AttributeLayers::parse(*attributes);
    }
    if (const auto* timelines = tag.find("timelines")) dimension.timelines = *timelines;
    dimension.active = resolve_timelines(dimension.timelines, nullptr);
    dimensions_.push_back(std::move(dimension));
}

void Environment::add_biome_json(std::string name, const json::Value& body) {
    const nbt::Value tag = to_nbt(body);
    add_biome(std::move(name), tag.find("attributes"));
}

bool Environment::select(std::string_view dimension_type, bool has_weather) {
    weather_ = has_weather;
    for (const auto& dimension : dimensions_) {
        if (strip_namespace(dimension.name) == strip_namespace(dimension_type)) {
            dimension_ = &dimension;
            selected_name_ = dimension.name;
            active_ = dimension.active;
            return true;
        }
    }
    dimension_ = nullptr;
    selected_name_.clear();
    active_.clear();
    return false;
}

const Timeline* Environment::timeline(std::string_view name) const {
    for (const auto& timeline : timelines_) {
        if (strip_namespace(timeline.name) == strip_namespace(name)) return &timeline;
    }
    return nullptr;
}

std::vector<size_t> Environment::resolve_timelines(const nbt::Value& field,
                                                   const Tags* tags) const {
    std::vector<size_t> out;
    const auto add_name = [&](std::string_view name) {
        if (name.starts_with('#')) {
            const std::string plain(strip_namespace(name));
            bool found = false;
            if (tags != nullptr) {
                const auto* ids = tags->find("minecraft:timeline", "minecraft:" + plain);
                if (ids == nullptr) ids = tags->find("minecraft:timeline", plain);
                if (ids != nullptr) {
                    found = true;
                    for (const int32_t id : *ids) {
                        if (id >= 0 && static_cast<size_t>(id) < timelines_.size()) {
                            out.push_back(static_cast<size_t>(id));
                        }
                    }
                }
            }
            if (!found) {
                for (const auto& [tag, ids] : json_tags_) {
                    if (strip_namespace(tag) == plain) out.insert(out.end(), ids.begin(), ids.end());
                }
            }
            return;
        }
        for (size_t i = 0; i < timelines_.size(); ++i) {
            if (strip_namespace(timelines_[i].name) == strip_namespace(name)) out.push_back(i);
        }
    };
    if (const auto* text = field.as_string()) {
        add_name(*text);
    } else if (const auto* list = field.as_list()) {
        for (const auto& item : *list) {
            if (const auto* entry = item.as_string()) add_name(*entry);
        }
    }
    return out;
}

AttributeValues Environment::evaluate(int64_t day_time, std::span<const BiomeWeight> biomes,
                                      size_t biome_underfoot, WeatherLevels weather) const {
    AttributeValues out;

    // The blend weighs each distinct attribute set once, in the order first met.
    struct Source {
        const AttributeLayers* layers;
        size_t key;
        double weight;
    };
    std::vector<Source> sources;
    sources.reserve(biomes.size());
    static const AttributeLayers kEmpty{};
    for (const auto& sample : biomes) {
        if (sample.biome >= biomes_.size()) continue;
        const auto& biome = biomes_[sample.biome];
        auto found = std::ranges::find(sources, biome.source, &Source::key);
        if (found != sources.end()) {
            found->weight += sample.weight;
        } else {
            sources.push_back({biome.source == 0 ? &kEmpty : &biome.layers, biome.source, sample.weight});
        }
    }

    const float thunder = weather.thunder;
    const float rain_only = weather.rain - thunder;

    for (size_t id = 0; id < kAttributeCount; ++id) {
        AttributeValue value = out.values[id];
        if (dimension_ != nullptr && dimension_->layers.layers[id]) {
            value = apply(id, value, *dimension_->layers.layers[id]);
        }

        if (biome_sets_[id]) {
            const auto through = [&](const AttributeLayers& layers) {
                return layers.layers[id] ? apply(id, value, *layers.layers[id]) : value;
            };
            if (kAttributes[id].spatial) {
                if (sources.size() == 1) {
                    value = through(*sources.front().layers);
                } else if (!sources.empty()) {
                    AttributeValue blended;
                    double total = 0.0;
                    bool first = true;
                    for (const auto& source : sources) {
                        AttributeValue here = through(*source.layers);
                        total += source.weight;
                        if (first) {
                            blended = std::move(here);
                            first = false;
                        } else {
                            const auto t = static_cast<float>(source.weight / total);
                            blended = lerp(id, Lerp::Spatial, t, blended, here);
                        }
                    }
                    value = std::move(blended);
                }
            } else if (biome_underfoot < biomes_.size()) {
                value = through(biomes_[biome_underfoot].layers);
            }
        }

        for (const size_t index : active_) {
            const Timeline& timeline = timelines_[index];
            for (const auto& track : timeline.tracks) {
                if (track.attribute != id) continue;
                value = apply(id, value, track.modifier, track.sample(day_time, timeline.period));
            }
        }

        if (weather_) {
            for (const auto& layer : weather_layers()) {
                if (layer.id != id) continue;
                if (rain_only > 0.0f) {
                    value = lerp(id, Lerp::StateChange, rain_only, value, apply(id, value, layer.rain));
                }
                if (thunder > 0.0f) {
                    value = lerp(id, Lerp::StateChange, thunder, value, apply(id, value, layer.thunder));
                }
            }
        }
        out.values[id] = sanitise(id, std::move(value));
    }
    return out;
}

float AttributeProbe::number(size_t id, float partial) const {
    return lerp(id, Lerp::PartialTick, partial, last_.values[id], next_.values[id]).number;
}

uint32_t AttributeProbe::colour(size_t id, float partial) const {
    return argb::srgb_lerp(partial, last_.values[id].argb, next_.values[id].argb);
}

nbt::Value to_nbt(const json::Value& value) {
    using Storage = nbt::Value::Storage;
    if (const auto flag = value.as_bool()) return nbt::Value(Storage{int8_t{*flag ? int8_t{1} : int8_t{0}}});
    if (const auto number = value.as_number()) {
        const double whole = std::floor(*number);
        if (whole == *number && std::abs(*number) < 2147483648.0) {
            return nbt::Value(Storage{static_cast<int32_t>(*number)});
        }
        return nbt::Value(Storage{*number});
    }
    if (const auto* text = value.as_string()) return nbt::Value(Storage{*text});
    if (const auto* array = value.as_array()) {
        nbt::List list;
        for (const auto& item : *array) list.push_back(to_nbt(item));
        return nbt::Value(Storage{std::move(list)});
    }
    if (const auto* object = value.as_object()) {
        nbt::Compound compound;
        for (const auto& [key, item] : *object) compound.emplace(key, to_nbt(item));
        return nbt::Value(Storage{std::move(compound)});
    }
    return {};
}

} // namespace mcpp::world
