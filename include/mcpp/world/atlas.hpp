#pragma once
#include <cstdint>
#include <filesystem>
#include <span>

namespace mcpp::world {

// Texture ids are the block table's own ids, and tiles are a uniform size, so a
// tile's place in the atlas is a multiply rather than a lookup.
class Atlas {
public:
    Atlas() = default;
    ~Atlas();

    Atlas(const Atlas&) = delete;
    Atlas& operator=(const Atlas&) = delete;

    bool open(const std::filesystem::path& path);
    void close();
    bool is_open() const { return base_ != nullptr; }

    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }
    uint32_t tile_size() const { return tile_size_; }
    uint32_t columns() const { return tile_size_ ? width_ / tile_size_ : 0; }
    uint32_t texture_count() const { return texture_count_; }
    int32_t protocol() const { return protocol_; }

    std::span<const uint8_t> pixels() const { return {pixels_, pixel_bytes_}; }

    static std::filesystem::path default_path(int32_t protocol);

private:
    const uint8_t* base_ = nullptr;
    size_t size_ = 0;
    int32_t protocol_ = 0;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint32_t tile_size_ = 0;
    uint32_t texture_count_ = 0;
    const uint8_t* pixels_ = nullptr;
    size_t pixel_bytes_ = 0;
};

} // namespace mcpp::world
