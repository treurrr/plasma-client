#include "mcpp/world/atmosphere.hpp"

#include "mcpp/jmath.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>

namespace mcpp::world {
namespace {

constexpr float kDegreesToRadians = static_cast<float>(std::numbers::pi / 180.0);

float lerp(float t, float from, float to) { return from + t * (to - from); }

uint32_t weather_darken(uint32_t colour, float rain, float thunder) {
    if (rain > 0.0f) {
        const float rg = 1.0f - rain * 0.5f;
        const float b = 1.0f - rain * 0.4f;
        colour = argb::scale_rgb(colour, rg, rg, b);
    }
    if (thunder > 0.0f) {
        const float all = 1.0f - thunder * 0.5f;
        colour = argb::scale_rgb(colour, all, all, all);
    }
    return colour;
}

// AtmosphericFogEnvironment's colour: the fog, pulled towards a rising or
// setting sun when looking at it, then towards the sky by how little of the
// world the sky fog leaves visible.
uint32_t atmospheric_colour(const AttributeProbe& probe, float partial, const AtmosphereInputs& in) {
    using namespace attribute;
    uint32_t fog = probe.colour(FogColor, partial);
    if (in.render_distance >= 4) {
        const float sun = probe.number(SunAngle, partial) * kDegreesToRadians;
        const float side = jmath::sin(sun) > 0.0f ? -1.0f : 1.0f;
        const float towards = in.forward[0] * side + in.forward[1] * 0.0f + in.forward[2] * 0.0f;
        if (towards > 0.0f) {
            const uint32_t glow = probe.colour(SunriseSunsetColor, partial);
            const float alpha = argb::alpha_float(glow);
            if (alpha > 0.0f) fog = argb::srgb_lerp(towards * alpha, fog, argb::opaque(glow));
        }
    }
    uint32_t sky = probe.colour(SkyColor, partial);
    sky = weather_darken(sky, in.rain, in.thunder);
    const float reach = std::min(probe.number(SkyFogEndDistance, partial) / 16.0f,
                                 static_cast<float>(in.render_distance));
    const float d = reach / 32.0f;
    float mix = d < 0.0f ? 0.25f : d > 1.0f ? 1.0f : lerp(d, 0.25f, 1.0f);
    mix = 1.0f - static_cast<float>(std::pow(mix, 0.25));
    return argb::srgb_lerp(mix, fog, sky);
}

} // namespace

Skybox skybox_named(std::string_view name) {
    if (name.starts_with("minecraft:")) name.remove_prefix(10);
    if (name == "end") return Skybox::End;
    if (name == "none") return Skybox::None;
    return Skybox::Overworld;
}

void RainFog::update(float rain, int32_t sky_light, bool precipitation, float delta_ticks) {
    const float open = std::clamp((static_cast<float>(sky_light) - 8.0f) / 7.0f, 0.0f, 1.0f);
    const float target = rain * open * (precipitation ? 1.0f : 0.5f);
    multiplier += (target - multiplier) * delta_ticks * 0.2f;
}

Atmosphere atmosphere(const AttributeProbe& probe, float partial, const AtmosphereInputs& in) {
    using namespace attribute;
    Atmosphere out;
    out.skybox = in.skybox;
    const float render = static_cast<float>(in.render_distance * 16);

    uint32_t base = 0;
    switch (in.medium) {
    case Medium::Water: base = probe.colour(WaterFogColor, partial); break;
    case Medium::Lava: base = 0xFF991A00u; break;
    case Medium::PowderSnow: base = 0xFF9FBACCu; break;
    case Medium::Air: base = atmospheric_colour(probe, partial, in); break;
    }

    // Towards the bottom of the world the fog fades to black.
    const float onset = in.flat_world ? 1.0f : 32.0f;
    const float void_dark = std::clamp(
        (onset + static_cast<float>(in.min_y) - static_cast<float>(in.eye[1])) / onset, 0.0f, 1.0f);
    float r = argb::red_float(base);
    float g = argb::green_float(base);
    float b = argb::blue_float(base);
    if (void_dark > 0.0f && in.medium != Medium::Lava && in.medium != Medium::PowderSnow) {
        const float keep = (1.0f - void_dark) * (1.0f - void_dark);
        r *= keep;
        g *= keep;
        b *= keep;
    }
    if (in.darken_world > 0.0f) {
        r = lerp(in.darken_world, r, r * 0.7f);
        g = lerp(in.darken_world, g, g * 0.6f);
        b = lerp(in.darken_world, b, b * 0.6f);
    }
    const float brighten = in.medium == Medium::Water ? in.water_vision : in.night_vision;
    if (r != 0.0f && g != 0.0f && b != 0.0f) {
        const float scale = 1.0f / std::max(r, std::max(g, b));
        r = lerp(brighten, r, r * scale);
        g = lerp(brighten, g, g * scale);
        b = lerp(brighten, b, b * scale);
    }
    out.fog_colour[0] = r;
    out.fog_colour[1] = g;
    out.fog_colour[2] = b;
    out.fog_colour[3] = 1.0f;

    switch (in.medium) {
    case Medium::Water:
        out.fog_start = probe.number(WaterFogStartDistance, partial);
        out.fog_end = probe.number(WaterFogEndDistance, partial) * std::max(0.25f, in.water_vision);
        out.sky_fog_end = out.fog_end;
        out.cloud_fog_end = out.fog_end;
        break;
    case Medium::Lava:
    case Medium::PowderSnow:
        if (in.spectator) {
            out.fog_start = -8.0f;
            out.fog_end = render * 0.5f;
        } else if (in.medium == Medium::Lava) {
            out.fog_start = 0.25f;
            out.fog_end = 1.0f;
        } else {
            out.fog_start = 0.0f;
            out.fog_end = 2.0f;
        }
        out.sky_fog_end = out.fog_end;
        out.cloud_fog_end = out.fog_end;
        break;
    case Medium::Air: {
        out.fog_start = probe.number(FogStartDistance, partial) + -160.0f * in.rain_fog;
        const float end = probe.number(FogEndDistance, partial);
        out.fog_end = std::max(std::min(96.0f, end), end + -256.0f * in.rain_fog);
        out.sky_fog_end = std::min(render, probe.number(SkyFogEndDistance, partial));
        out.cloud_fog_end = std::min(static_cast<float>(in.cloud_range * 16),
                                     probe.number(CloudFogEndDistance, partial));
        break;
    }
    }
    const float last_stretch = std::clamp(render / 10.0f, 4.0f, 64.0f);
    out.render_fog_start = render - last_stretch;
    out.render_fog_end = render;

    out.draw_sky = in.medium != Medium::PowderSnow && in.medium != Medium::Lava &&
                   in.skybox != Skybox::None;
    if (in.skybox == Skybox::Overworld) {
        out.sun_angle = probe.number(SunAngle, partial) * kDegreesToRadians;
        out.moon_angle = probe.number(MoonAngle, partial) * kDegreesToRadians;
        out.star_angle = probe.number(StarAngle, partial) * kDegreesToRadians;
        out.rain_brightness = 1.0f - in.rain;
        out.star_brightness = probe.number(StarBrightness, partial);
        out.sunrise_colour = probe.colour(SunriseSunsetColor, partial);
        out.moon_phase = moon_phase_index(probe.value(MoonPhase).text);
        out.sky_colour = probe.colour(SkyColor, partial);
        const double horizon = in.flat_world ? static_cast<double>(in.min_y) : 63.0;
        out.dark_disc = in.eye[1] - horizon < 0.0;
    }

    out.cloud_colour = probe.colour(CloudColor, partial);
    out.cloud_height = probe.number(CloudHeight, partial);

    const uint32_t sky_light = probe.colour(SkyLightColor, partial);
    out.ambient_light_factor = in.ambient_light;
    out.sky_factor = probe.number(SkyLightFactor, partial);
    out.block_factor = in.block_flicker + 1.5f;
    out.night_vision_factor = in.night_vision;
    out.darkness_scale = 0.0f;
    out.darken_world_factor = in.darken_world;
    out.brightness_factor = std::max(0.0f, in.gamma);
    out.sky_light_colour[0] = argb::red_float(sky_light);
    out.sky_light_colour[1] = argb::green_float(sky_light);
    out.sky_light_colour[2] = argb::blue_float(sky_light);
    fill_lightmap(out);
    return out;
}

void lightmap_texel(const Atmosphere& a, int32_t block_level, int32_t sky_level, float out[3]) {
    const auto brightness = [](float level) { return level / (4.0f - 3.0f * level); };
    const float block = brightness(static_cast<float>(block_level) / 15.0f) * a.block_factor;
    const float sky = brightness(static_cast<float>(sky_level) / 15.0f) * a.sky_factor;
    float c[3] = {block, block * ((block * 0.6f + 0.4f) * 0.6f + 0.4f),
                  block * (block * block * 0.6f + 0.4f)};
    for (int i = 0; i < 3; ++i) {
        c[i] = lerp(a.ambient_light_factor, c[i], a.ambient_colour[i]);
        c[i] += a.sky_light_colour[i] * sky;
        c[i] = lerp(0.04f, c[i], 0.75f);
    }
    if (a.ambient_light_factor == 0.0f) {
        const float darkened[3] = {c[0] * 0.7f, c[1] * 0.6f, c[2] * 0.6f};
        for (int i = 0; i < 3; ++i) c[i] = lerp(a.darken_world_factor, c[i], darkened[i]);
    }
    if (a.night_vision_factor > 0.0f) {
        const float top = std::max(c[0], std::max(c[1], c[2]));
        if (top < 1.0f) {
            for (float& channel : c) channel = lerp(a.night_vision_factor, channel, channel / top);
        }
    }
    if (a.ambient_light_factor == 0.0f) {
        for (float& channel : c) channel -= a.darkness_scale;
    }
    for (float& channel : c) channel = std::clamp(channel, 0.0f, 1.0f);
    const float top = std::max(std::max(c[0], c[1]), c[2]);
    const float inverted = 1.0f - top;
    const float scaled = 1.0f - inverted * inverted * inverted * inverted;
    for (int i = 0; i < 3; ++i) {
        const float lifted = c[i] * (scaled / top);
        c[i] = lerp(a.brightness_factor, c[i], lifted);
        out[i] = lerp(0.04f, c[i], 0.75f);
    }
}

void fill_lightmap(Atmosphere& a) {
    // `LightTexture.updateLightTexture`: the game rebuilds the 256 texels on a
    // tick, not on a frame. Nothing here depends on where the camera is or
    // looks, so the same inputs give the same bytes, and a frame between two
    // ticks copies them instead of working them out again. The comparison is
    // bitwise, so a reuse is only ever a reuse of an identical table.
    struct Key {
        float factors[7];
        float sky_light_colour[3];
        float ambient_colour[3];
    };
    struct Cache {
        Key key{};
        std::array<uint8_t, 16 * 16 * 4> lightmap{};
        bool filled = false;
    };
    static thread_local Cache cache;
    Key key{{a.ambient_light_factor, a.sky_factor, a.block_factor, a.night_vision_factor,
             a.darkness_scale, a.darken_world_factor, a.brightness_factor},
            {a.sky_light_colour[0], a.sky_light_colour[1], a.sky_light_colour[2]},
            {a.ambient_colour[0], a.ambient_colour[1], a.ambient_colour[2]}};
    if (cache.filled && std::memcmp(&key, &cache.key, sizeof(Key)) == 0) {
        a.lightmap = cache.lightmap;
        return;
    }
    for (int32_t sky = 0; sky < 16; ++sky) {
        for (int32_t block = 0; block < 16; ++block) {
            float rgb[3];
            lightmap_texel(a, block, sky, rgb);
            uint8_t* texel = &a.lightmap[static_cast<size_t>(sky * 16 + block) * 4];
            for (int i = 0; i < 3; ++i) {
                texel[i] = static_cast<uint8_t>(std::lround(std::clamp(rgb[i], 0.0f, 1.0f) * 255.0f));
            }
            texel[3] = 255;
        }
    }
    cache.key = key;
    cache.lightmap = a.lightmap;
    cache.filled = true;
}

} // namespace mcpp::world
