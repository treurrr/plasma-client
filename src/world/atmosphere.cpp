#include "mcpp/world/atmosphere.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>

namespace mcpp::world {
namespace {

void unpack_rgb(uint32_t argb, float out[3]) {
    out[0] = static_cast<float>((argb >> 16) & 0xFFu) / 255.0f;
    out[1] = static_cast<float>((argb >> 8) & 0xFFu) / 255.0f;
    out[2] = static_cast<float>(argb & 0xFFu) / 255.0f;
}

float luminance(const float c[3]) { return c[0] * 0.3f + c[1] * 0.59f + c[2] * 0.11f; }

// Rain and thunder wash a colour towards a grey of its own brightness.
void weather_grey(float c[3], float level, float grey_scale, float strength) {
    if (level <= 0.0f) return;
    const float grey = luminance(c) * grey_scale;
    const float keep = 1.0f - level * strength;
    for (int i = 0; i < 3; ++i) c[i] = c[i] * keep + grey * (1.0f - keep);
}

constexpr float kGaussian[7] = {0.0f, 1.0f, 4.0f, 6.0f, 4.0f, 1.0f, 0.0f};

} // namespace

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

Atmosphere atmosphere(const AttributeValues& values, const AtmosphereInputs& in) {
    using namespace attribute;
    Atmosphere out;
    out.skybox = in.skybox == "end" ? Skybox::End
               : in.skybox == "none" ? Skybox::None
                                     : Skybox::Overworld;
    const float render = static_cast<float>(in.render_chunks) * 16.0f;
    const float rain = std::clamp(in.rain, 0.0f, 1.0f);
    const float thunder = std::clamp(in.thunder, 0.0f, 1.0f) * rain;

    unpack_rgb(values.colour(SkyColor), out.sky);
    weather_grey(out.sky, rain, 0.6f, 0.75f);
    weather_grey(out.sky, thunder, 0.2f, 0.75f);

    out.sun_angle = values.number(SunAngle);
    out.moon_angle = values.number(MoonAngle);
    out.star_angle = values.number(StarAngle);
    out.moon_phase = moon_phase_index(values.text(MoonPhase));
    out.celestial_alpha = 1.0f - rain;
    out.star_brightness = values.number(StarBrightness) * (1.0f - rain);
    const uint32_t sunrise = values.colour(SunriseSunsetColor);
    unpack_rgb(sunrise, out.sunrise);
    out.sunrise[3] = static_cast<float>(sunrise >> 24) / 255.0f * (1.0f - rain);

    const uint32_t cloud = values.colour(CloudColor);
    unpack_rgb(cloud, out.cloud);
    out.cloud[3] = static_cast<float>(cloud >> 24) / 255.0f;
    weather_grey(out.cloud, rain, 0.6f, 0.95f);
    weather_grey(out.cloud, thunder, 0.2f, 0.95f);
    out.cloud_height = values.number(CloudHeight);
    out.cloud_fog_end = std::min(values.number(CloudFogEndDistance),
                                 static_cast<float>(in.cloud_chunks) * 16.0f);

    // The view distance's fog closes the last stretch, never less than four
    // blocks of it and never more than 64.
    out.render_fog_end = render;
    out.render_fog_start = render - std::clamp(render / 10.0f, 4.0f, 64.0f);
    out.sky_fog_end = std::min(values.number(SkyFogEndDistance), render);

    if (in.medium == Medium::Water) {
        unpack_rgb(values.colour(WaterFogColor), out.fog);
        out.fog_start = values.number(WaterFogStartDistance);
        out.fog_end = values.number(WaterFogEndDistance) * std::max(0.25f, in.water_vision);
        out.draw_sky = false;
    } else if (in.medium == Medium::Lava) {
        out.fog[0] = 0.6f;
        out.fog[1] = 0.1f;
        out.fog[2] = 0.0f;
        out.fog_start = 0.25f;
        out.fog_end = 1.0f;
        out.draw_sky = false;
    } else if (in.medium == Medium::PowderSnow) {
        out.fog[0] = 0.623f;
        out.fog[1] = 0.734f;
        out.fog[2] = 0.785f;
        out.fog_start = 0.0f;
        out.fog_end = 2.0f;
        out.draw_sky = false;
    } else {
        unpack_rgb(values.colour(FogColor), out.fog);
        out.fog_start = values.number(FogStartDistance);
        out.fog_end = values.number(FogEndDistance);
        // Looking towards a rising or setting sun pulls the fog towards its glow.
        if (in.render_chunks >= 4 && out.sunrise[3] > 0.0f) {
            const float side =
                std::sin(out.sun_angle * std::numbers::pi_v<float> / 180.0f) > 0.0f ? -1.0f : 1.0f;
            float towards = in.look[0] * side;
            if (towards > 0.0f) {
                towards *= out.sunrise[3];
                for (int i = 0; i < 3; ++i) out.fog[i] += (out.sunrise[i] - out.fog[i]) * towards;
            }
        }
        // A short view distance shows more of the sky's own colour in the fog.
        const float reach = 1.0f - std::pow(0.25f + 0.75f * static_cast<float>(in.render_chunks) /
                                                        32.0f, 0.25f);
        for (int i = 0; i < 3; ++i) out.fog[i] += (out.sky[i] - out.fog[i]) * reach;
        if (rain > 0.0f) {
            out.fog[0] *= 1.0f - rain * 0.5f;
            out.fog[1] *= 1.0f - rain * 0.5f;
            out.fog[2] *= 1.0f - rain * 0.4f;
        }
        if (thunder > 0.0f) {
            for (float& c : out.fog) c *= 1.0f - thunder * 0.5f;
        }
    }

    // Towards the bottom of the world the fog darkens to black.
    if (in.medium != Medium::Lava) {
        double fade = (in.eye_y - static_cast<double>(in.min_y)) * (in.flat_world ? 1.0 : 1.0 / 32.0);
        if (fade < 1.0) {
            fade = std::max(fade, 0.0);
            fade *= fade;
            for (float& c : out.fog) c *= static_cast<float>(fade);
        }
    }

    const double horizon = in.flat_world ? static_cast<double>(in.min_y) : in.sea_level;
    out.dark_disc = in.eye_y - horizon < 0.0;

    // Rain dims the sky's share of the light the way it dims the day, down
    // to the night's floor and no further.
    const float factor = values.number(SkyLightFactor);
    constexpr float kNightFloor = 0.24f;
    if (factor > kNightFloor) {
        const float dim = (1.0f - rain * 5.0f / 16.0f) * (1.0f - thunder * 5.0f / 16.0f);
        out.sky_factor = kNightFloor + (factor - kNightFloor) * dim;
    } else {
        out.sky_factor = factor;
    }
    unpack_rgb(values.colour(SkyLightColor), out.sky_light_colour);
    out.ambient_factor = in.ambient_light;
    out.brightness = in.gamma;
    return out;
}

void lightmap(const Atmosphere& a, float sky_level, float block_level, float out[3]) {
    const auto brightness = [](float level) { return level / (4.0f - 3.0f * level); };
    const float block = brightness(std::floor(block_level) / 15.0f) * a.block_factor;
    const float sky = brightness(std::floor(sky_level) / 15.0f) * a.sky_factor;
    float c[3] = {block, block * ((block * 0.6f + 0.4f) * 0.6f + 0.4f),
                  block * (block * block * 0.6f + 0.4f)};
    for (int i = 0; i < 3; ++i) {
        c[i] = c[i] + (1.0f - c[i]) * a.ambient_factor;
        c[i] += a.sky_light_colour[i] * sky;
        c[i] = c[i] + (0.75f - c[i]) * 0.04f;
        c[i] = std::clamp(c[i], 0.0f, 1.0f);
    }
    const float top = std::max({c[0], c[1], c[2]});
    const float inverted = 1.0f - top;
    const float scaled = 1.0f - inverted * inverted * inverted * inverted;
    for (int i = 0; i < 3; ++i) {
        const float lifted = top > 0.0f ? c[i] * (scaled / top) : 0.0f;
        c[i] = c[i] + (lifted - c[i]) * a.brightness;
        out[i] = c[i] + (0.75f - c[i]) * 0.04f;
    }
}

std::vector<BiomeWeight> biome_weights(double x, double y, double z,
                                       const std::function<int32_t(int32_t, int32_t, int32_t)>& biome_at) {
    const double qx = (x - 2.0) * 0.25;
    const double qy = (y - 2.0) * 0.25;
    const double qz = (z - 2.0) * 0.25;
    const auto ix = static_cast<int32_t>(std::floor(qx));
    const auto iy = static_cast<int32_t>(std::floor(qy));
    const auto iz = static_cast<int32_t>(std::floor(qz));
    const double fx = qx - ix;
    const double fy = qy - iy;
    const double fz = qz - iz;
    const auto weight = [](double f, int i) {
        return kGaussian[i + 1] + (kGaussian[i] - kGaussian[i + 1]) * f;
    };
    std::map<int32_t, double> sums;
    for (int dx = 0; dx < 6; ++dx) {
        const double wx = weight(fx, dx);
        for (int dy = 0; dy < 6; ++dy) {
            const double wy = weight(fy, dy);
            for (int dz = 0; dz < 6; ++dz) {
                const double w = wx * wy * weight(fz, dz);
                if (w <= 0.0) continue;
                const int32_t biome = biome_at(ix - 2 + dx, iy - 2 + dy, iz - 2 + dz);
                if (biome < 0) continue;
                sums[biome] += w;
            }
        }
    }
    std::vector<BiomeWeight> out;
    out.reserve(sums.size());
    for (const auto& [biome, w] : sums) out.push_back({static_cast<size_t>(biome), w});
    return out;
}

} // namespace mcpp::world
