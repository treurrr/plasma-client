#include "client/player_rules.hpp"
#include "mcpp/world/physics.hpp"
#include "mcpp/world/shape_edges.hpp"
#include "mcpp/world/shapes.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <format>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace {

// The drawn lines of an outline, each axis run merged as far as it goes, so
// two cuttings of the same shape read the same.
std::string edges_of(const mcpp::world::BlockShapes& shapes, uint16_t shape) {
    std::vector<std::array<double, 6>> segments;
    mcpp::world::for_all_edges(shapes, shape,
                               [&](double x0, double y0, double z0, double x1, double y1, double z1) {
                                   segments.push_back({x0, y0, z0, x1, y1, z1});
                               });
    std::set<std::string> out;
    for (int axis = 0; axis < 3; ++axis) {
        const int a = (axis + 1) % 3;
        const int b = (axis + 2) % 3;
        std::map<std::string, std::vector<std::pair<double, double>>> lines;
        for (const auto& s : segments) {
            if (s[static_cast<size_t>(axis)] == s[static_cast<size_t>(axis) + 3]) continue;
            if (s[static_cast<size_t>(a)] != s[static_cast<size_t>(a) + 3]) continue;
            if (s[static_cast<size_t>(b)] != s[static_cast<size_t>(b) + 3]) continue;
            const auto key = std::format("{},{}", std::bit_cast<int64_t>(s[static_cast<size_t>(a)]),
                                         std::bit_cast<int64_t>(s[static_cast<size_t>(b)]));
            lines[key].emplace_back(std::min(s[static_cast<size_t>(axis)], s[static_cast<size_t>(axis) + 3]),
                                    std::max(s[static_cast<size_t>(axis)], s[static_cast<size_t>(axis) + 3]));
        }
        for (auto& [key, spans] : lines) {
            std::ranges::sort(spans);
            double from = spans.front().first;
            double to = spans.front().second;
            for (size_t i = 1; i < spans.size(); ++i) {
                if (spans[i].first <= to) {
                    to = std::max(to, spans[i].second);
                } else {
                    out.insert(std::format("{}:{}:{}-{}", axis, key, std::bit_cast<int64_t>(from),
                                           std::bit_cast<int64_t>(to)));
                    from = spans[i].first;
                    to = spans[i].second;
                }
            }
            out.insert(std::format("{}:{}:{}-{}", axis, key, std::bit_cast<int64_t>(from),
                                   std::bit_cast<int64_t>(to)));
        }
    }
    if (out.empty()) return "none";
    std::string joined;
    for (const auto& line : out) {
        if (!joined.empty()) joined += ';';
        joined += line;
    }
    return joined;
}

} // namespace

// The C++ half of ClipOracle: the same rays at the same block, values only.
int main() {
    namespace w = mcpp::world;
    const auto& shapes = w::BlockShapes::game();
    if (!shapes.loaded()) return 1;

    const std::string_view blocks[] = {
        "minecraft:snow", "minecraft:oak_slab", "minecraft:oak_stairs", "minecraft:oak_fence",
        "minecraft:cobblestone_wall", "minecraft:white_carpet", "minecraft:glass_pane",
        "minecraft:white_bed", "minecraft:hopper", "minecraft:cauldron", "minecraft:stone",
        "minecraft:oak_trapdoor", "minecraft:scaffolding",
    };
    const int steps[] = {0, 1, 2, 3, 4, 5, 6, 7, 11, 17, 31};
    const double walks[][3] = {
        {-0.7, 0.0, 0.5}, {1.7, 0.0, 0.5}, {0.5, 0.45, 0.5}, {0.5, -1.8, 0.5},
        {0.5, 0.0, -0.7}, {0.5, 0.0, 1.7}, {0.2, 0.4, -0.31}, {1.31, 0.5, 0.9},
    };
    const double walk_distances[] = {0.5, -0.5, 0.08, -0.08, 1.2, -1.2};
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
            std::cout << edges_of(shapes, shapes.state(state).outline) << '\n';
            std::string collide;
            for (const auto& o : walks) {
                const double x = px + o[0];
                const double y = py + o[1];
                const double z = pz + o[2];
                const double half = static_cast<double>(0.3f);
                const w::Aabb box{{x - half, y, z - half},
                                  {x + half, y + static_cast<double>(1.8f), z + half}};
                for (int axis = 0; axis < 3; ++axis) {
                    for (const double d : walk_distances) {
                        const double r = w::collide_with_block(shapes, state, px, py, pz, axis, box, d);
                        if (!collide.empty()) collide += ':';
                        collide += std::to_string(std::bit_cast<int64_t>(r));
                    }
                }
            }
            std::cout << collide << '\n';
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
