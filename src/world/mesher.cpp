#include "mcpp/world/mesher.hpp"

#include <array>
#include <cmath>

namespace mcpp::world {
namespace {

constexpr float kBlock = static_cast<float>(kUnitsPerBlock);
constexpr int kSection = 16;

struct Offset {
    int x, y, z;
};

// Index order matches the baked face ids: down, up, north, south, west, east.
constexpr Offset kFaceOffset[6] = {
    {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0},
};

// Corner order is counter-clockwise seen from outside the box.
constexpr int kCorner[6][4][3] = {
    {{0, 0, 1}, {1, 0, 1}, {1, 0, 0}, {0, 0, 0}},   // down
    {{0, 1, 0}, {1, 1, 0}, {1, 1, 1}, {0, 1, 1}},   // up
    {{1, 1, 0}, {0, 1, 0}, {0, 0, 0}, {1, 0, 0}},   // north
    {{0, 1, 1}, {1, 1, 1}, {1, 0, 1}, {0, 0, 1}},   // south
    {{0, 1, 0}, {0, 1, 1}, {0, 0, 1}, {0, 0, 0}},   // west
    {{1, 1, 1}, {1, 1, 0}, {1, 0, 0}, {1, 0, 1}},   // east
};

constexpr float kCornerUv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};

// A quarter turn about the block centre, in the same order the assets use.
void rotate_quarter_turns(float& a, float& b, int turns) {
    for (int i = 0; i < turns; ++i) {
        const float old_a = a;
        a = b;
        b = -old_a;
    }
}

void apply_rotation(float position[3], uint8_t rotation_x, uint8_t rotation_y) {
    if (rotation_x == 0 && rotation_y == 0) return;
    float x = position[0] - 0.5f;
    float y = position[1] - 0.5f;
    float z = position[2] - 0.5f;
    if (rotation_x) rotate_quarter_turns(y, z, rotation_x);
    if (rotation_y) rotate_quarter_turns(z, x, rotation_y);
    position[0] = x + 0.5f;
    position[1] = y + 0.5f;
    position[2] = z + 0.5f;
}

int rotate_face(int face, uint8_t rotation_x, uint8_t rotation_y) {
    if (rotation_x == 0 && rotation_y == 0) return face;
    Offset offset = kFaceOffset[face];
    float x = static_cast<float>(offset.x);
    float y = static_cast<float>(offset.y);
    float z = static_cast<float>(offset.z);
    if (rotation_x) rotate_quarter_turns(y, z, rotation_x);
    if (rotation_y) rotate_quarter_turns(z, x, rotation_y);

    for (int i = 0; i < 6; ++i) {
        if (std::lround(x) == kFaceOffset[i].x && std::lround(y) == kFaceOffset[i].y &&
            std::lround(z) == kFaceOffset[i].z) {
            return i;
        }
    }
    return face;
}

} // namespace

Mesh mesh_chunk(const Chunk& chunk, const BlockTable& table, int32_t min_section_y) {
    Mesh mesh;
    mesh.vertices.reserve(1 << 16);
    mesh.indices.reserve(1 << 16);
    const auto section_count = static_cast<int32_t>(chunk.sections.size());

    const auto state_at = [&](int32_t x, int32_t y, int32_t z) -> int32_t {
        if (x < 0 || x > 15 || z < 0 || z > 15) return -1;   // another chunk: treat as open
        const int32_t section = y / kSection;
        if (y < 0 || section >= section_count) return -1;
        const size_t index = static_cast<size_t>(y % kSection) * 256 +
                             static_cast<size_t>(z) * 16 + static_cast<size_t>(x);
        return chunk.sections[static_cast<size_t>(section)].blocks.at(index);
    };

    for (int32_t section = 0; section < section_count; ++section) {
        if (chunk.sections[static_cast<size_t>(section)].non_air_count == 0) continue;

        for (int32_t local_y = 0; local_y < kSection; ++local_y) {
            const int32_t y = section * kSection + local_y;
            for (int32_t z = 0; z < kSection; ++z) {
                for (int32_t x = 0; x < kSection; ++x) {
                    const int32_t state = state_at(x, y, z);
                    if (state < 0) continue;
                    const auto quads = table.quads(state);
                    if (quads.empty()) continue;
                    ++mesh.blocks_visited;

                    bool opaque_neighbour[6];
                    for (int side = 0; side < 6; ++side) {
                        const Offset offset = kFaceOffset[side];
                        const int32_t neighbour = state_at(x + offset.x, y + offset.y, z + offset.z);
                        opaque_neighbour[side] = neighbour >= 0 && table.full_cube(neighbour);
                    }

                    const float world_y = static_cast<float>(y + min_section_y * kSection);

                    for (const auto& quad : quads) {
                        if (quad.cullface >= 0) {
                            const int culling_face =
                                rotate_face(quad.cullface, quad.rotation_x, quad.rotation_y);
                            if (opaque_neighbour[culling_face]) {
                                ++mesh.quads_culled;
                                continue;
                            }
                        }

                        const auto base = static_cast<uint32_t>(mesh.vertices.size());
                        const int face = rotate_face(quad.face, quad.rotation_x, quad.rotation_y);

                        for (int corner = 0; corner < 4; ++corner) {
                            const int* pick = kCorner[quad.face][corner];
                            float local[3] = {
                                (pick[0] ? quad.to[0] : quad.from[0]) / kBlock,
                                (pick[1] ? quad.to[1] : quad.from[1]) / kBlock,
                                (pick[2] ? quad.to[2] : quad.from[2]) / kBlock,
                            };
                            apply_rotation(local, quad.rotation_x, quad.rotation_y);

                            Vertex vertex{};
                            vertex.position[0] = static_cast<float>(x) + local[0];
                            vertex.position[1] = world_y + local[1];
                            vertex.position[2] = static_cast<float>(z) + local[2];
                            vertex.uv[0] = (quad.uv[0] + (quad.uv[2] - quad.uv[0]) *
                                                             kCornerUv[corner][0]) /
                                           (16.0f * kUnitsPerModelUnit);
                            vertex.uv[1] = (quad.uv[1] + (quad.uv[3] - quad.uv[1]) *
                                                             kCornerUv[corner][1]) /
                                           (16.0f * kUnitsPerModelUnit);
                            vertex.texture = quad.texture;
                            vertex.face = static_cast<uint32_t>(face);
                            mesh.vertices.push_back(vertex);
                        }

                        mesh.indices.insert(mesh.indices.end(),
                                            {base, base + 1, base + 2, base, base + 2, base + 3});
                        ++mesh.quads_emitted;
                    }
                }
            }
        }
    }
    return mesh;
}

} // namespace mcpp::world
