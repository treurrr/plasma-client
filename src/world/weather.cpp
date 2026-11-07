#include "mcpp/world/weather.hpp"

#include <algorithm>
#include <cmath>

namespace mcpp::world {
namespace {

// `Mth.floor`: rounds a double down to an int.
int32_t floor_i(double v) {
    const auto i = static_cast<int32_t>(v);
    return v < static_cast<double>(i) ? i - 1 : i;
}

// `Mth.lerp`: start, then delta of the way to end.
float lerp(float delta, float start, float end) { return start + delta * (end - start); }

// `ARGB.white`, `ARGB.as8BitChannel`: white at an alpha, floored to a byte.
uint32_t argb_white(float alpha) {
    const float scaled = alpha * 255.0f;
    const auto truncated = static_cast<int32_t>(scaled);
    const int32_t floored = scaled < static_cast<float>(truncated) ? truncated - 1 : truncated;
    return static_cast<uint32_t>(floored) << 24 | 0x00FFFFFFu;
}

// `WeatherEffectRenderer.WeatherEffectRenderer`: for every offset in a 32 by
// 32 square around the camera's own column, the unit vector to it turned a
// quarter turn, which is the direction the column's flat side runs along.
struct ColumnTable {
    std::array<float, 1024> x{};
    std::array<float, 1024> z{};
    ColumnTable() {
        for (int32_t row = 0; row < 32; ++row) {
            for (int32_t column = 0; column < 32; ++column) {
                const auto dx = static_cast<float>(column - 16);
                const auto dz = static_cast<float>(row - 16);
                // `Mth.length`: the hypotenuse, as a float.
                // `Mth.length`, `Mth.lengthSquared`: the square is summed as
                // doubles and the root taken back to a float.
                const auto length = static_cast<float>(
                    std::sqrt(static_cast<double>(dx) * dx + static_cast<double>(dz) * dz));
                x[static_cast<size_t>(row) * 32 + static_cast<size_t>(column)] = -dz / length;
                z[static_cast<size_t>(row) * 32 + static_cast<size_t>(column)] = dx / length;
            }
        }
    }
};

const ColumnTable& table() {
    static const ColumnTable built;
    return built;
}

} // namespace

// `WeatherEffectRenderer.extractRenderState`: the seed a column is given.
int32_t column_seed(int32_t x, int32_t z) {
    return static_cast<int32_t>(
        (static_cast<uint32_t>(x) * static_cast<uint32_t>(x) * 3121u +
         static_cast<uint32_t>(x) * 45238971u) ^
        (static_cast<uint32_t>(z) * static_cast<uint32_t>(z) * 418711u +
         static_cast<uint32_t>(z) * 13761u));
}

// `WeatherEffectRenderer.createRainColumnInstance`: the texture scrolled down
// by the clock, each column at its own speed and its own head start.
WeatherColumn rain_column(jmath::LegacyRandom& random, int32_t ticks, int32_t x, int32_t bottom_y,
                          int32_t top_y, int32_t z, int32_t light, float partial) {
    const int32_t clock = ticks & 131071;
    const int32_t offset = static_cast<int32_t>(
        (static_cast<uint32_t>(x) * static_cast<uint32_t>(x) * 3121u +
         static_cast<uint32_t>(x) * 45238971u + static_cast<uint32_t>(z) * static_cast<uint32_t>(z) * 418711u +
         static_cast<uint32_t>(z) * 13761u) & 0xFFu);
    const float speed = 3.0f + random.next_float();
    const float scroll = -(static_cast<float>(clock) + static_cast<float>(offset) + partial) / 32.0f * speed;
    return {x, z, bottom_y, top_y, 0.0f, std::fmod(scroll, 32.0f), light};
}

// `WeatherEffectRenderer.createSnowColumnInstance`: snow drifts sideways as it
// falls and is drawn brighter than the light it stands in.
WeatherColumn snow_column(jmath::LegacyRandom& random, int32_t ticks, int32_t x, int32_t bottom_y,
                          int32_t top_y, int32_t z, int32_t light, float partial) {
    const float clock = static_cast<float>(ticks) + partial;
    // Java reads left to right, so the draw before the gaussian is the one
    // that is added to it; C++ would not say which came first.
    // Both sums are a double plus a float: the gaussian is multiplied in
    // float and only then widened, which is not the same number.
    const double drift_base = random.next_double();
    const float drift_step = clock * 0.01f * static_cast<float>(random.next_gaussian());
    const auto drift = static_cast<float>(drift_base + static_cast<double>(drift_step));
    const double sway_base = random.next_double();
    const float sway_step = clock * static_cast<float>(random.next_gaussian()) * 0.001f;
    const auto sway = static_cast<float>(sway_base + static_cast<double>(sway_step));
    const float fall = -(static_cast<float>(ticks & 511) + partial) / 512.0f;
    const int32_t brightened =
        light_pack((light_block(light) * 3 + 15) / 4, (light_sky(light) * 3 + 15) / 4);
    return {x, z, bottom_y, top_y, drift, fall + sway, brightened};
}

Precipitation precipitation_at(bool has_precipitation, float base_temperature, int32_t y,
                               int32_t sea_level) {
    if (!has_precipitation) return Precipitation::None;
    float temperature = base_temperature;
    const int32_t fall_off = sea_level + 17;
    if (y > fall_off) {
        temperature -= static_cast<float>(y - fall_off) * 0.05f / 40.0f;
    }
    return temperature >= 0.15f ? Precipitation::Rain : Precipitation::Snow;
}

const std::array<float, 1024>& column_size_x() { return table().x; }
const std::array<float, 1024>& column_size_z() { return table().z; }

// `WeatherEffectRenderer.extractRenderState`: a square of columns around the
// camera, each seeded from where it stands so it falls the same way every
// frame, and skipped where its column has no height to fall through.
void extract_weather(const WeatherLevel& level, int32_t ticks, float partial,
                     const double camera[3], float intensity, int32_t radius, WeatherState& out) {
    out.clear();
    out.intensity = intensity;
    if (intensity <= 0.0f) return;
    out.radius = radius;
    const int32_t cx = floor_i(camera[0]);
    const int32_t cy = floor_i(camera[1]);
    const int32_t cz = floor_i(camera[2]);
    for (int32_t z = cz - radius; z <= cz + radius; ++z) {
        for (int32_t x = cx - radius; x <= cx + radius; ++x) {
            const int32_t ground = level.height(x, z);
            const int32_t bottom = std::max(cy - radius, ground);
            const int32_t top = std::max(cy + radius, ground);
            if (top - bottom == 0) continue;
            const Precipitation falls = level.precipitation(x, cy, z);
            if (falls == Precipitation::None) continue;
            jmath::LegacyRandom random(column_seed(x, z));
            const int32_t light = level.light(x, std::max(cy, ground), z);
            if (falls == Precipitation::Rain) {
                out.rain.push_back(rain_column(random, ticks, x, bottom, top, z, light, partial));
            } else {
                out.snow.push_back(snow_column(random, ticks, x, bottom, top, z, light, partial));
            }
        }
    }
}

// `WeatherEffectRenderer.renderInstances`: each column a single quad turned
// flat towards the camera, fading from its own alpha at the camera to half
// that at the edge of the weather radius, all of it scaled by the rain level.
void weather_quads(const std::vector<WeatherColumn>& columns, const double camera[3], float alpha,
                   int32_t radius, float intensity, std::vector<WeatherVertex>& out) {
    const auto reach = static_cast<float>(radius * radius);
    const int32_t cx = floor_i(camera[0]);
    const int32_t cz = floor_i(camera[2]);
    for (const auto& column : columns) {
        const auto dx = static_cast<float>(static_cast<double>(column.x) + 0.5 - camera[0]);
        const auto dz = static_cast<float>(static_cast<double>(column.z) + 0.5 - camera[2]);
        // `Mth.lengthSquared`: summed as doubles, then taken to a float.
        const auto distance = static_cast<float>(static_cast<double>(dx) * dx + static_cast<double>(dz) * dz);
        const float faded = lerp(std::min(distance / reach, 1.0f), alpha, 0.5f) * intensity;
        const uint32_t colour = argb_white(faded);
        const auto slot = static_cast<size_t>((column.z - cz + 16) * 32 + column.x - cx + 16);
        const float half_x = column_size_x()[slot] / 2.0f;
        const float half_z = column_size_z()[slot] / 2.0f;
        const float x0 = dx - half_x;
        const float x1 = dx + half_x;
        const auto y0 = static_cast<float>(static_cast<double>(column.top_y) - camera[1]);
        const auto y1 = static_cast<float>(static_cast<double>(column.bottom_y) - camera[1]);
        const float z0 = dz - half_z;
        const float z1 = dz + half_z;
        const float u0 = column.u_offset + 0.0f;
        const float u1 = column.u_offset + 1.0f;
        const float v0 = static_cast<float>(column.bottom_y) * 0.25f + column.v_offset;
        const float v1 = static_cast<float>(column.top_y) * 0.25f + column.v_offset;
        const auto light = static_cast<uint32_t>(column.light);
        out.push_back({x0, y0, z0, u0, v0, colour, light});
        out.push_back({x1, y0, z1, u1, v0, colour, light});
        out.push_back({x1, y1, z1, u1, v1, colour, light});
        out.push_back({x0, y1, z0, u0, v1, colour, light});
    }
}

// `WeatherEffectRenderer.tickRainParticles`: drops scattered over the square
// the weather radius covers, landing on whatever each column's top block is,
// and one rain sound every few ticks from wherever the last drop landed.
void tick_rain_particles(const RainTick& world, int32_t ticks, float rain_level,
                         const double camera[3], int32_t radius, int32_t& rain_sound_time) {
    if (rain_level <= 0.0f) return;
    jmath::LegacyRandom random(static_cast<int64_t>(ticks) * 312987231LL);
    const int32_t cx = floor_i(camera[0]);
    const int32_t cy = floor_i(camera[1]);
    const int32_t cz = floor_i(camera[2]);
    bool landed = false;
    int32_t last[3] = {0, 0, 0};
    const int32_t side = 2 * radius + 1;
    const int32_t area = side * side;
    const int32_t drops = static_cast<int32_t>(0.225f * static_cast<float>(area) * rain_level * rain_level) /
                          (world.detail == RainTick::Detail::Decreased ? 2 : 1);

    for (int32_t i = 0; i < drops; ++i) {
        const int32_t x = cx + random.next_int(side) - radius;
        const int32_t z = cz + random.next_int(side) - radius;
        const int32_t top = world.height(x, z);
        if (top <= world.min_y || top > cy + 10 || top < cy - 10) continue;
        if (world.precipitation(x, top, z) != Precipitation::Rain) continue;
        landed = true;
        last[0] = x;
        last[1] = top - 1;
        last[2] = z;
        if (world.detail == RainTick::Detail::Minimal) break;
        const double dx = random.next_double();
        const double dz = random.next_double();
        const double surface = world.surface(last[0], last[1], last[2], dx, dz);
        const bool hot = world.hot(last[0], last[1], last[2]);
        if (world.particle) {
            world.particle(hot, static_cast<double>(last[0]) + dx,
                           static_cast<double>(last[1]) + surface, static_cast<double>(last[2]) + dz);
        }
    }

    if (landed && random.next_int(3) < rain_sound_time++) {
        rain_sound_time = 0;
        if (!world.sound) return;
        // Heard from above when the drop landed over the camera's head and the
        // camera itself is under something.
        if (last[1] > cy + 1 && world.height(cx, cz) > cy) {
            world.sound("weather.rain.above", last[0], last[1], last[2], 0.1f, 0.5f);
        } else {
            world.sound("weather.rain", last[0], last[1], last[2], 0.2f, 1.0f);
        }
    }
}

} // namespace mcpp::world
