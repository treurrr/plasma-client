#pragma once
#include "mcpp/gpu/device.hpp"
#include "mcpp/world/atmosphere.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace mcpp::gpu {

struct Camera;

// The sky the way the game builds it, as geometry around the eye: a disc
// overhead and a dark one below, a fan of glow at sunrise and sunset, the sun
// and the moon, fifteen hundred stars, or in the end a box of its own texture.
// The clouds are boxes on a grid read from the cloud sheet, drawn after the
// terrain so a hill in front of a cloud still hides it.
class Sky {
public:
    Sky(Device& device, VkFormat colour_format, VkFormat depth_format,
        const std::filesystem::path& textures);
    ~Sky();

    Sky(const Sky&) = delete;
    Sky& operator=(const Sky&) = delete;

    enum class Clouds : uint8_t { Off, Fast, Fancy };
    // Where the clouds are this frame: the game's tick count, its partial
    // tick, and the options that shape them.
    void set_clouds(Clouds mode, int32_t range_chunks) {
        cloud_mode_ = mode;
        cloud_range_ = range_chunks;
    }
    void set_cloud_time(int64_t game_time, float partial) {
        cloud_ticks_ = game_time;
        cloud_partial_ = partial;
    }

    // Before the terrain, inside the frame's rendering.
    void record(VkCommandBuffer command, const Camera& camera, uint32_t width, uint32_t height,
                const world::Atmosphere& atmosphere) const;
    // After the terrain and the water, with the depth they left.
    void record_clouds(VkCommandBuffer command, const Camera& camera, uint32_t width,
                       uint32_t height, const world::Atmosphere& atmosphere);

    static std::filesystem::path default_path(int32_t protocol);
    // The star field as the game seeds it: four corners per star.
    static std::vector<std::array<float, 3>> star_corners();

private:
    struct Range {
        uint32_t first = 0;
        uint32_t count = 0;
    };
    void build_geometry();
    void create_pipelines(VkFormat colour_format, VkFormat depth_format);
    void destroy();

    Device& device_;
    std::vector<uint8_t> cloud_cells_;   // 256x256, bit 4.. filled, low 4 bits open sides
    Clouds cloud_mode_ = Clouds::Fancy;
    int32_t cloud_range_ = 128;
    int64_t cloud_ticks_ = 0;
    float cloud_partial_ = 0.0f;
    // The cloud mesh is rebuilt only when the camera enters another cell or
    // crosses the layer, the way the game does it.
    int32_t built_cell_[2] = {INT32_MIN, INT32_MIN};
    int32_t built_side_ = -1;
    Clouds built_mode_ = Clouds::Off;
    uint32_t cloud_faces_ = 0;

    struct Resources;
    Resources* gpu_ = nullptr;
    Range disc_;
    Range dark_disc_;
    Range fan_;
    Range sun_;
    Range moons_;
    Range stars_;
    Range end_box_;
    Range end_flash_;
};

} // namespace mcpp::gpu
