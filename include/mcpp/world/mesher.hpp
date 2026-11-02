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

// Without these a chunk emits all four of its side walls, because every edge
// block then looks like it borders open air.
struct Neighbours {
    const Chunk* west = nullptr;    // x - 1
    const Chunk* east = nullptr;    // x + 1
    const Chunk* north = nullptr;   // z - 1
    const Chunk* south = nullptr;   // z + 1
};

// Positions are in blocks, relative to the chunk's own corner. A face whose
// cullface points at a neighbouring full cube is dropped.
Mesh mesh_chunk(const Chunk& chunk, const BlockTable& table, const Neighbours& neighbours = {},
                int32_t min_section_y = -4);

} // namespace mcpp::world
