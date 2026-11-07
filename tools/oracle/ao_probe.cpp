#include "mcpp/byte_buffer.hpp"
#include "mcpp/world/block_table.hpp"
#include "mcpp/world/chunk.hpp"
#include "mcpp/world/mesher.hpp"
#include "mcpp/world/render_table.hpp"

#include <array>
#include <cmath>
#include <format>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

// The C++ half of AoOracle: the mesher's own ambient occlusion and smooth
// lighting for the neighbourhoods in ao_cases.txt, keyed by where in the block
// the corner sits, values only.
namespace {

constexpr int32_t kCentre = 8;

mcpp::world::ChunkSection empty_section() {
    mcpp::ByteBuffer blocks;
    blocks.write_u8(0);
    blocks.write_varint(0);
    mcpp::ByteBuffer biomes;
    biomes.write_u8(0);
    biomes.write_varint(0);

    mcpp::world::ChunkSection section;
    section.non_air_count = 0;
    section.blocks = mcpp::world::PalettedContainer::read(blocks, 4096, 8);
    section.biomes = mcpp::world::PalettedContainer::read(biomes, 64, 3);
    return section;
}

struct Case {
    std::string name;
    std::string centre;
    int face = 1;
    std::vector<std::pair<std::array<int, 3>, std::string>> blocks;
    std::vector<std::pair<std::array<int, 3>, std::array<int, 2>>> lights;
};

std::array<int, 3> triple(const std::string& text) {
    std::array<int, 3> out{};
    std::istringstream fields(text);
    std::string part;
    for (int& value : out) {
        std::getline(fields, part, ',');
        value = std::stoi(part);
    }
    return out;
}

int face_id(const std::string& name) {
    if (name == "down") return 0;
    if (name == "up") return 1;
    if (name == "north") return 2;
    if (name == "south") return 3;
    if (name == "west") return 4;
    return 5;
}

std::vector<std::string> split(const std::string& text, char by) {
    std::vector<std::string> out;
    std::istringstream fields(text);
    std::string part;
    while (std::getline(fields, part, by)) {
        if (!part.empty()) out.push_back(part);
    }
    return out;
}

std::string trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t");
    if (first == std::string::npos) return {};
    return text.substr(first, text.find_last_not_of(" \t") - first + 1);
}

int32_t first_state(const mcpp::world::BlockTable& table, const std::string& name) {
    for (int32_t state = 0; state < static_cast<int32_t>(table.state_count()); ++state) {
        if (table.block_name(state) == name) return state;
    }
    return -1;
}

} // namespace

