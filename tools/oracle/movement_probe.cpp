#include "client/player_rules.hpp"
#include "mcpp/world/physics.hpp"
#include "mcpp/world/shapes.hpp"

#include <bit>
#include <cstdint>
#include <iostream>
#include <string_view>

// The C++ half of MovementOracle: the same inputs in the same order, values only.
int main() {
    namespace w = mcpp::world;
    const double sensitivities[] = {0.0, 0.25, 0.5, 0.75, 1.0, 0.123};
    const double pixels[] = {1.0, -1.0, 3.0, 17.5, -250.0, 0.25};
    for (const double s : sensitivities) {
        for (const double p : pixels) {
            w::Body body;
            const double scale = client::mouse_scale(s);
            client::turn(body, p * scale, p * 0.5 * scale);
            std::cout << std::bit_cast<int32_t>(body.yaw) << ':' << std::bit_cast<int32_t>(body.pitch)
                      << '\n';
        }
    }

    const float speeds[] = {0.1f, 0.13f, 0.02f, 0.025999999f,
                            0.1f * 0.21600002f / (0.98f * 0.98f * 0.98f)};
    const float yaws[] = {0.0f, 45.0f, -90.0f, 123.456f, 3600.25f, -0.001f};
    const double inputs[][3] = {{0.98, 0.0, 0.0}, {0.0, 0.0, 0.98}, {0.70710677, 0.0, 0.70710677},
                                {-0.294, 0.0, 0.294}, {1.0, 0.0, 1.0}};
    for (const float speed : speeds) {
        for (const float yaw : yaws) {
            for (const auto& in : inputs) {
                double out[3];
                w::input_vector(in[0], in[1], in[2], speed, yaw, out);
                std::cout << std::bit_cast<int64_t>(out[0]) << ':' << std::bit_cast<int64_t>(out[1])
                          << ':' << std::bit_cast<int64_t>(out[2]) << '\n';
            }
        }
    }

    const std::string_view blocks[] = {
        "minecraft:stone", "minecraft:oak_slab", "minecraft:oak_stairs", "minecraft:oak_fence",
        "minecraft:cobblestone_wall", "minecraft:oak_door", "minecraft:oak_trapdoor",
        "minecraft:snow", "minecraft:white_carpet", "minecraft:soul_sand", "minecraft:honey_block",
        "minecraft:chest", "minecraft:hopper", "minecraft:cauldron", "minecraft:anvil",
        "minecraft:bamboo", "minecraft:ladder", "minecraft:glass_pane", "minecraft:lectern",
        "minecraft:scaffolding",
    };
    const int steps[] = {0, 1, 5, 11, 17, 29};
    const double distances[] = {0.5, -0.5, 0.08, -0.08, 1.2, -1.2, 3.9, -3.9};
    const double offsets[][3] = {
        {-0.7, 0.0, 0.5}, {1.7, 0.0, 0.5}, {0.5, 1.0, 0.5}, {0.5, -1.8, 0.5},
        {0.5, 0.0, -0.7}, {0.5, 0.0, 1.7}, {0.2, 0.4, -0.31}, {1.31, 0.5, 0.9},
    };
    const auto& shapes = w::BlockShapes::game();
    const int32_t px = 1000;
    const int32_t py = 64;
    const int32_t pz = -2000;
    for (const auto name : blocks) {
        const int32_t first = shapes.find(name);
        const int32_t count = shapes.block(first).count;
        for (const int step : steps) {
            if (step >= count) continue;
            for (const auto& o : offsets) {
                const double x = px + o[0];
                const double y = py + o[1];
                const double z = pz + o[2];
                const double half = static_cast<double>(0.3f);
                const w::Aabb box{{x - half, y, z - half},
                                  {x + half, y + static_cast<double>(1.8f), z + half}};
                for (int axis = 0; axis < 3; ++axis) {
                    for (const double d : distances) {
                        const double r =
                            w::collide_with_block(shapes, first + step, px, py, pz, axis, box, d);
                        std::cout << std::bit_cast<int64_t>(r) << '\n';
                    }
                }
            }
        }
    }
}
