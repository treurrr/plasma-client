#include "mcpp/jmath.hpp"
#include "mcpp/world/weather.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

// Java's Float.floatToIntBits: the oracle reads its floats through it, and it
// hands every NaN back as the one quiet NaN. The camera's own column divides
// zero by zero, so the sign of that NaN is the machine's, not the game's.
int32_t bits(float value) {
    return std::isnan(value) ? 0x7FC00000 : std::bit_cast<int32_t>(value);
}

// The C++ half of WeatherOracle: the same columns, clocks and cameras in the
// same order, values only.
int main() {
    using namespace mcpp::world;

    const int32_t columns[][2] = {
        {0, 0}, {1, 0}, {-1, 0}, {0, 1}, {0, -1}, {7, -3}, {-12, 9}, {131, -47},
        {-1000, 1000}, {46341, -46341}, {1234567, 7654321}, {-2147483, 2147483},
    };
    const int32_t ticks_list[] = {0, 1, 7, 100, 1023, 131071, 131072, 524287, 1000000};
    const float partials[] = {0.0f, 0.25f, 0.5f, 0.99f};
    const int32_t lights[] = {0, 15728880, 983040, 240, 15 << 4 | 7 << 20, 3 << 4 | 11 << 20};

    for (size_t i = 0; i < 1024; ++i) {
        std::cout << bits(column_size_x()[i]) << '\n';
        std::cout << bits(column_size_z()[i]) << '\n';
    }

    int n = 0;
    for (const auto& column : columns) {
        const int32_t x = column[0];
        const int32_t z = column[1];
        std::cout << column_seed(x, z) << '\n';
        for (const int32_t ticks : ticks_list) {
            const float partial = partials[static_cast<size_t>(n) % std::size(partials)];
            const int32_t light = lights[static_cast<size_t>(n) % std::size(lights)];
            const int32_t bottom = -3 + (n % 7) * 11;
            const int32_t top = bottom + 1 + (n % 23);
            ++n;

            mcpp::jmath::LegacyRandom rain_random(column_seed(x, z));
            const WeatherColumn rain =
                rain_column(rain_random, ticks, x, bottom, top, z, light, partial);
            std::cout << bits(rain.u_offset) << '\n';
            std::cout << bits(rain.v_offset) << '\n';
            std::cout << rain.light << '\n';
            mcpp::jmath::LegacyRandom snow_random(column_seed(x, z));
            const WeatherColumn snow =
                snow_column(snow_random, ticks, x, bottom, top, z, light, partial);
            std::cout << bits(snow.u_offset) << '\n';
            std::cout << bits(snow.v_offset) << '\n';
            std::cout << snow.light << '\n';

        }
    }

    // Columns close enough to the camera for the quad pass, which indexes a
    // 32 by 32 table around the camera's own column.
    std::vector<WeatherColumn> made;
    int m = 0;
    for (int32_t x = -7; x <= 7; x += 3) {
        for (int32_t z = -7; z <= 7; z += 3) {
            const int32_t ticks = ticks_list[static_cast<size_t>(m) % std::size(ticks_list)];
            const float partial = partials[static_cast<size_t>(m) % std::size(partials)];
            const int32_t light = lights[static_cast<size_t>(m) % std::size(lights)];
            const int32_t bottom = 54 + (m % 7);
            const int32_t top = bottom + 1 + (m % 23);
            ++m;
            mcpp::jmath::LegacyRandom a(column_seed(x, z));
            made.push_back(rain_column(a, ticks, x, bottom, top, z, light, partial));
            mcpp::jmath::LegacyRandom b(column_seed(x, z));
            made.push_back(snow_column(b, ticks, x, bottom, top, z, light, partial));
        }
    }

    const double cameras[][3] = {
        {0.5, 70.0, 0.5}, {-0.5, 64.3, 0.5}, {3.25, 100.75, -3.5}, {7.5, 61.0, -7.5},
    };
    const float alphas[] = {1.0f, 0.8f};
    const int32_t radii[] = {5, 10, 15};
    const float intensities[] = {1.0f, 0.35f};
    int run = 0;
    for (const auto& camera : cameras) {
        for (const float alpha : alphas) {
            const int32_t radius = radii[static_cast<size_t>(run) % std::size(radii)];
            const float intensity = intensities[static_cast<size_t>(run) % std::size(intensities)];
            ++run;
            std::vector<WeatherVertex> quads;
            weather_quads(made, camera, alpha, radius, intensity, quads);
            for (const auto& v : quads) {
                std::cout << bits(v.x) << '\n'
                          << bits(v.y) << '\n'
                          << bits(v.z) << '\n'
                          << bits(v.u) << '\n'
                          << bits(v.v) << '\n'
                          << static_cast<int32_t>(v.colour) << '\n'
                          << static_cast<int32_t>(v.light) << '\n';
            }
        }
    }
}
