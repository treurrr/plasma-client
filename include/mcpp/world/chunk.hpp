#pragma once
#include "mcpp/byte_buffer.hpp"

#include <cstdint>
#include <vector>

namespace mcpp::world {

constexpr size_t kBlocksPerSection = 16 * 16 * 16;
constexpr size_t kBiomesPerSection = 4 * 4 * 4;

// Entries are bit-packed into longs and never span two of them, so a long holds
// 64/bits entries and the leftover high bits are padding.
class PalettedContainer {
public:
    static PalettedContainer read(ByteBuffer& in, size_t entry_count, uint8_t max_indirect_bits);

    int32_t at(size_t index) const;
    // Writes one entry, growing the palette or leaving it behind for direct
    // storage when the value will not fit what is there.
    void set(size_t index, int32_t value, uint8_t max_indirect_bits);
    size_t size() const { return entry_count_; }
    uint8_t bits() const { return bits_; }
    bool uniform() const { return bits_ == 0; }
    const std::vector<int32_t>& palette() const { return palette_; }

private:
    // Where a palette stops paying for itself and the state is stored outright.
    static constexpr uint8_t kDirectBits = 15;

    void write_raw(size_t index, uint32_t raw);
    void rewrite(uint8_t bits);

    uint8_t bits_ = 0;
    size_t entry_count_ = 0;
    std::vector<int32_t> palette_;
    std::vector<uint64_t> data_;
};

struct ChunkSection {
    int16_t non_air_count = 0;
    PalettedContainer blocks;
    PalettedContainer biomes;
};

struct BlockEntity {
    int32_t x = 0;
    int32_t y = 0;
    int32_t z = 0;
    int32_t type = 0;
};

// Half a byte per block, as the server sends it: 4096 nibbles for a section.
// One slot per light section, which is the world sections plus one below and
// one above. An empty slot means the server said the whole section was dark.
struct LightLayer {
    std::vector<std::vector<uint8_t>> sections;
    uint8_t at(size_t slot, int32_t local_x, int32_t y, int32_t local_z) const;
};

struct Chunk {
    int32_t x = 0;
    int32_t z = 0;
    std::vector<ChunkSection> sections;
    std::vector<BlockEntity> block_entities;
    LightLayer sky_light;
    LightLayer block_light;

    size_t non_empty_sections() const;
    int32_t block_state(int32_t local_x, int32_t y_index, int32_t local_z) const;
    // True when the write landed; false when the position is outside the chunk.
    bool set_block_state(int32_t local_x, int32_t y_index, int32_t local_z, int32_t state);
    // Biomes are stored one per 4x4x4 cell, so this is the cell the block
    // falls in rather than the block itself.
    int32_t biome_at(int32_t local_x, int32_t y_index, int32_t local_z) const;
    // Light as the game stores it, 0 to 15. Outside the chunk, sky light is
    // full and block light is none, which is what an unloaded neighbour should
    // look like rather than a black wall.
    uint8_t sky_light_at(int32_t local_x, int32_t y_index, int32_t local_z) const;
    uint8_t block_light_at(int32_t local_x, int32_t y_index, int32_t local_z) const;
};

// Reads a level_chunk_with_light payload, light included.
Chunk read_chunk(ByteBuffer& in);
// The light half of a chunk packet on its own, which is what the server sends
// after a block changes. The chunk keeps its blocks and takes the new light.
void read_light_into(ByteBuffer& in, Chunk& chunk);

} // namespace mcpp::world
