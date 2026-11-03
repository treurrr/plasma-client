#pragma once
#include "mcpp/world/environment.hpp"

#include <cstdint>
#include <functional>
#include <string_view>
#include <vector>

namespace mcpp::world {

// What the camera is in, which decides whose fog it sees.
enum class Medium : uint8_t { Air, Water, Lava, PowderSnow };

// What a frame's sky depends on besides the data: where the camera is and
// looks, the weather, and the player's options.
struct AtmosphereInputs {
    float look[3] = {0.0f, 0.0f, -1.0f};
    double eye_y = 64.0;
    int32_t min_y = -64;
    int32_t sea_level = 63;
    bool flat_world = false;
    int32_t render_chunks = 12;
    int32_t cloud_chunks = 128;
    float rain = 0.0f;
    float thunder = 0.0f;
    Medium medium = Medium::Air;
    // 0 on entering water, 1 once the eyes have adjusted.
    float water_vision = 1.0f;
    std::string_view skybox = "overworld";
    float ambient_light = 0.0f;
    // options.gamma, 0 (moody) to 1 (bright).
    float gamma = 0.5f;
};

enum class Skybox : uint8_t { Overworld, End, None };

// Everything the renderer needs to draw one frame's sky, fog and light.
struct Atmosphere {
    Skybox skybox = Skybox::Overworld;
    float sky[3] = {0.0f, 0.0f, 0.0f};
    // The colour the world fades into and the screen is cleared to.
    float fog[3] = {0.0f, 0.0f, 0.0f};
    // Spherical fog from the environment, cylindrical fog from the view
    // distance; a fragment takes the thicker of the two.
    float fog_start = 0.0f;
    float fog_end = 1024.0f;
    float render_fog_start = 0.0f;
    float render_fog_end = 192.0f;
    float sky_fog_end = 192.0f;
    float cloud_fog_end = 2048.0f;
    // rgba; alpha 0 is no glow.
    float sunrise[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float sun_angle = 0.0f;    // degrees, 0 straight up
    float moon_angle = 180.0f;
    float star_angle = 0.0f;
    int32_t moon_phase = 0;    // 0 full ... 7 waxing gibbous
    float star_brightness = 0.0f;
    // How much of the sun and moon the weather lets through.
    float celestial_alpha = 1.0f;
    // Below the horizon there is a dark plane under the sky.
    bool dark_disc = false;
    bool draw_sky = true;

    float cloud[4] = {1.0f, 1.0f, 1.0f, 0.8f};
    float cloud_height = 192.33f;

    // The lightmap's inputs, as the game's lightmap shader names them.
    float sky_factor = 1.0f;
    float block_factor = 1.5f;
    float ambient_factor = 0.0f;
    float sky_light_colour[3] = {1.0f, 1.0f, 1.0f};
    float brightness = 0.5f;
};

Atmosphere atmosphere(const AttributeValues& values, const AtmosphereInputs& inputs);

// The game's lightmap at one sky and block level, 0 to 15 each, for a check
// against a vanilla screenshot or a test. rgb, 0 to 1.
void lightmap(const Atmosphere& atmosphere, float sky_level, float block_level, float out[3]);

// Biomes around a point and how much each counts: a Gaussian over the 4x4x4
// cells the biomes are stored in, so walking across an edge fades the sky
// rather than switching it. biome_at takes cell coordinates.
std::vector<BiomeWeight> biome_weights(double x, double y, double z,
                                       const std::function<int32_t(int32_t, int32_t, int32_t)>& biome_at);

int32_t moon_phase_index(std::string_view name);

} // namespace mcpp::world
