#include <algorithm>
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
// Entries never straddle a long, so the count follows from how many fit in
// one, not from the total number of bits.
size_t longs_needed(size_t entry_count, uint8_t bits) {
    if (bits == 0) return 0;
    const size_t per_long = 64 / bits;
    return (entry_count + per_long - 1) / per_long;
}

std::vector<uint64_t> read_packed_longs(ByteBuffer& in, size_t entry_count, uint8_t bits) {
    if (bits == 0) return {};
    const size_t count = longs_needed(entry_count, bits);
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

void PalettedContainer::set(size_t index, int32_t value, uint8_t max_indirect_bits) {
    if (index >= entry_count_) return;
    if (at(index) == value) return;

    // A section of one block becomes a section of two, which is the smallest
    // palette the format allows.
    if (bits_ == 0) {
        const int32_t only = palette_.empty() ? 0 : palette_.front();
        bits_ = 4;
        palette_ = {only, value};
        data_.assign(longs_needed(entry_count_, bits_), 0);
        write_raw(index, 1);
        return;
    }

    if (palette_.empty()) {   // direct storage: the value is its own index
        write_raw(index, static_cast<uint32_t>(value));
        return;
    }

    auto found = std::find(palette_.begin(), palette_.end(), value);
    if (found == palette_.end()) {
        const size_t wanted = palette_.size() + 1;
        if (wanted > (size_t{1} << bits_)) {
            uint8_t grown = bits_;
            while (wanted > (size_t{1} << grown) && grown < max_indirect_bits) ++grown;
            if (wanted > (size_t{1} << grown)) {
                // Out of room for a palette at all; every entry becomes the
                // block state itself, which is what the server would send.
                std::vector<int32_t> states(entry_count_);
                for (size_t i = 0; i < entry_count_; ++i) states[i] = at(i);
                states[index] = value;
                palette_.clear();
                bits_ = kDirectBits;
                data_.assign(longs_needed(entry_count_, bits_), 0);
                for (size_t i = 0; i < entry_count_; ++i) {
                    write_raw(i, static_cast<uint32_t>(states[i]));
                }
                return;
            }
            rewrite(grown);
        }
        palette_.push_back(value);
        found = palette_.end() - 1;
    }
    write_raw(index, static_cast<uint32_t>(found - palette_.begin()));
}

void PalettedContainer::write_raw(size_t index, uint32_t raw) {
    const size_t per_long = 64 / bits_;
    const uint64_t mask = (uint64_t{1} << bits_) - 1;
    const size_t shift = index % per_long * bits_;
    uint64_t& packed = data_[index / per_long];
    packed = (packed & ~(mask << shift)) | ((static_cast<uint64_t>(raw) & mask) << shift);
}

void PalettedContainer::rewrite(uint8_t bits) {
    std::vector<uint32_t> raw(entry_count_);
    const size_t per_long = 64 / bits_;
    const uint64_t mask = (uint64_t{1} << bits_) - 1;
    for (size_t i = 0; i < entry_count_; ++i) {
        raw[i] = static_cast<uint32_t>((data_[i / per_long] >> (i % per_long * bits_)) & mask);
    }
    bits_ = bits;
    data_.assign(longs_needed(entry_count_, bits_), 0);
    for (size_t i = 0; i < entry_count_; ++i) write_raw(i, raw[i]);
}

size_t Chunk::non_empty_sections() const {
    size_t count = 0;
    for (const auto& section : sections) {
        if (section.non_air_count > 0) ++count;
    }
    return count;
}

bool Chunk::set_block_state(int32_t local_x, int32_t y_index, int32_t local_z, int32_t state) {
    const auto section = static_cast<size_t>(y_index / 16);
    if (y_index < 0 || section >= sections.size()) return false;
    const size_t index = static_cast<size_t>(y_index % 16) * 256 +
                         static_cast<size_t>(local_z) * 16 + static_cast<size_t>(local_x);
    const int32_t was = sections[section].blocks.at(index);
    if (was == state) return true;
    sections[section].blocks.set(index, state, 8);
    // The count decides whether the section is drawn at all, so it has to
    // follow, or breaking the last block in a section leaves it on screen.
    if (was == 0 && state != 0) ++sections[section].non_air_count;
    if (was != 0 && state == 0) --sections[section].non_air_count;
    return true;
}

int32_t Chunk::block_state(int32_t local_x, int32_t y_index, int32_t local_z) const {
    const auto section = static_cast<size_t>(y_index / 16);
    if (section >= sections.size()) return 0;
    const size_t index = static_cast<size_t>(y_index % 16) * 256 +
                         static_cast<size_t>(local_z) * 16 + static_cast<size_t>(local_x);
    return sections[section].blocks.at(index);
}

int32_t Chunk::biome_at(int32_t local_x, int32_t y_index, int32_t local_z) const {
    if (local_x < 0 || local_x > 15 || local_z < 0 || local_z > 15 || y_index < 0) return -1;
    const auto section = static_cast<size_t>(y_index) / 16;
    if (section >= sections.size()) return -1;
    const size_t index = static_cast<size_t>((y_index % 16) / 4) * 16 +
                         static_cast<size_t>(local_z / 4) * 4 + static_cast<size_t>(local_x / 4);
    return sections[section].biomes.at(index);
}

uint8_t LightLayer::at(size_t slot, int32_t local_x, int32_t y, int32_t local_z) const {
    if (slot >= sections.size() || sections[slot].empty()) return 0;
    const size_t index = static_cast<size_t>(y) * 256 + static_cast<size_t>(local_z) * 16 +
                         static_cast<size_t>(local_x);
    const uint8_t packed = sections[slot][index >> 1];
    return (index & 1) != 0 ? static_cast<uint8_t>(packed >> 4) : static_cast<uint8_t>(packed & 0x0F);
}

uint8_t Chunk::sky_light_at(int32_t local_x, int32_t y_index, int32_t local_z) const {
    // A chunk we built ourselves, or one whose light has not arrived, is lit
    // rather than pitch black.
    if (sky_light.sections.empty()) return 15;
    if (local_x < 0 || local_x > 15 || local_z < 0 || local_z > 15) return 15;
    if (y_index < 0 || y_index >= static_cast<int32_t>(sections.size()) * 16) return 15;
    return sky_light.at(static_cast<size_t>(y_index / 16) + 1, local_x, y_index % 16, local_z);
}

uint8_t Chunk::block_light_at(int32_t local_x, int32_t y_index, int32_t local_z) const {
    if (local_x < 0 || local_x > 15 || local_z < 0 || local_z > 15) return 0;
    if (y_index < 0 || y_index >= static_cast<int32_t>(sections.size()) * 16) return 0;
    return block_light.at(static_cast<size_t>(y_index / 16) + 1, local_x, y_index % 16, local_z);
}

namespace {

std::vector<uint64_t> read_bitset(ByteBuffer& in) {
    const int32_t count = in.read_varint();
    if (count < 0 || count > 64) throw ProtocolError("implausible light bitset");
    std::vector<uint64_t> words;
    words.reserve(static_cast<size_t>(count));
    for (int32_t i = 0; i < count; ++i) words.push_back(static_cast<uint64_t>(in.read_i64()));
    return words;
}

bool bit(const std::vector<uint64_t>& words, size_t index) {
    const size_t word = index >> 6;
    return word < words.size() && ((words[word] >> (index & 63)) & 1) != 0;
}

// Arrays arrive in mask order, one per set bit, so they are matched to slots by
// walking the mask rather than by any index in the packet.
LightLayer read_light_layer(ByteBuffer& in, const std::vector<uint64_t>& present, size_t slots) {
    LightLayer layer;
    layer.sections.resize(slots);
    const int32_t count = in.read_varint();
    if (count < 0) throw ProtocolError("negative light array count");
    int32_t taken = 0;
    for (size_t slot = 0; slot < slots && taken < count; ++slot) {
        if (!bit(present, slot)) continue;
        const int32_t length = in.read_varint();
        if (length != 2048) throw ProtocolError("light array is not 2048 bytes");
        layer.sections[slot] = in.read_bytes(2048);
        ++taken;
    }
    for (; taken < count; ++taken) {
        const int32_t length = in.read_varint();
        if (length < 0) throw ProtocolError("negative light array length");
        in.read_bytes(static_cast<size_t>(length));
    }
    return layer;
}

} // namespace

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

    read_light_into(in, chunk);
    return chunk;
}

void read_light_into(ByteBuffer& in, Chunk& chunk) {
    const auto sky_mask = read_bitset(in);
    const auto block_mask = read_bitset(in);
    read_bitset(in);   // empty sky sections, which are zero anyway
    read_bitset(in);   // empty block sections
    const size_t slots = chunk.sections.size() + 2;
    chunk.sky_light = read_light_layer(in, sky_mask, slots);
    chunk.block_light = read_light_layer(in, block_mask, slots);
}

} // namespace mcpp::world
