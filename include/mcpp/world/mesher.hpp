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
    // Tint kind in the low three bits, the corner's own brightness - ambient
    // occlusion already multiplied by the face's shade - in the high byte.
    uint16_t packed;
    uint16_t uv[2];         // normalised across the texture
    uint16_t texture;       // array layer
    // The game's packed light, blended per corner: block light in the low
    // byte, sky light in the high one, both over 255 rather than over 15, so
    // the quarter steps a corner blend produces survive to the lightmap.
    uint16_t light;
    // The tint the three bits in `packed` ask for, already blended over the
    // square the biome blend radius covers, as 0x00RRGGBB. It travels in the
    // vertex rather than as a biome index because the average is not any one
    // biome's colour.
    uint32_t tint;

    static uint16_t encode(float blocks) {
        const float shifted = (blocks + static_cast<float>(kPositionBias)) *
                              static_cast<float>(kPositionScale);
        const auto rounded = static_cast<int32_t>(shifted + 0.5f);
        return static_cast<uint16_t>(std::clamp(rounded, 0, 65535));
    }

    void set_brightness(float brightness) {
        const auto value = static_cast<uint32_t>(
            std::clamp(brightness, 0.0f, 1.0f) * 255.0f + 0.5f);
        packed = static_cast<uint16_t>((packed & 0x00FFu) | (value << 8));
    }
};
static_assert(sizeof(Vertex) == 20, "the vertex layout must match the pipeline");

// Geometry for one 16x16x16 section, with positions relative to its own corner.
// Per section rather than per chunk because that is the unit the renderer can
// cull, upload and throw away on its own.
struct SectionMesh {
    int32_t origin[3] = {0, 0, 0};
    float bounds_min[3] = {0.0f, 0.0f, 0.0f};
    float bounds_max[3] = {0.0f, 0.0f, 0.0f};
    std::vector<Vertex> vertices;
    // Opaque first, then translucent. Blending has to happen after everything
    // solid has been drawn, so the two are one buffer with a split rather than
    // two buffers.
    std::vector<uint32_t> indices;
    size_t opaque_indices = 0;

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
    size_t opaque_indices = 0;

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
    // Sky light in the low nibble, block light in the high one, which is how
    // it reaches the vertex and the shader.
    uint8_t light_at(int32_t x, int32_t y, int32_t z) const;
    // The biome registry index, as the chunk's own palette gives it.
    uint16_t biome_at(int32_t x, int32_t y, int32_t z) const;
    size_t section_count() const { return sections_.size(); }

private:
    std::vector<std::vector<int32_t>> sections_;   // an empty section is all air
    std::vector<uint8_t> light_;
    std::vector<uint16_t> biomes_;
};

// Without these a chunk emits all four of its side walls, because every edge
// block then looks like it borders open air.
struct Neighbours {
    const ChunkBlocks* west = nullptr;    // x - 1
    const ChunkBlocks* east = nullptr;    // x + 1
    const ChunkBlocks* north = nullptr;   // z - 1
    const ChunkBlocks* south = nullptr;   // z + 1
    // Only the biome blend reads these: its square leaves the chunk on both
    // axes at once at a corner, and nothing else samples a diagonal that far.
    const ChunkBlocks* north_west = nullptr;
    const ChunkBlocks* north_east = nullptr;
    const ChunkBlocks* south_west = nullptr;
    const ChunkBlocks* south_east = nullptr;
};

// The colours the game tints with and how wide it averages them.
struct BiomeTints {
    // Four per biome - grass, foliage, water, dry foliage, each 0xRRGGBB - in
    // the order the server listed the biomes, which is `BiomeTable::tints`.
    std::vector<uint32_t> colour;
    // options.txt's biomeBlendRadius: 0 for the block's own biome, 2 by
    // default for the 5x5 square around it.
    int32_t blend_radius = 2;

    bool empty() const { return colour.empty(); }
};

// The four tints a quad's tint index can ask for, in the order a biome's row
// holds them, and which index asks for which. -1 is one of the fixed tints
// (spruce, birch, lily pad) or none, which the shader has its own table for.
constexpr int kTintChannels = 4;
constexpr int kTintChannel[8] = {-1, 0, 1, 2, -1, -1, -1, 3};
// What a caller with no biome table gets: the colours the game falls back to.
constexpr uint32_t kDefaultTint[kTintChannels] = {0x91BD59u, 0x77AB2Fu, 0x3F76E4u, 0x77AB2Fu};

// `Biome.getGrassColor`, `Biome.getFoliageColor`, `Biome.getWaterColor`: what
// one biome contributes to one channel, resolved by `BiomeTable::tints`.
inline uint32_t biome_tint(const BiomeTints& tints, uint16_t biome, int channel) {
    const size_t at = static_cast<size_t>(biome) * kTintChannels + static_cast<size_t>(channel);
    return at < tints.colour.size() ? tints.colour[at] : kDefaultTint[channel];
}

// `ClientLevel.calculateBlockTint`, `BiomeColors.getAverageColor`: a channel
// averaged over the (2r+1)x(2r+1) square of blocks around one, each channel
// summed as an int and divided by the area, which truncates. r is the
// biomeBlendRadius option: at 0 the block's own biome decides alone, and that
// is the hard line a border used to be.
template <typename BiomeAt>
uint32_t blend_biome_tint(const BiomeTints& tints, int channel, int32_t x, int32_t z,
                          BiomeAt&& biome_at) {
    const int32_t radius = std::clamp(tints.blend_radius, 0, 7);
    if (radius == 0) return biome_tint(tints, biome_at(x, z), channel);
    int32_t red = 0;
    int32_t green = 0;
    int32_t blue = 0;
    for (int32_t dz = -radius; dz <= radius; ++dz) {
        for (int32_t dx = -radius; dx <= radius; ++dx) {
            const uint32_t colour = biome_tint(tints, biome_at(x + dx, z + dz), channel);
            red += static_cast<int32_t>((colour & 0xFF0000u) >> 16);
            green += static_cast<int32_t>((colour & 0xFF00u) >> 8);
            blue += static_cast<int32_t>(colour & 0xFFu);
        }
    }
    const int32_t area = (radius * 2 + 1) * (radius * 2 + 1);
    return static_cast<uint32_t>((red / area & 0xFF) << 16 | (green / area & 0xFF) << 8 |
                                 (blue / area & 0xFF));
}

// One mesh per non-empty section, with positions relative to that section's
// corner. A face whose cullface points at a neighbouring full cube is dropped.
std::vector<SectionMesh> mesh_sections(const Chunk& chunk, const ChunkBlocks& blocks,
                                       const BlockTable& table, const Neighbours& neighbours = {},
                                       const BiomeTints& tints = {});

// The same geometry merged into one buffer, in world coordinates. Convenient
// for a one-off render or a test; the renderer wants the sections.
Mesh mesh_chunk(const Chunk& chunk, const ChunkBlocks& blocks, const BlockTable& table,
                const Neighbours& neighbours = {}, const BiomeTints& tints = {});

// One baked quad's corners in the block, in the order the mesher emits them,
// turned the way its model and blockstate turn it, and the face it ends up on.
void quad_corners(const BakedQuad& quad, float out[4][3], int& face);
// The face a neighbour must cover to hide the quad, after the turns, or -1.
int quad_cull_face(const BakedQuad& quad);

} // namespace mcpp::world
