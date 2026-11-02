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
    size_t size() const { return entry_count_; }
    uint8_t bits() const { return bits_; }
    bool uniform() const { return bits_ == 0; }
    const std::vector<int32_t>& palette() const { return palette_; }

private:
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

struct Chunk {
    int32_t x = 0;
    int32_t z = 0;
    std::vector<ChunkSection> sections;
    std::vector<BlockEntity> block_entities;

    size_t non_empty_sections() const;
    int32_t block_state(int32_t local_x, int32_t y_index, int32_t local_z) const;
};

// Reads a level_chunk_with_light payload up to the light data, which is skipped.
Chunk read_chunk(ByteBuffer& in);

} // namespace mcpp::world
