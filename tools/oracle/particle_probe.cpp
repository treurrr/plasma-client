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

// What the oracle's `born` prints: the fields a particle is made with, its
// sprite given as which of its set was picked.
std::string born(const w::Particle& p, const w::ParticleSprites& sprites) {
    // Which of the set the sprite is: the sets share layers, so a set's
    // layers are not one run and the index has to be looked for.
    int32_t index = -1;
    for (size_t at = 0; at < sprites.size(p.sprite_set); ++at) {
        if (sprites.layer(p.sprite_set, at) == p.texture) {
            index = static_cast<int32_t>(at);
            break;
        }
    }
    const std::string sprite =
        p.layer == w::QuadLayer::Terrain ? std::string("-") : std::to_string(index);
    const bool quad = p.kind != w::ParticleKind::ExplosionSeed;
    return std::format("{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}", d(p.x), d(p.y), d(p.z), d(p.xd),
                       d(p.yd), d(p.zd), p.lifetime,
                       quad ? f(p.size_at(0.0f)) : std::string("-"),
                       quad ? std::format("{}:{}:{}:{}", f(p.r), f(p.g), f(p.b), f(p.alpha))
                            : std::string("-"),
                       quad ? sprite : std::string("-"), f(p.gravity), f(p.friction), d(p.bb.min[0]),
                       d(p.bb.max[1]));
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
    w::ParticleSprites sprites;
    if (!sprites.open(w::ParticleSprites::default_path(protocol),
                      static_cast<uint32_t>(blocks.texture_count()))) {
        std::cerr << "no particle sprites; run tools/bake_particle_sprites.py\n";
        return 1;
    }
    const w::ParticleLevel level{block_at, shapes, table, [](int32_t, int32_t, int32_t) { return 0; },
                                 [](w::ParticleTable::Colour, int32_t, int32_t, int32_t) { return 0xFFFFFFu; },
                                 -64, &sprites};

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
    std::vector<std::string> requests;
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
        } else if (line.starts_with("make ")) {
            engine.clear();
            placed = 0;
            std::stringstream parts(input(line));
            std::string name;
            std::string engine_seed;
            std::getline(parts, name, ',');
            std::getline(parts, engine_seed, ',');
            engine.level_random = mcpp::jmath::LegacyRandom(std::stoll(engine_seed));
            engine.spell_random = mcpp::jmath::LegacyRandom(std::stoll(engine_seed));
            take(i + 1, std::stoul(value(line)));
            const auto type = w::particle_type_from_name("minecraft:" + name);
            w::ParticleOptions options;
            if (name == "dust") {
                options.colour = 0xFF4C1A99u;
                options.scale = 1.5f;
            } else if (name == "dust_color_transition") {
                options.colour = 0xFF4C1A99u;
                options.to_colour = 0xFF19B2CCu;
                options.scale = 1.5f;
            } else if (name == "entity_effect") {
                options.colour = 0x80CC3366u;
            } else if (name == "effect") {
                options.colour = 0xFF2266CCu;
                options.power = 1.3f;
            } else if (name == "instant_effect") {
                options.colour = 0xFFCC2266u;
                options.power = 0.7f;
            }
            engine.spawn(level, type, options, 100.5, 65.3, 200.5, 0.2, 0.3, -0.1);
            collect();
            std::cout << made.size() << '\n';
        } else if (line.starts_with("born ")) {
            const auto name = input(line);
            const size_t n = std::stoul(name.substr(name.rfind('#') + 1));
            std::cout << (n < made.size() ? born(made[n], sprites) : std::string("missing")) << '\n';
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
            requests.clear();
            made[n].tick(mover, level, [&](const w::Particle::Spawn& s) {
                const auto full = w::particle_type_name(static_cast<w::ParticleType>(s.type));
                requests.push_back(std::format("{}:{}:{}:{}:{}:{}:{}",
                                               full.substr(full.find(':') + 1), d(s.x), d(s.y), d(s.z),
                                               d(s.xd), d(s.yd), d(s.zd)));
            });
            std::cout << motion(made[n]) << '\n';
        } else if (line.starts_with("emit ")) {
            if (requests.empty()) {
                std::cout << "-\n";
                continue;
            }
            std::string joined;
            for (const auto& request : requests) {
                if (!joined.empty()) joined += '|';
                joined += request;
            }
            std::cout << joined << '\n';
        }
    }
    return 0;
}
