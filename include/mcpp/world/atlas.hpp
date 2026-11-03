#pragma once
#include <cstdint>
#include <filesystem>
#include <span>
#include <utility>
#include <vector>

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

    // A texture the game animates: its frames as (layer, ticks shown), in
    // the order its .mcmeta plays them, and whether it fades between them.
    struct Animation {
        uint32_t texture = 0;
        bool interpolate = false;
        std::vector<std::pair<uint32_t, uint32_t>> frames;
        uint32_t length = 0;
    };
    // Layers below this are textures by id; the rest are further frames.
    uint32_t textures() const { return textures_; }
    const std::vector<Animation>& animations() const { return animations_; }
    // The game's ticker: which layer shows after this many ticks, the next one,
    // and how far towards it an interpolating texture has faded.
    struct Frame {
        uint32_t layer = 0;
        uint32_t next = 0;
        float fade = 0.0f;
    };
    static Frame frame_at(const Animation& animation, uint64_t ticks);

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
    uint32_t textures_ = 0;
    std::vector<Animation> animations_;
};

} // namespace mcpp::world
