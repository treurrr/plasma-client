#include "mcpp/world/environment.hpp"

#include "mcpp/json.hpp"
#include "mcpp/registries.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <numbers>

namespace mcpp::world {
namespace {

using Kind = AttributeKind;

// Kind and whether it blends, per the registry order. The defaults are the
// values an attribute has when neither the dimension nor a biome nor a
// timeline sets it.
constexpr AttributeInfo kAttributes[kAttributeCount] = {
    {"visual/fog_color", Kind::Rgb, true},
    {"visual/fog_start_distance", Kind::Float, true},
    {"visual/fog_end_distance", Kind::Float, true},
    {"visual/sky_fog_end_distance", Kind::Float, true},
    {"visual/cloud_fog_end_distance", Kind::Float, true},
    {"visual/water_fog_color", Kind::Rgb, true},
    {"visual/water_fog_start_distance", Kind::Float, true},
    {"visual/water_fog_end_distance", Kind::Float, true},
    {"visual/sky_color", Kind::Rgb, true},
    {"visual/sunrise_sunset_color", Kind::Argb, true},
    {"visual/cloud_color", Kind::Argb, true},
    {"visual/cloud_height", Kind::Float, true},
    {"visual/sun_angle", Kind::Float, true},
    {"visual/moon_angle", Kind::Float, true},
    {"visual/star_angle", Kind::Float, true},
    {"visual/moon_phase", Kind::Text, false},
    {"visual/star_brightness", Kind::Float, true},
    {"visual/sky_light_color", Kind::Rgb, true},
    {"visual/sky_light_factor", Kind::Float, true},
    {"visual/default_dripstone_particle", Kind::Object, false},
    {"visual/ambient_particles", Kind::Object, false},
    {"audio/background_music", Kind::Object, false},
    {"audio/music_volume", Kind::Float, false},
    {"audio/ambient_sounds", Kind::Object, false},
    {"audio/firefly_bush_sounds", Kind::Bool, false},
    {"gameplay/sky_light_level", Kind::Float, true},
    {"gameplay/can_start_raid", Kind::Bool, false},
    {"gameplay/water_evaporates", Kind::Bool, false},
    {"gameplay/bed_rule", Kind::Object, false},
    {"gameplay/respawn_anchor_works", Kind::Bool, false},
    {"gameplay/nether_portal_spawns_piglin", Kind::Bool, false},
    {"gameplay/fast_lava", Kind::Bool, false},
    {"gameplay/increased_fire_burnout", Kind::Bool, false},
    {"gameplay/eyeblossom_open", Kind::Text, false},
    {"gameplay/turtle_egg_hatch_chance", Kind::Float, true},
    {"gameplay/piglins_zombify", Kind::Bool, false},
    {"gameplay/snow_golem_melts", Kind::Bool, false},
    {"gameplay/creaking_active", Kind::Bool, false},
    {"gameplay/surface_slime_spawn_chance", Kind::Float, true},
    {"gameplay/cat_waking_up_gift_chance", Kind::Float, true},
    {"gameplay/bees_stay_in_hive", Kind::Bool, false},
    {"gameplay/monsters_burn", Kind::Bool, false},
    {"gameplay/can_pillager_patrol_spawn", Kind::Bool, false},
    {"gameplay/villager_activity", Kind::Text, false},
    {"gameplay/baby_villager_activity", Kind::Text, false},
};

// A datapack writes 96.0 and a server may send it as an int; either is a number.
std::optional<double> number_of(const nbt::Value& value) {
    if (const auto number = value.as_double()) return number;
    if (const auto number = value.as_int()) return static_cast<double>(*number);
    return std::nullopt;
}

std::string_view strip_namespace(std::string_view name) {
    if (name.starts_with("minecraft:")) name.remove_prefix(10);
    if (name.starts_with('#')) name.remove_prefix(1);
    if (name.starts_with("minecraft:")) name.remove_prefix(10);
    return name;
}

double channel(uint32_t argb, int shift) {
    return static_cast<double>((argb >> shift) & 0xFFu) / 255.0;
}

uint32_t pack(double a, double r, double g, double b) {
    const auto byte = [](double value) {
        return static_cast<uint32_t>(std::lround(std::clamp(value, 0.0, 1.0) * 255.0));
    };
    return (byte(a) << 24) | (byte(r) << 16) | (byte(g) << 8) | byte(b);
}

template <typename Op>
uint32_t per_channel(uint32_t x, uint32_t y, bool with_alpha, Op op) {
    const double a = with_alpha ? op(channel(x, 24), channel(y, 24)) : channel(x, 24);
    return pack(a, op(channel(x, 16), channel(y, 16)), op(channel(x, 8), channel(y, 8)),
                op(channel(x, 0), channel(y, 0)));
}

bool read_bool(const nbt::Value& value) {
    if (const auto number = value.as_int()) return *number != 0;
    if (const auto* text = value.as_string()) return *text == "true";
    return false;
}

AttributeValue read_value(size_t id, const nbt::Value& raw) {
    AttributeValue value = default_value(id);
    switch (kAttributes[id].kind) {
    case Kind::Float:
        value.number = number_of(raw).value_or(value.number);
        break;
    case Kind::Bool:
        value.number = read_bool(raw) ? 1.0 : 0.0;
        break;
    case Kind::Rgb:
        if (const auto rgb = parse_colour(raw, false)) value.argb = *rgb | 0xFF000000u;
        break;
    case Kind::Argb:
        if (const auto argb = parse_colour(raw, true)) value.argb = *argb;
        break;
    case Kind::Text:
        if (const auto* text = raw.as_string()) {
            value.text = std::string(strip_namespace(*text));
        } else if (const auto flag = raw.as_int()) {
            value.text = *flag != 0 ? "true" : "false";
        }
        break;
    case Kind::Object:
        value.object = raw;
        break;
    }
    return value;
}

// A cubic bezier from (0,0) to (1,1), the way a stylesheet's timing function
// is one: find where the curve's x is t, and answer its y there.
double cubic_bezier(const double p[4], double t) {
    const auto curve = [](double a, double b, double s) {
        const double u = 1.0 - s;
        return 3.0 * u * u * s * a + 3.0 * u * s * s * b + s * s * s;
    };
    double low = 0.0;
    double high = 1.0;
    double s = t;
    for (int i = 0; i < 64; ++i) {
        const double x = curve(p[0], p[2], s);
        if (std::abs(x - t) < 1e-9) break;
        if (x < t) low = s; else high = s;
        s = (low + high) * 0.5;
    }
    return curve(p[1], p[3], s);
}

double bounce_out(double x) {
    constexpr double n = 7.5625;
    constexpr double d = 2.75;
    if (x < 1.0 / d) return n * x * x;
    if (x < 2.0 / d) { x -= 1.5 / d; return n * x * x + 0.75; }
    if (x < 2.5 / d) { x -= 2.25 / d; return n * x * x + 0.9375; }
    x -= 2.625 / d;
    return n * x * x + 0.984375;
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
    case FogEndDistance: value.number = 1024.0; break;
    case SkyFogEndDistance: value.number = 512.0; break;
    case CloudFogEndDistance: value.number = 2048.0; break;
    case WaterFogColor: value.argb = 0xFF050533u; break;
    case WaterFogStartDistance: value.number = -8.0; break;
    case WaterFogEndDistance: value.number = 96.0; break;
    case FogColor: case SkyColor: value.argb = 0xFF000000u; break;
    case CloudHeight: value.number = 192.33; break;
    case MoonPhase: value.text = "full_moon"; break;
    case SkyLightColor: value.argb = 0xFFFFFFFFu; break;
    case SkyLightFactor: value.number = 1.0; break;
    case MusicVolume: value.number = 1.0; break;
    case SkyLightLevel: value.number = 15.0; break;
    case TurtleEggHatchChance: value.number = 0.002; break;
    case CanStartRaid: case PiglinsZombify: case CanPillagerPatrolSpawn: value.number = 1.0; break;
    case EyeblossomOpen: value.text = "default"; break;
    case VillagerActivity: case BabyVillagerActivity: value.text = "idle"; break;
    default: break;
    }
    return value;
}

std::optional<uint32_t> parse_colour(const nbt::Value& value, bool with_alpha) {
    if (const auto number = value.as_int()) {
        const auto packed = static_cast<uint32_t>(*number);
        return with_alpha ? packed : (packed & 0xFFFFFFu) | 0xFF000000u;
    }
    if (const auto* list = value.as_list()) {
        std::array<double, 4> parts{1.0, 1.0, 1.0, 1.0};
        if (list->size() != 3 && list->size() != 4) return std::nullopt;
        // Three floats are rgb, four are argb.
        const size_t first = list->size() == 4 ? 0 : 1;
        for (size_t i = 0; i < list->size(); ++i) {
            parts[first + i] = number_of((*list)[i]).value_or(0.0);
        }
        return pack(with_alpha ? parts[0] : 1.0, parts[1], parts[2], parts[3]);
    }
    const std::string* text = value.as_string();
    if (text == nullptr) return std::nullopt;
    std::string_view digits = *text;
    if (digits.starts_with('#')) digits.remove_prefix(1);
    uint32_t packed = 0;
    const char* end = digits.data() + digits.size();
    const auto parsed = std::from_chars(digits.data(), end, packed, 16);
    if (parsed.ec != std::errc{} || parsed.ptr != end) return std::nullopt;
    if (digits.size() == 6) return packed | 0xFF000000u;
    if (digits.size() == 8) return with_alpha ? packed : packed | 0xFF000000u;
    return std::nullopt;
}

std::optional<Modifier> modifier_named(std::string_view name) {
    static constexpr std::pair<std::string_view, Modifier> kNames[] = {
        {"override", Modifier::Override}, {"add", Modifier::Add},
        {"subtract", Modifier::Subtract}, {"multiply", Modifier::Multiply},
        {"minimum", Modifier::Minimum}, {"maximum", Modifier::Maximum},
        {"alpha_blend", Modifier::AlphaBlend}, {"blend_to_gray", Modifier::BlendToGray},
        {"and", Modifier::And}, {"nand", Modifier::Nand}, {"or", Modifier::Or},
        {"nor", Modifier::Nor}, {"xor", Modifier::Xor}, {"xnor", Modifier::Xnor},
    };
    name = strip_namespace(name);
    for (const auto& [text, modifier] : kNames) {
        if (text == name) return modifier;
    }
    return std::nullopt;
}

AttributeLayers AttributeLayers::parse(const nbt::Value& attributes) {
    AttributeLayers out;
    const auto* compound = attributes.as_compound();
    if (compound == nullptr) return out;
    for (const auto& [name, raw] : *compound) {
        const size_t id = attribute_id(name);
        if (id == kAttributeCount) continue;
        AttributeLayer layer;
        const nbt::Value* argument = &raw;
        // {modifier, argument} is a layer that combines; anything else is the
        // value itself. An object attribute is never combined, so a compound
        // there is always its value.
        if (kAttributes[id].kind != Kind::Object && raw.as_compound() != nullptr) {
            if (const auto* name_value = raw.find("modifier")) {
                if (const auto* text = name_value->as_string()) {
                    layer.modifier = modifier_named(*text).value_or(Modifier::Override);
                }
            }
            if (const auto* value = raw.find("argument")) argument = value;
        }
        if (layer.modifier == Modifier::AlphaBlend && kAttributes[id].kind == Kind::Float) {
            if (const auto* value = argument->find("value")) {
                layer.argument = read_value(id, *value);
            }
            if (const auto* alpha = argument->find("alpha")) {
                layer.alpha = number_of(*alpha).value_or(1.0);
            }
        } else if (layer.modifier == Modifier::BlendToGray) {
            if (const auto* value = argument->find("brightness")) {
                layer.brightness = number_of(*value).value_or(1.0);
            }
            if (const auto* value = argument->find("factor")) {
                layer.alpha = number_of(*value).value_or(1.0);
            }
        } else {
            // A colour argument to a combining modifier may carry an alpha of
            // its own even on an rgb attribute: alpha_blend needs one.
            if (layer.modifier == Modifier::AlphaBlend || layer.modifier == Modifier::Multiply) {
                if (const auto argb = parse_colour(*argument, true);
                    argb && kAttributes[id].kind != Kind::Float &&
                    kAttributes[id].kind != Kind::Bool) {
                    layer.argument.argb = *argb;
                    out.layers[id] = std::move(layer);
                    continue;
                }
            }
            layer.argument = read_value(id, *argument);
        }
        out.layers[id] = std::move(layer);
    }
    return out;
}

AttributeValue apply(size_t id, const AttributeValue& value, const AttributeLayer& layer) {
    const Kind kind = kAttributes[id].kind;
    const AttributeValue& arg = layer.argument;
    if (layer.modifier == Modifier::Override || kind == Kind::Text || kind == Kind::Object) {
        return arg;
    }
    AttributeValue out = value;
    if (kind == Kind::Float) {
        const double x = value.number;
        const double y = arg.number;
        switch (layer.modifier) {
        case Modifier::Add: out.number = x + y; break;
        case Modifier::Subtract: out.number = x - y; break;
        case Modifier::Multiply: out.number = x * y; break;
        case Modifier::Minimum: out.number = std::min(x, y); break;
        case Modifier::Maximum: out.number = std::max(x, y); break;
        case Modifier::AlphaBlend: out.number = x + (y - x) * layer.alpha; break;
        default: break;
        }
        return out;
    }
    if (kind == Kind::Bool) {
        const bool x = value.flag();
        const bool y = arg.flag();
        bool result = x;
        switch (layer.modifier) {
        case Modifier::And: result = x && y; break;
        case Modifier::Nand: result = !(x && y); break;
        case Modifier::Or: result = x || y; break;
        case Modifier::Nor: result = !(x || y); break;
        case Modifier::Xor: result = x != y; break;
        case Modifier::Xnor: result = x == y; break;
        default: break;
        }
        out.number = result ? 1.0 : 0.0;
        return out;
    }
    const bool argb = kind == Kind::Argb;
    const uint32_t x = value.argb;
    const uint32_t y = arg.argb;
    switch (layer.modifier) {
    case Modifier::Add:
        out.argb = per_channel(x, y, false, [](double a, double b) { return a + b; });
        break;
    case Modifier::Subtract:
        out.argb = per_channel(x, y, false, [](double a, double b) { return a - b; });
        break;
    case Modifier::Multiply:
        out.argb = per_channel(x, y, argb, [](double a, double b) { return a * b; });
        break;
    case Modifier::AlphaBlend: {
        const double alpha = channel(y, 24);
        const double a = argb ? channel(x, 24) + (1.0 - channel(x, 24)) * alpha : 1.0;
        out.argb = pack(a, channel(x, 16) + (channel(y, 16) - channel(x, 16)) * alpha,
                        channel(x, 8) + (channel(y, 8) - channel(x, 8)) * alpha,
                        channel(x, 0) + (channel(y, 0) - channel(x, 0)) * alpha);
        break;
    }
    case Modifier::BlendToGray: {
        const double grey = (channel(x, 16) * 0.3 + channel(x, 8) * 0.59 + channel(x, 0) * 0.11) *
                            layer.brightness;
        const double t = layer.alpha;
        out.argb = pack(channel(x, 24), channel(x, 16) + (grey - channel(x, 16)) * t,
                        channel(x, 8) + (grey - channel(x, 8)) * t,
                        channel(x, 0) + (grey - channel(x, 0)) * t);
        break;
    }
    default: break;
    }
    if (!argb) out.argb |= 0xFF000000u;
    return out;
}

AttributeValue mix(size_t id, const AttributeValue& from, const AttributeValue& to, double t) {
    const Kind kind = kAttributes[id].kind;
    if (!kAttributes[id].interpolated) return from;
    AttributeValue out = from;
    if (kind == Kind::Float) {
        out.number = from.number + (to.number - from.number) * t;
    } else if (kind == Kind::Rgb || kind == Kind::Argb) {
        const auto lerp = [&](int shift) {
            return channel(from.argb, shift) + (channel(to.argb, shift) - channel(from.argb, shift)) * t;
        };
        out.argb = pack(lerp(24), lerp(16), lerp(8), lerp(0));
    }
    return out;
}

Ease Ease::parse(const nbt::Value* value) {
    Ease ease;
    if (value == nullptr) return ease;
    if (const auto* points = value->find("cubic_bezier")) {
        if (const auto* list = points->as_list(); list != nullptr && list->size() == 4) {
            ease.kind = Kind::CubicBezier;
            for (size_t i = 0; i < 4; ++i) ease.bezier[i] = number_of((*list)[i]).value_or(0.0);
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

double Ease::operator()(double x) const {
    constexpr double pi = std::numbers::pi;
    constexpr double c1 = 1.70158;
    constexpr double c2 = c1 * 1.525;
    constexpr double c3 = c1 + 1.0;
    constexpr double c4 = 2.0 * pi / 3.0;
    constexpr double c5 = 2.0 * pi / 4.5;
    const auto power_in = [](double t, double n) { return std::pow(t, n); };
    const auto power_out = [](double t, double n) { return 1.0 - std::pow(1.0 - t, n); };
    const auto power_in_out = [](double t, double n) {
        return t < 0.5 ? std::pow(2.0, n - 1.0) * std::pow(t, n)
                       : 1.0 - std::pow(-2.0 * t + 2.0, n) / 2.0;
    };
    switch (kind) {
    case Kind::Linear: return x;
    case Kind::Constant: return 0.0;
    case Kind::CubicBezier: return cubic_bezier(bezier, x);
    case Kind::InBack: return c3 * x * x * x - c1 * x * x;
    case Kind::OutBack: return 1.0 + c3 * std::pow(x - 1.0, 3.0) + c1 * std::pow(x - 1.0, 2.0);
    case Kind::InOutBack:
        return x < 0.5 ? (std::pow(2.0 * x, 2.0) * ((c2 + 1.0) * 2.0 * x - c2)) / 2.0
                       : (std::pow(2.0 * x - 2.0, 2.0) * ((c2 + 1.0) * (x * 2.0 - 2.0) + c2) + 2.0) / 2.0;
    case Kind::InBounce: return 1.0 - bounce_out(1.0 - x);
    case Kind::OutBounce: return bounce_out(x);
    case Kind::InOutBounce:
        return x < 0.5 ? (1.0 - bounce_out(1.0 - 2.0 * x)) / 2.0 : (1.0 + bounce_out(2.0 * x - 1.0)) / 2.0;
    case Kind::InCirc: return 1.0 - std::sqrt(1.0 - x * x);
    case Kind::OutCirc: return std::sqrt(1.0 - (x - 1.0) * (x - 1.0));
    case Kind::InOutCirc:
        return x < 0.5 ? (1.0 - std::sqrt(1.0 - 4.0 * x * x)) / 2.0
                       : (std::sqrt(1.0 - std::pow(-2.0 * x + 2.0, 2.0)) + 1.0) / 2.0;
    case Kind::InElastic:
        if (x <= 0.0 || x >= 1.0) return x;
        return -std::pow(2.0, 10.0 * x - 10.0) * std::sin((x * 10.0 - 10.75) * c4);
    case Kind::OutElastic:
        if (x <= 0.0 || x >= 1.0) return x;
        return std::pow(2.0, -10.0 * x) * std::sin((x * 10.0 - 0.75) * c4) + 1.0;
    case Kind::InOutElastic:
        if (x <= 0.0 || x >= 1.0) return x;
        return x < 0.5 ? -(std::pow(2.0, 20.0 * x - 10.0) * std::sin((20.0 * x - 11.125) * c5)) / 2.0
                       : (std::pow(2.0, -20.0 * x + 10.0) * std::sin((20.0 * x - 11.125) * c5)) / 2.0 + 1.0;
    case Kind::InSine: return 1.0 - std::cos(x * pi / 2.0);
    case Kind::OutSine: return std::sin(x * pi / 2.0);
    case Kind::InOutSine: return -(std::cos(pi * x) - 1.0) / 2.0;
    case Kind::InQuad: return power_in(x, 2.0);
    case Kind::OutQuad: return power_out(x, 2.0);
    case Kind::InOutQuad: return power_in_out(x, 2.0);
    case Kind::InCubic: return power_in(x, 3.0);
    case Kind::OutCubic: return power_out(x, 3.0);
    case Kind::InOutCubic: return power_in_out(x, 3.0);
    case Kind::InQuart: return power_in(x, 4.0);
    case Kind::OutQuart: return power_out(x, 4.0);
    case Kind::InOutQuart: return power_in_out(x, 4.0);
    case Kind::InQuint: return power_in(x, 5.0);
    case Kind::OutQuint: return power_out(x, 5.0);
    case Kind::InOutQuint: return power_in_out(x, 5.0);
    case Kind::InExpo: return x <= 0.0 ? 0.0 : std::pow(2.0, 10.0 * x - 10.0);
    case Kind::OutExpo: return x >= 1.0 ? 1.0 : 1.0 - std::pow(2.0, -10.0 * x);
    case Kind::InOutExpo:
        if (x <= 0.0 || x >= 1.0) return x;
        return x < 0.5 ? std::pow(2.0, 20.0 * x - 10.0) / 2.0
                       : (2.0 - std::pow(2.0, -20.0 * x + 10.0)) / 2.0;
    }
    return x;
}

AttributeValue Track::sample(double ticks, std::optional<int64_t> period) const {
    if (keyframes.empty()) return default_value(attribute);
    if (keyframes.size() == 1) return keyframes.front().value;
    double at = ticks;
    if (period && *period > 0) {
        at = std::fmod(ticks, static_cast<double>(*period));
        if (at < 0.0) at += static_cast<double>(*period);
    }
    // The last keyframe at or before the tick, so where two share a tick the
    // later one is what the timeline leaves it at; and the first one after.
    const Keyframe* before = nullptr;
    const Keyframe* after = nullptr;
    for (const auto& frame : keyframes) {
        if (static_cast<double>(frame.ticks) <= at) before = &frame;
        else if (after == nullptr) after = &frame;
    }
    double before_ticks = before != nullptr ? static_cast<double>(before->ticks) : 0.0;
    double after_ticks = after != nullptr ? static_cast<double>(after->ticks) : 0.0;
    if (period && *period > 0) {
        // Round the end of the period into the start of the next one.
        if (before == nullptr) {
            before = &keyframes.back();
            before_ticks = static_cast<double>(before->ticks - *period);
        }
        if (after == nullptr) {
            after = &keyframes.front();
            after_ticks = static_cast<double>(after->ticks + *period);
        }
    } else {
        if (before == nullptr) return keyframes.front().value;
        if (after == nullptr) return before->value;
    }
    if (ease.kind == Ease::Kind::Constant || !kAttributes[attribute].interpolated ||
        after_ticks <= before_ticks) {
        return before->value;
    }
    const double t = (at - before_ticks) / (after_ticks - before_ticks);
    return mix(attribute, before->value, after->value, ease(std::clamp(t, 0.0, 1.0)));
}

Timeline Timeline::parse(std::string name, const nbt::Value& body) {
    Timeline timeline;
    timeline.name = std::move(name);
    if (const auto* period = body.find("period_ticks")) {
        if (const auto ticks = period->as_int()) timeline.period = *ticks;
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
                track.modifier = modifier_named(*text).value_or(Modifier::Override);
            }
        }
        if (const auto* frames = raw.find("keyframes")) {
            if (const auto* list = frames->as_list()) {
                for (const auto& frame : *list) {
                    Keyframe keyframe;
                    if (const auto* ticks = frame.find("ticks")) {
                        keyframe.ticks = ticks->as_int().value_or(0);
                    }
                    if (const auto* value = frame.find("value")) {
                        // A multiply on an rgb attribute still keys an alpha
                        // when the colour has one; keep it as written.
                        if (track.modifier == Modifier::Multiply ||
                            track.modifier == Modifier::AlphaBlend) {
                            keyframe.value = read_value(track.attribute, *value);
                            const Kind kind = kAttributes[track.attribute].kind;
                            if (kind == Kind::Rgb || kind == Kind::Argb) {
                                if (const auto argb = parse_colour(*value, true)) {
                                    keyframe.value.argb = *argb;
                                }
                            }
                        } else {
                            keyframe.value = read_value(track.attribute, *value);
                        }
                    }
                    track.keyframes.push_back(std::move(keyframe));
                }
            }
        }
        // Stable, so two keyframes on one tick keep their order.
        std::ranges::stable_sort(track.keyframes, {}, &Keyframe::ticks);
        timeline.tracks.push_back(std::move(track));
    }
    return timeline;
}

AttributeValues::AttributeValues() {
    for (size_t i = 0; i < kAttributeCount; ++i) values[i] = default_value(i);
}

void Environment::load(const Registries& registries, const Tags& tags) {
    dimensions_.clear();
    biomes_.clear();
    biome_names_.clear();
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
            const nbt::Value* attributes = entry.data ? entry.data->find("attributes") : nullptr;
            biomes_.push_back(attributes != nullptr ? AttributeLayers::parse(*attributes)
                                                    : AttributeLayers{});
            biome_names_.push_back(entry.name);
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
    const nbt::Value* attributes = tag.find("attributes");
    biomes_.push_back(attributes != nullptr ? AttributeLayers::parse(*attributes)
                                            : AttributeLayers{});
    biome_names_.push_back(std::move(name));
}

bool Environment::select(std::string_view dimension_type) {
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
            if (tags != nullptr) {
                const auto* ids = tags->find("minecraft:timeline", "minecraft:" + plain);
                if (ids == nullptr) ids = tags->find("minecraft:timeline", plain);
                if (ids != nullptr) {
                    for (const int32_t id : *ids) {
                        if (id >= 0 && static_cast<size_t>(id) < timelines_.size()) {
                            out.push_back(static_cast<size_t>(id));
                        }
                    }
                }
            }
            for (const auto& [tag, ids] : json_tags_) {
                if (strip_namespace(tag) == plain) out.insert(out.end(), ids.begin(), ids.end());
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

AttributeValues Environment::evaluate(double day_time, std::span<const BiomeWeight> biomes) const {
    AttributeValues out;
    double total_weight = 0.0;
    const BiomeWeight* heaviest = nullptr;
    for (const auto& sample : biomes) {
        total_weight += sample.weight;
        if (heaviest == nullptr || sample.weight > heaviest->weight) heaviest = &sample;
    }

    for (size_t id = 0; id < kAttributeCount; ++id) {
        AttributeValue value = out.values[id];
        if (dimension_ != nullptr && dimension_->layers.layers[id]) {
            value = apply(id, value, *dimension_->layers.layers[id]);
        }

        // A biome edge is a blend for what blends, and the biome the camera is
        // most in for what does not.
        const auto in_biome = [&](size_t biome) {
            if (biome < biomes_.size() && biomes_[biome].layers[id]) {
                return apply(id, value, *biomes_[biome].layers[id]);
            }
            return value;
        };
        if (heaviest != nullptr && total_weight > 0.0) {
            const Kind kind = kAttributes[id].kind;
            if (kAttributes[id].interpolated && (kind == Kind::Float || kind == Kind::Rgb ||
                                                 kind == Kind::Argb)) {
                double sum[4]{};
                for (const auto& sample : biomes) {
                    const AttributeValue here = in_biome(sample.biome);
                    const double w = sample.weight / total_weight;
                    if (kind == Kind::Float) {
                        sum[0] += here.number * w;
                    } else {
                        sum[0] += channel(here.argb, 24) * w;
                        sum[1] += channel(here.argb, 16) * w;
                        sum[2] += channel(here.argb, 8) * w;
                        sum[3] += channel(here.argb, 0) * w;
                    }
                }
                if (kind == Kind::Float) value.number = sum[0];
                else value.argb = pack(sum[0], sum[1], sum[2], sum[3]);
            } else {
                value = in_biome(heaviest->biome);
            }
        }

        for (const size_t index : active_) {
            const Timeline& timeline = timelines_[index];
            for (const auto& track : timeline.tracks) {
                if (track.attribute != id) continue;
                AttributeLayer layer;
                layer.modifier = track.modifier;
                layer.argument = track.sample(day_time, timeline.period);
                value = apply(id, value, layer);
            }
        }
        out.values[id] = std::move(value);
    }
    return out;
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
