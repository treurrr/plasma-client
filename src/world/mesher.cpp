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

// The two axes that vary across each face; the third is the face's own normal.
constexpr int kFaceAxes[6][2] = {{0, 2}, {0, 2}, {0, 1}, {0, 1}, {1, 2}, {1, 2}};

// Minecraft's smooth lighting: a corner is darkest when both edge neighbours
// are solid, and then the diagonal no longer matters because it cannot be seen.
int corner_shadow(bool side_a, bool side_b, bool diagonal) {
    if (side_a && side_b) return 0;
    return 3 - (static_cast<int>(side_a) + static_cast<int>(side_b) +
                static_cast<int>(diagonal));
}

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

ChunkBlocks::ChunkBlocks(const Chunk& chunk) : sections_(chunk.sections.size()) {
    for (size_t i = 0; i < chunk.sections.size(); ++i) {
        // Most of a column is air; leaving those sections empty saves both the
        // unpacking and the memory.
        if (chunk.sections[i].non_air_count == 0) continue;

        const auto& blocks = chunk.sections[i].blocks;
        auto& flat = sections_[i];
        flat.resize(kBlocksPerSection);
        for (size_t at = 0; at < kBlocksPerSection; ++at) flat[at] = blocks.at(at);
    }
}

int32_t ChunkBlocks::at(int32_t x, int32_t y, int32_t z) const {
    if (y < 0) return -1;
    const auto section = static_cast<size_t>(y / kSection);
    if (section >= sections_.size()) return -1;
    const auto& flat = sections_[section];
    if (flat.empty()) return 0;
    return flat[static_cast<size_t>(y % kSection) * 256 + static_cast<size_t>(z) * 16 +
                static_cast<size_t>(x)];
}

Mesh mesh_chunk(const Chunk& chunk, const ChunkBlocks& blocks, const BlockTable& table,
                const Neighbours& neighbours, int32_t min_section_y) {
    Mesh mesh;
    mesh.vertices.reserve(1 << 16);
    mesh.indices.reserve(1 << 16);

    const auto section_count = static_cast<int32_t>(blocks.section_count());

    const auto state_at = [&](int32_t x, int32_t y, int32_t z) -> int32_t {
        const bool past_x = x < 0 || x > 15;
        const bool past_z = z < 0 || z > 15;

        // Ambient occlusion also samples diagonals, which can leave the chunk on
        // two axes at once; that chunk is not among the four we are given.
        if (past_x && past_z) return -1;

        if (past_x) {
            const ChunkBlocks* side = x < 0 ? neighbours.west : neighbours.east;
            return side ? side->at(x & 15, y, z) : -1;
        }
        if (past_z) {
            const ChunkBlocks* side = z < 0 ? neighbours.north : neighbours.south;
            return side ? side->at(x, y, z & 15) : -1;
        }
        return blocks.at(x, y, z);
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

                    // A fluid also hides against its own kind, or an ocean
                    // would be full of faces between neighbouring water blocks.
                    // One 3x3x3 read per block instead of a lookup per face and
                    // per corner; occlusion and ambient occlusion both use it.
                    bool solid[3][3][3];
                    for (int dx = -1; dx <= 1; ++dx) {
                        for (int dy = -1; dy <= 1; ++dy) {
                            for (int dz = -1; dz <= 1; ++dz) {
                                const int32_t around = state_at(x + dx, y + dy, z + dz);
                                solid[dx + 1][dy + 1][dz + 1] =
                                    around >= 0 && table.occludes(around);
                            }
                        }
                    }

                    const bool self_fluid = table.fluid(state);
                    // Only the top block of a fluid column has a lowered
                    // surface; a submerged one fills its block, or the column
                    // would be sliced by a gap at every level.
                    const bool submerged =
                        self_fluid && table.fluid(state_at(x, y + 1, z));
                    bool hidden[6];
                    for (int side = 0; side < 6; ++side) {
                        const Offset offset = kFaceOffset[side];
                        if (solid[offset.x + 1][offset.y + 1][offset.z + 1]) {
                            hidden[side] = true;
                            continue;
                        }
                        if (!self_fluid) {
                            hidden[side] = false;
                            continue;
                        }
                        const int32_t neighbour = state_at(x + offset.x, y + offset.y, z + offset.z);
                        hidden[side] = neighbour >= 0 && table.fluid(neighbour);
                    }

                    const float world_y = static_cast<float>(y + min_section_y * kSection);

                    for (const auto& quad : quads) {
                        if (quad.cullface >= 0) {
                            const int culling_face =
                                rotate_face(quad.cullface, quad.rotation_x, quad.rotation_y);
                            if (hidden[culling_face]) {
                                ++mesh.quads_culled;
                                continue;
                            }
                        }

                        const auto base = static_cast<uint32_t>(mesh.vertices.size());
                        const int face = rotate_face(quad.face, quad.rotation_x, quad.rotation_y);
                        const Offset normal = kFaceOffset[face];
                        const int axis_a = kFaceAxes[face][0];
                        const int axis_b = kFaceAxes[face][1];

                        for (int corner = 0; corner < 4; ++corner) {
                            const int* pick = kCorner[quad.face][corner];
                            const float top = submerged ? kBlock : quad.to[1];
                            float local[3] = {
                                (pick[0] ? quad.to[0] : quad.from[0]) / kBlock,
                                (pick[1] ? top : quad.from[1]) / kBlock,
                                (pick[2] ? quad.to[2] : quad.from[2]) / kBlock,
                            };
                            apply_rotation(local, quad.rotation_x, quad.rotation_y);

                            int away[3] = {normal.x, normal.y, normal.z};
                            const int sign_a = pick[axis_a] ? 1 : -1;
                            const int sign_b = pick[axis_b] ? 1 : -1;

                            int probe[3] = {away[0], away[1], away[2]};
                            probe[axis_a] += sign_a;
                            const bool side_a = solid[probe[0] + 1][probe[1] + 1][probe[2] + 1];

                            probe[0] = away[0]; probe[1] = away[1]; probe[2] = away[2];
                            probe[axis_b] += sign_b;
                            const bool side_b = solid[probe[0] + 1][probe[1] + 1][probe[2] + 1];

                            probe[axis_a] += sign_a;
                            const bool diagonal = solid[probe[0] + 1][probe[1] + 1][probe[2] + 1];

                            const auto shadow =
                                static_cast<uint32_t>(corner_shadow(side_a, side_b, diagonal));

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
                            if (quad.tintindex == 0) vertex.face |= 0x100u;   // grass, foliage
                            if (quad.tintindex == 1) vertex.face |= 0x200u;   // water
                            vertex.face |= shadow << 16;
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
