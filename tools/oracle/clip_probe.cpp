#include "client/player_rules.hpp"
#include "mcpp/world/physics.hpp"
#include "mcpp/world/shapes.hpp"

#include <bit>
#include <cstdint>
#include <format>
#include <iostream>
#include <string_view>

// The C++ half of ClipOracle: the same rays at the same block, values only.
int main() {
    namespace w = mcpp::world;
    const auto& shapes = w::BlockShapes::game();
    if (!shapes.loaded()) return 1;

    const std::string_view blocks[] = {
        "minecraft:snow", "minecraft:oak_slab", "minecraft:oak_stairs", "minecraft:oak_fence",
        "minecraft:cobblestone_wall", "minecraft:white_carpet", "minecraft:glass_pane",
        "minecraft:white_bed", "minecraft:hopper", "minecraft:cauldron", "minecraft:stone",
    };
    const int steps[] = {0, 1, 3, 5, 7, 11, 17, 31};
    const double rays[][6] = {
        {0.5, 2.0, 0.5, 0.5, -0.5, 0.5},
        {0.5, -0.5, 0.5, 0.5, 2.0, 0.5},
        {-1.0, 0.0625, 0.5, 2.0, 0.0625, 0.5},
        {-1.0, 0.3, 0.5, 2.0, 0.3, 0.5},
        {-1.0, 0.6, 0.5, 2.0, 0.6, 0.5},
        {-1.0, 0.95, 0.5, 2.0, 0.95, 0.5},
        {0.5, 1.5, -1.0, 0.5, 0.05, 2.0},
        {-0.5, 1.5, -0.5, 1.5, -0.5, 1.5},
        {0.2, 0.05, 0.2, 0.9, 0.9, 0.9},
        {0.5, 0.5, 0.5, 0.5, 3.0, 0.5},
        {0.0, 0.125, -1.0, 0.0, 0.125, 2.0},
        {1.0, 0.5, 1.0, -1.0, 0.5, -1.0},
        {0.5, 3.0, 0.5, 0.5, 2.5, 0.5},
        {-1.0, 1.01, 0.5, 2.0, 0.99, 0.5},
    };
    const int32_t px = 3;
    const int32_t py = 64;
    const int32_t pz = -7;
    for (const auto name : blocks) {
        const int32_t first = shapes.find(name);
        const int32_t count = shapes.block(first).count;
        for (const int step : steps) {
            if (step >= count) continue;
            const int32_t state = first + step;
            const w::BlockAt at = [&](int32_t x, int32_t y, int32_t z) {
                return x == px && y == py && z == pz ? state : 0;
            };
            for (const auto& ray : rays) {
                const double from[3] = {px + ray[0], py + ray[1], pz + ray[2]};
                const double to[3] = {px + ray[3], py + ray[4], pz + ray[5]};
                const client::Hit hit = client::pick_block(shapes, at, from, to);
                if (!hit.hit) {
                    std::cout << "miss\n";
                    continue;
                }
                std::cout << std::format("hit:{}:{}:{}:{}:{}:{},{},{}\n", hit.face,
                                         hit.inside ? 1 : 0, std::bit_cast<int64_t>(hit.location[0]),
                                         std::bit_cast<int64_t>(hit.location[1]),
                                         std::bit_cast<int64_t>(hit.location[2]), hit.pos[0],
                                         hit.pos[1], hit.pos[2]);
            }
        }
    }
}
