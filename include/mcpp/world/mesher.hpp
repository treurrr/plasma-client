#pragma once
#include "mcpp/world/block_table.hpp"
#include "mcpp/world/chunk.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace mcpp::world {

// 16 bytes. Positions are section-local, so they fit in fixed point: 1/2048 of
// a block, shifted by one block because a model may stick out on either side.
// That is coarser than the baked geometry's 1/4096, which is 0.0005 of a block
// here - far below one screen pixel at any sane distance.
constexpr int32_t kPositionScale = 2048;
constexpr int32_t kPositionBias = 1;      // in blocks

struct Vertex {
    uint16_t position[3];
    uint16_t packed;        // face | tint kind | corner shadow
    uint16_t uv[2];         // normalised across the texture
    uint16_t texture;       // array layer
    uint16_t light;         // reserved: sky and block light

    static uint16_t encode(float blocks) {
        const float shifted = (blocks + static_cast<float>(kPositionBias)) *
                              static_cast<float>(kPositionScale);
        const auto rounded = static_cast<int32_t>(shifted + 0.5f);
        return static_cast<uint16_t>(std::clamp(rounded, 0, 65535));
    }
};
static_assert(sizeof(Vertex) == 16, "the vertex layout must match the pipeline");

// Geometry for one 16x16x16 section, with positions relative to its own corner.
// Per section rather than per chunk because that is the unit the renderer can
// cull, upload and throw away on its own.
struct SectionMesh {
    int32_t origin[3] = {0, 0, 0};
    float bounds_min[3] = {0.0f, 0.0f, 0.0f};
    float bounds_max[3] = {0.0f, 0.0f, 0.0f};
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    size_t quads_emitted = 0;
    size_t quads_culled = 0;

    size_t triangles() const { return indices.size() / 3; }
    size_t bytes() const {
        return vertices.size() * sizeof(Vertex) + indices.size() * sizeof(uint32_t);
    }
};

struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    size_t quads_emitted = 0;
    size_t quads_culled = 0;
    size_t blocks_visited = 0;

    size_t triangles() const { return indices.size() / 3; }
    size_t bytes() const {
        return vertices.size() * sizeof(Vertex) + indices.size() * sizeof(uint32_t);
    }
};

// A chunk's blocks laid out flat. Every block is read 27 times while meshing,
// for occlusion and ambient occlusion, and each read would otherwise divide,
// shift, mask and follow a palette. Unpacking once turns that into an index.
// It lives outside the mesher because neighbouring chunks are meshed too and
// should not be unpacked again for each of them.
class ChunkBlocks {
public:
    explicit ChunkBlocks(const Chunk& chunk);

    int32_t at(int32_t x, int32_t y, int32_t z) const;
    size_t section_count() const { return sections_.size(); }

private:
    std::vector<std::vector<int32_t>> sections_;   // an empty section is all air
};

// Without these a chunk emits all four of its side walls, because every edge
// block then looks like it borders open air.
struct Neighbours {
    const ChunkBlocks* west = nullptr;    // x - 1
    const ChunkBlocks* east = nullptr;    // x + 1
    const ChunkBlocks* north = nullptr;   // z - 1
    const ChunkBlocks* south = nullptr;   // z + 1
};

// One mesh per non-empty section, with positions relative to that section's
// corner. A face whose cullface points at a neighbouring full cube is dropped.
std::vector<SectionMesh> mesh_sections(const Chunk& chunk, const ChunkBlocks& blocks,
                                       const BlockTable& table, const Neighbours& neighbours = {},
                                       int32_t min_section_y = -4);

// The same geometry merged into one buffer, in world coordinates. Convenient
// for a one-off render or a test; the renderer wants the sections.
Mesh mesh_chunk(const Chunk& chunk, const ChunkBlocks& blocks, const BlockTable& table,
                const Neighbours& neighbours = {}, int32_t min_section_y = -4);

} // namespace mcpp::world
