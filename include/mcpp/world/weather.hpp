#pragma once
// Rain and snow: the ten-block column of falling texture the game puts over
// every column around the camera, each with its own random, and the rain that
// lands as a particle and is heard. `WeatherEffectRenderer` whole, except for
// how the quads reach the gpu.
#include "mcpp/jmath.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <string_view>
#include <vector>

namespace mcpp::world {

// Biome's Precipitation: what falls on a column, if anything.
enum class Precipitation : uint8_t { None, Rain, Snow };

// `WeatherEffectRenderer`'s ColumnInstance: one column of falling texture, from
// bottom_y to top_y, with where its texture has scrolled to and the light it
// is drawn under.
struct WeatherColumn {
    int32_t x = 0;
    int32_t z = 0;
    int32_t bottom_y = 0;
    int32_t top_y = 0;
    float u_offset = 0.0f;
    float v_offset = 0.0f;
    int32_t light = 0;
};

// `WeatherRenderState`: everything a frame of weather is, before the camera.
struct WeatherState {
    float intensity = 0.0f;
    int32_t radius = 10;
    std::vector<WeatherColumn> rain;
    std::vector<WeatherColumn> snow;

    void clear() {
        rain.clear();
        snow.clear();
    }
};

// What the weather asks the world. Each is answered for a block column.
struct WeatherLevel {
    // `Level.getHeight(Heightmap.Types.MOTION_BLOCKING, x, z)`.
    std::function<int32_t(int32_t, int32_t)> height;
    // `WeatherEffectRenderer.getPrecipitationAt`: none where no chunk is loaded.
    std::function<Precipitation(int32_t, int32_t, int32_t)> precipitation;
    // `LevelRenderer.getLightColor`: block light << 4 | sky light << 20.
    std::function<int32_t(int32_t, int32_t, int32_t)> light;
    int32_t min_y = -64;
};

// `Biome.getPrecipitationAt`, `Biome.warmEnoughToRain`,
// `Biome.getHeightAdjustedTemperature`: nothing where the biome has none,
// snow below a temperature of 0.15, rain above it. The temperature falls off
// above sea level plus seventeen.
//
// Two pieces of vanilla's temperature are not here, both noise: the one it
// adds to the height fall-off, and the frozen biomes' own modifier. Either
// can move a snow line by a block on a mountain; neither changes what falls
// at or below sea level, which is where a player is.
Precipitation precipitation_at(bool has_precipitation, float base_temperature, int32_t y,
                               int32_t sea_level);

// `LightTexture.block`, `LightTexture.sky`, `LightTexture.pack`.
constexpr int32_t light_block(int32_t packed) { return packed >> 4 & 0xF; }
constexpr int32_t light_sky(int32_t packed) { return packed >> 20 & 0xF; }
constexpr int32_t light_pack(int32_t block, int32_t sky) { return block << 4 | sky << 20; }

// `WeatherEffectRenderer.WeatherEffectRenderer`: a 32 by 32 table of which way
// a column faces, so every column turns its flat side towards the camera.
const std::array<float, 1024>& column_size_x();
const std::array<float, 1024>& column_size_z();

// `WeatherEffectRenderer.extractRenderState`: the seed a column is given,
// which is the one part of the pass that is arithmetic on its position alone.
int32_t column_seed(int32_t x, int32_t z);

// `WeatherEffectRenderer.createRainColumnInstance`: the texture scrolled down
// by the clock, each column at its own speed and its own head start.
WeatherColumn rain_column(jmath::LegacyRandom& random, int32_t ticks, int32_t x, int32_t bottom_y,
                          int32_t top_y, int32_t z, int32_t light, float partial);
// `WeatherEffectRenderer.createSnowColumnInstance`: snow drifts sideways as it
// falls and is drawn brighter than the light it stands in.
WeatherColumn snow_column(jmath::LegacyRandom& random, int32_t ticks, int32_t x, int32_t bottom_y,
                          int32_t top_y, int32_t z, int32_t light, float partial);

// `WeatherEffectRenderer.extractRenderState`: every column within the weather
// radius that has something falling on it, each seeded from its own position.
void extract_weather(const WeatherLevel& level, int32_t ticks, float partial,
                     const double camera[3], float intensity, int32_t radius,
                     WeatherState& out);

// One corner of a weather quad, the way the game writes it.
struct WeatherVertex {
    float x, y, z;
    float u, v;
    uint32_t colour;   // ARGB, white with the column's own alpha
    uint32_t light;
};

// `WeatherEffectRenderer.renderInstances`: four corners a column, fading out
// with the square of the distance and turned flat towards the camera. alpha is
// 1 for rain and 0.8 for snow, which is what makes snow the softer of the two.
void weather_quads(const std::vector<WeatherColumn>& columns, const double camera[3], float alpha,
                   int32_t radius, float intensity, std::vector<WeatherVertex>& out);

// What `tickRainParticles` asks for: a drop on top of the block below, or
// smoke where it lands on something hot, and a sound now and then.
struct RainTick {
    // `ParticleStatus`: every drop, half of them, or only the sound.
    enum class Detail : uint8_t { All, Decreased, Minimal };

    // The block the drop lands on, its collision shape's top at (dx, dz) and
    // the fluid height there, which is what the drop sits on.
    std::function<double(int32_t, int32_t, int32_t, double, double)> surface;
    // Lava, magma or a lit campfire there: the drop becomes smoke.
    std::function<bool(int32_t, int32_t, int32_t)> hot;
    std::function<int32_t(int32_t, int32_t)> height;
    std::function<Precipitation(int32_t, int32_t, int32_t)> precipitation;
    // Where a particle goes, and what is heard; the sound is already placed.
    std::function<void(bool, double, double, double)> particle;
    std::function<void(std::string_view, int32_t, int32_t, int32_t, float, float)> sound;
    int32_t min_y = -64;
    Detail detail = Detail::All;
};

// `WeatherEffectRenderer.tickRainParticles`: 0.225 drops a block of the square
// the weather radius covers, thinning with the square of the rain level.
// rain_sound_time carries between ticks, as the renderer's field does.
void tick_rain_particles(const RainTick& world, int32_t ticks, float rain_level,
                         const double camera[3], int32_t radius, int32_t& rain_sound_time);

} // namespace mcpp::world
