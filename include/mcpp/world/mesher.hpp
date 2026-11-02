#pragma once
#include "mcpp/world/block_table.hpp"
#include "mcpp/world/chunk.hpp"

#include <cstdint>
#include <vector>

namespace mcpp::world {

struct Vertex {
    float position[3];
    float uv[2];
    uint32_t texture;
    uint32_t face;
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

// Positions are in blocks, relative to the chunk's own corner. A face whose
// cullface points at a neighbouring full cube is dropped.
Mesh mesh_chunk(const Chunk& chunk, const ChunkBlocks& blocks, const BlockTable& table,
                const Neighbours& neighbours = {}, int32_t min_section_y = -4);

} // namespace mcpp::world
