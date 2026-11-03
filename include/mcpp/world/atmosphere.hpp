#pragma once
#include "mcpp/world/environment.hpp"

#include <array>
#include <cstdint>

namespace mcpp::world {

// What the camera is in, which decides whose fog it sees.
enum class Medium : uint8_t { Air, Water, Lava, PowderSnow };

enum class Skybox : uint8_t { Overworld, End, None };
Skybox skybox_named(std::string_view name);

// What a frame's sky, fog and light depend on besides the attributes: where
// the camera is and looks, the weather, the player's state and options.
struct AtmosphereInputs {
    float forward[3] = {0.0f, 0.0f, -1.0f};
    double eye[3] = {0.0, 64.0, 0.0};
    int32_t min_y = -64;
    bool flat_world = false;
    // options.getEffectiveRenderDistance(), in chunks.
    int32_t render_distance = 12;
    // options.cloudRange, in chunks.
    int32_t cloud_range = 128;
    // This frame's rain level, and thunder already multiplied by it.
    float rain = 0.0f;
    float thunder = 0.0f;
    Medium medium = Medium::Air;
    bool spectator = false;
    // LocalPlayer.getWaterVision: 0 on entering water, 1 once the eyes adjust.
    float water_vision = 0.0f;
    float night_vision = 0.0f;
    Skybox skybox = Skybox::Overworld;
    float ambient_light = 0.0f;
    // options.gamma, 0 (moody) to 1 (bright).
    float gamma = 0.5f;
    // LightTexture's flicker, a small random walk around zero.
    float block_flicker = 0.0f;
    // AtmosphericFogEnvironment's rain fog, kept across frames by RainFog.
    float rain_fog = 0.0f;
    // GameRenderer.getDarkenWorldAmount, from a boss bar that darkens the sky.
    float darken_world = 0.0f;
};

// The fog pulled in by rain under an open sky, eased towards its target a
// fifth of the way per tick.
struct RainFog {
    float multiplier = 0.0f;
    void update(float rain, int32_t sky_light, bool precipitation, float delta_ticks);
};

// Everything the renderer needs to draw one frame's sky, fog and light.
struct Atmosphere {
    Skybox skybox = Skybox::Overworld;
    bool draw_sky = true;
    uint32_t sky_colour = 0;
    float fog_colour[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float fog_start = 0.0f;
    float fog_end = 1024.0f;
    float render_fog_start = 0.0f;
    float render_fog_end = 192.0f;
    float sky_fog_end = 192.0f;
    float cloud_fog_end = 2048.0f;
    uint32_t sunrise_colour = 0;
    // Radians, as the game hands them to the sky renderer.
    float sun_angle = 0.0f;
    float moon_angle = 0.0f;
    float star_angle = 0.0f;
    int32_t moon_phase = 0;
    float rain_brightness = 1.0f;
    float star_brightness = 0.0f;
    bool dark_disc = false;

    uint32_t cloud_colour = 0;
    float cloud_height = 192.33f;

    // The lightmap shader's inputs, under the game's own names.
    float ambient_light_factor = 0.0f;
    float sky_factor = 1.0f;
    float block_factor = 1.5f;
    float night_vision_factor = 0.0f;
    float darkness_scale = 0.0f;
    float darken_world_factor = 0.0f;
    float brightness_factor = 0.5f;
    float sky_light_colour[3] = {1.0f, 1.0f, 1.0f};
    float ambient_colour[3] = {1.0f, 1.0f, 1.0f};
    // 16x16 RGBA8, block light along x and sky light along y.
    std::array<uint8_t, 16 * 16 * 4> lightmap{};
};

Atmosphere atmosphere(const AttributeProbe& probe, float partial, const AtmosphereInputs& inputs);

// The lightmap shader on the cpu, texel by texel: rgb 0 to 1 for a block and a
// sky level.
void lightmap_texel(const Atmosphere& atmosphere, int32_t block, int32_t sky, float out[3]);
void fill_lightmap(Atmosphere& atmosphere);

} // namespace mcpp::world
