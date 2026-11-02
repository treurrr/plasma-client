#include "mcpp/world/chunk.hpp"

#include "mcpp/nbt.hpp"

#include <format>

namespace mcpp::world {
namespace {

constexpr uint8_t kMaxBlockIndirectBits = 8;
constexpr uint8_t kMaxBiomeIndirectBits = 3;

std::vector<uint64_t> read_prefixed_longs(ByteBuffer& in) {
    const int32_t count = in.read_varint();
    if (count < 0) throw ProtocolError("long array with a negative length");
    std::vector<uint64_t> data(static_cast<size_t>(count));
    for (auto& value : data) value = static_cast<uint64_t>(in.read_i64());
    return data;
}

// A paletted container's data is not length-prefixed: the long count follows
// from the entry count and the bits per entry.
std::vector<uint64_t> read_packed_longs(ByteBuffer& in, size_t entry_count, uint8_t bits) {
    if (bits == 0) return {};
    const size_t per_long = 64 / bits;
    const size_t count = (entry_count + per_long - 1) / per_long;
    std::vector<uint64_t> data(count);
    for (auto& value : data) value = static_cast<uint64_t>(in.read_i64());
    return data;
}

} // namespace

PalettedContainer PalettedContainer::read(ByteBuffer& in, size_t entry_count,
                                          uint8_t max_indirect_bits) {
    PalettedContainer container;
    container.entry_count_ = entry_count;
    container.bits_ = in.read_u8();

    if (container.bits_ == 0) {
        container.palette_.push_back(in.read_varint());
    } else if (container.bits_ <= max_indirect_bits) {
        const int32_t size = in.read_varint();
        if (size < 0) throw ProtocolError("palette with a negative length");
        container.palette_.reserve(static_cast<size_t>(size));
        for (int32_t i = 0; i < size; ++i) container.palette_.push_back(in.read_varint());
    }

    container.data_ = read_packed_longs(in, entry_count, container.bits_);
    return container;
}

int32_t PalettedContainer::at(size_t index) const {
    if (index >= entry_count_) throw ProtocolError("index outside the paletted container");
    if (bits_ == 0) return palette_.empty() ? 0 : palette_.front();

    const size_t per_long = 64 / bits_;
    const uint64_t mask = (uint64_t{1} << bits_) - 1;
    const uint64_t packed = data_[index / per_long];
    const auto raw = static_cast<int32_t>((packed >> (index % per_long * bits_)) & mask);

    if (palette_.empty()) return raw;
    if (static_cast<size_t>(raw) >= palette_.size()) {
        throw ProtocolError("palette index outside the palette");
    }
    return palette_[static_cast<size_t>(raw)];
}

size_t Chunk::non_empty_sections() const {
    size_t count = 0;
    for (const auto& section : sections) {
        if (section.non_air_count > 0) ++count;
    }
    return count;
}

int32_t Chunk::block_state(int32_t local_x, int32_t y_index, int32_t local_z) const {
    const auto section = static_cast<size_t>(y_index / 16);
    if (section >= sections.size()) return 0;
    const size_t index = static_cast<size_t>(y_index % 16) * 256 +
                         static_cast<size_t>(local_z) * 16 + static_cast<size_t>(local_x);
    return sections[section].blocks.at(index);
}

Chunk read_chunk(ByteBuffer& in) {
    Chunk chunk;
    chunk.x = in.read_i32();
    chunk.z = in.read_i32();

    const int32_t heightmap_count = in.read_varint();
    if (heightmap_count < 0) throw ProtocolError("negative heightmap count");
    for (int32_t i = 0; i < heightmap_count; ++i) {
        in.read_varint();
        read_prefixed_longs(in);
    }

    const int32_t data_size = in.read_varint();
    if (data_size < 0) throw ProtocolError("negative chunk data length");
    ByteBuffer sections(in.read_bytes(static_cast<size_t>(data_size)));

    // The section count follows from the dimension height, which lives in a
    // registry we do not keep; reading until the blob runs out needs neither.
    while (sections.remaining() > 0) {
        ChunkSection section;
        section.non_air_count = static_cast<int16_t>(sections.read_u16());
        section.blocks = PalettedContainer::read(sections, kBlocksPerSection, kMaxBlockIndirectBits);
        section.biomes = PalettedContainer::read(sections, kBiomesPerSection, kMaxBiomeIndirectBits);
        chunk.sections.push_back(std::move(section));
    }

    const int32_t block_entity_count = in.read_varint();
    if (block_entity_count < 0) throw ProtocolError("negative block entity count");
    for (int32_t i = 0; i < block_entity_count; ++i) {
        const uint8_t packed_xz = in.read_u8();
        BlockEntity entity;
        entity.x = (chunk.x << 4) + (packed_xz >> 4);
        entity.z = (chunk.z << 4) + (packed_xz & 0x0F);
        entity.y = static_cast<int16_t>(in.read_u16());
        entity.type = in.read_varint();
        nbt::read_network_tag(in);
        chunk.block_entities.push_back(entity);
    }
    return chunk;
}

} // namespace mcpp::world
