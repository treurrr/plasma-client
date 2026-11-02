#pragma once
#include <cstdint>
#include <filesystem>
#include <span>

namespace mcpp::world {

// Texture ids are the block table's own ids and double as array layers, so a
// lookup needs no arithmetic. A layer per texture also keeps minification from
// filtering across a neighbouring texture's edge, which a grid atlas cannot.
class Atlas {
public:
    Atlas() = default;
    ~Atlas();

    Atlas(const Atlas&) = delete;
    Atlas& operator=(const Atlas&) = delete;

    bool open(const std::filesystem::path& path);
    void close();
    bool is_open() const { return base_ != nullptr; }

    uint32_t tile_size() const { return tile_size_; }
    uint32_t layers() const { return layers_; }
    uint32_t mip_levels() const { return mip_levels_; }
    int32_t protocol() const { return protocol_; }

    std::span<const uint8_t> pixels() const { return {pixels_, pixel_bytes_}; }

    // Byte offset of a mip level's first layer; layers follow one another.
    size_t level_offset(uint32_t mip) const;
    size_t level_bytes(uint32_t mip) const;

    static std::filesystem::path default_path(int32_t protocol);

private:
    const uint8_t* base_ = nullptr;
    size_t size_ = 0;
    int32_t protocol_ = 0;
    uint32_t tile_size_ = 0;
    uint32_t layers_ = 0;
    uint32_t mip_levels_ = 1;
    const uint8_t* pixels_ = nullptr;
    size_t pixel_bytes_ = 0;
};

} // namespace mcpp::world