int main() {
    mcpp::world::BlockTable table;
    if (!table.open(mcpp::world::BlockTable::default_path(774))) {
        std::cerr << "no block table; bake one with tools/bake_all.py\n";
        return 1;
    }

    std::ifstream input("tools/oracle/ao_cases.txt");
    if (!input) {
        std::cerr << "tools/oracle/ao_cases.txt is missing\n";
        return 1;
    }

    mcpp::world::RenderTable render;
    render.open(mcpp::world::RenderTable::default_path(774));

    std::map<std::string, std::string> answers;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') continue;
        const auto groups = [&] {
            std::vector<std::string> out;
            size_t at = 0;
            for (size_t bar = line.find('|'); ; bar = line.find('|', at)) {
                if (bar == std::string::npos) {
                    out.push_back(line.substr(at));
                    break;
                }
                out.push_back(line.substr(at, bar - at));
                at = bar + 1;
            }
            return out;
        }();

        std::istringstream head(groups[0]);
        Case item;
        std::string face_name;
        std::string from_text;
        std::string to_text;
        head >> item.name >> item.centre >> face_name >> from_text >> to_text;
        item.face = face_id(face_name);
        const auto from = triple(from_text);
        const auto to = triple(to_text);
        if (groups.size() > 1) {
            for (const auto& entry : split(trim(groups[1]), ';')) {
                const auto at = entry.find('=');
                item.blocks.emplace_back(triple(entry.substr(0, at)), entry.substr(at + 1));
            }
        }
        if (groups.size() > 2) {
            for (const auto& entry : split(trim(groups[2]), ';')) {
                const auto at = entry.find('=');
                const auto slash = entry.find('/', at);
                item.lights.emplace_back(
                    triple(entry.substr(0, at)),
                    std::array<int, 2>{std::stoi(entry.substr(at + 1, slash - at - 1)),
                                       std::stoi(entry.substr(slash + 1))});
            }
        }

        mcpp::world::Chunk chunk;
        chunk.min_y = 0;
        chunk.sections.push_back(empty_section());
        const auto place = [&](int dx, int dy, int dz, const std::string& name) {
            const int32_t state = first_state(table, name);
            chunk.set_block_state(kCentre + dx, kCentre + dy, kCentre + dz, state);
        };
        place(0, 0, 0, item.centre);
        for (const auto& [at, name] : item.blocks) place(at[0], at[1], at[2], name);

        // Light the way the oracle's hand-built level does: dark inside a block
        // that stops light, open sky outside one, then the case's own values.
        // The light arrays hold a section below and above the chunk's own, so
        // the only section here is slot one.
        chunk.sky_light.sections.resize(3);
        chunk.block_light.sections.resize(3);
        for (int32_t y = 0; y < 16; ++y) {
            for (int32_t z = 0; z < 16; ++z) {
                for (int32_t x = 0; x < 16; ++x) {
                    const int32_t state = chunk.block_state(x, y, z);
                    const bool opaque = render.at(state).light_block >= 15;
                    chunk.sky_light.set(1, x, y, z, opaque ? 0 : 15);
                    chunk.block_light.set(1, x, y, z, 0);
                }
            }
        }
        for (const auto& [at, pair] : item.lights) {
            chunk.block_light.set(1, kCentre + at[0], kCentre + at[1], kCentre + at[2],
                                  static_cast<uint8_t>(pair[0]));
            chunk.sky_light.set(1, kCentre + at[0], kCentre + at[1], kCentre + at[2],
                                static_cast<uint8_t>(pair[1]));
        }

        const mcpp::world::ChunkBlocks blocks(chunk);
        const auto mesh = mcpp::world::mesh_chunk(chunk, blocks, table, {});

        // The quad that fills the case's own box: every other quad of the
        // block reaches outside it, and the cell the face looks into is empty
        // in every case, so no neighbour's quad shares the plane.
        const int normal_axis = item.face < 2 ? 1 : (item.face < 4 ? 2 : 0);
        for (size_t at = 0; at + 4 <= mesh.vertices.size(); at += 4) {
            float local[4][3];
            bool on_face = true;
            for (int corner = 0; corner < 4 && on_face; ++corner) {
                for (int axis = 0; axis < 3; ++axis) {
                    local[corner][axis] =
                        static_cast<float>(mesh.vertices[at + corner].position[axis]) /
                            static_cast<float>(mcpp::world::kPositionScale) -
                        static_cast<float>(mcpp::world::kPositionBias) -
                        static_cast<float>(kCentre);
                    if (local[corner][axis] < from[axis] / 16.0f - 0.001f ||
                        local[corner][axis] > to[axis] / 16.0f + 0.001f) {
                        on_face = false;
                    }
                }
            }
            if (!on_face) continue;
            // Four corners spanning the box, not an edge of some other quad.
            float reach = 0.0f;
            for (int corner = 0; corner < 4; ++corner) {
                for (int axis = 0; axis < 3; ++axis) {
                    if (axis == normal_axis) continue;
                    reach = std::max(reach, std::abs(local[corner][axis] - local[0][axis]));
                }
            }
            if (reach < 0.001f) continue;

            for (int corner = 0; corner < 4; ++corner) {
                const auto& vertex = mesh.vertices[at + corner];
                const auto key = std::format("{}.{}_{}_{}", item.name,
                                             std::lround(local[corner][0] * 16.0f),
                                             std::lround(local[corner][1] * 16.0f),
                                             std::lround(local[corner][2] * 16.0f));
                answers[key] = std::format("{}:{}:{}", vertex.packed >> 8,
                                           vertex.light & 0xFFu, (vertex.light >> 8) & 0xFFu);
            }
        }
    }

    while (std::getline(std::cin, line)) {
        std::istringstream fields(line);
        std::string tag;
        std::string key;
        fields >> tag >> key;
        if (key.empty()) continue;
        const auto found = answers.find(key);
        std::cout << (found == answers.end() ? std::string("missing") : found->second) << '\n';
    }
    return 0;
}
