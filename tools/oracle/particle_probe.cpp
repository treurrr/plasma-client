// The C++ half of ParticleOracle: the same broken blocks, digs and ticks
// through mcpp::world::ParticleEngine, with each particle seeded the way the
// oracle found the game's was. Reads the oracle's lines on stdin.
#include "mcpp/protocol_versions.hpp"
#include "mcpp/world/block_table.hpp"
#include "mcpp/world/particles.hpp"

#include <bit>
#include <deque>
#include <format>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace w = mcpp::world;

namespace {

std::string d(double v) { return std::to_string(std::bit_cast<int64_t>(v)); }
std::string f(float v) { return std::to_string(std::bit_cast<int32_t>(v)); }

std::string spawn(const w::Particle& p) {
    return std::format("{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}", d(p.x), d(p.y), d(p.z), d(p.xd), d(p.yd),
                       d(p.zd), p.lifetime, f(p.quad_size), f(p.uo), f(p.vo), f(p.r), f(p.g), f(p.b), d(p.bb.min[0]),
                       d(p.bb.max[1]));
}

std::string motion(const w::Particle& p) {
    return std::format("{}:{}:{}:{}:{}:{}:{}:{}", d(p.x), d(p.y), d(p.z), d(p.xd), d(p.yd), d(p.zd),
                       p.on_ground ? 1 : 0, p.alive() ? 1 : 0);
}

} // namespace

int main() {
    const int32_t protocol = mcpp::protocol::kTargetProtocol;
    w::BlockTable blocks;
    blocks.open(w::BlockTable::default_path(protocol));
    w::ParticleTable table;
    if (!table.open(w::ParticleTable::default_path(protocol), blocks)) {
        std::cerr << "no particle table; run tools/bake_particles.py\n";
        return 1;
    }
    const auto& shapes = w::BlockShapes::game();
    const int32_t stone = shapes.find("minecraft:stone");
    const int32_t at[3] = {100, 64, 200};
    int32_t placed = 0;
    const w::BlockAt block_at = [&](int32_t x, int32_t y, int32_t z) -> int32_t {
        if (x == at[0] && y == at[1] && z == at[2]) return placed;
        if (y == 63 && x >= 96 && x <= 104 && z >= 196 && z <= 204) return stone;
        if (x == 102 && y >= 64 && y <= 66 && z >= 196 && z <= 204) return stone;
        return 0;
    };
    const w::ParticleLevel level{block_at, shapes, table, [](int32_t, int32_t, int32_t) { return 0; },
                                 [](w::ParticleTable::Colour, int32_t, int32_t, int32_t) { return 0xFFFFFFu; },
                                 -64};

    std::vector<std::string> lines;
    for (std::string line; std::getline(std::cin, line);) {
        if (!line.empty()) lines.push_back(line);
    }
    const auto value = [](const std::string& line) { return line.substr(line.rfind(' ') + 1); };
    const auto input = [](const std::string& line) {
        const auto first = line.find(' ');
        return line.substr(first + 1, line.rfind(' ') - first - 1);
    };

    w::ParticleEngine engine;
    std::deque<int64_t> seeds;
    engine.next_seed = [&] {
        const int64_t s = seeds.front();
        seeds.pop_front();
        return s;
    };
    std::vector<w::Particle> made;
    const auto take = [&](size_t from, size_t count) {
        seeds.clear();
        for (size_t j = from; j < lines.size() && seeds.size() < count; ++j) {
            if (lines[j].starts_with("seed ")) seeds.push_back(std::stoll(value(lines[j])));
        }
    };
    const auto collect = [&] {
        engine.tick(level);
        made.clear();
        engine.each([&](const w::Particle& p) { made.push_back(p); });
    };

    for (size_t i = 0; i < lines.size(); ++i) {
        const auto& line = lines[i];
        if (line.starts_with("destroy ")) {
            engine.clear();
            placed = 0;
            const int32_t state = std::stoi(input(line));
            take(i + 1, std::stoul(value(line)));
            const int32_t pos[3] = {at[0], at[1], at[2]};
            engine.destroy(level, pos, state);
            collect();
            std::cout << made.size() << '\n';
        } else if (line.starts_with("crack ")) {
            engine.clear();
            std::stringstream parts(input(line));
            std::string part;
            std::vector<int64_t> numbers;
            while (std::getline(parts, part, ',')) numbers.push_back(std::stoll(part));
            placed = static_cast<int32_t>(numbers[0]);
            engine.level_random = mcpp::jmath::LegacyRandom(numbers[2]);
            take(i + 1, std::stoul(value(line)));
            engine.crack(level, at, static_cast<int>(numbers[1]));
            collect();
            placed = 0;
            std::cout << made.size() << '\n';
        } else if (line.starts_with("seed ")) {
            std::cout << value(line) << '\n';
        } else if (line.starts_with("spawn ")) {
            const auto name = input(line);
            const size_t n = std::stoul(name.substr(name.rfind('#') + 1));
            std::cout << (n < made.size() ? spawn(made[n]) : std::string("missing")) << '\n';
        } else if (line.starts_with("tick ")) {
            const auto name = input(line);
            const size_t n = std::stoul(name.substr(name.rfind('#') + 1, name.rfind('@') - name.rfind('#') - 1));
            if (n >= made.size()) {
                std::cout << "missing\n";
                continue;
            }
            w::BoxMover mover(w::World{block_at, shapes, nullptr, -64});
            made[n].tick(mover);
            std::cout << motion(made[n]) << '\n';
        }
    }
    return 0;
}
