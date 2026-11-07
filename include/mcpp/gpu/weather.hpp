#pragma once
// Rain and snow on the gpu. What falls where comes from the game's own code
// (mcpp/world/weather.hpp); this is only how it is drawn: every column of a
// frame in one buffer, inside the world's pass so it takes the world's fog
// and lightmap, with rain and snow as two layers of one texture.
#include "mcpp/gpu/device.hpp"
#include "mcpp/gpu/world_renderer.hpp"
#include "mcpp/world/weather.hpp"

#include <filesystem>
#include <span>

namespace mcpp::gpu {

class WeatherRenderer {
public:
    // The two textures come out of the baked sky file, where bake_sky.py puts
    // rain.png and snow.png after the end sky. Without it nothing is drawn.
    WeatherRenderer(Device& device, const WorldRenderer& world,
                    const std::filesystem::path& sky_textures);
    ~WeatherRenderer();
    WeatherRenderer(const WeatherRenderer&) = delete;
    WeatherRenderer& operator=(const WeatherRenderer&) = delete;

    // This frame's columns, turned into corners about the camera.
    void set_frame(const world::WeatherState& state, const double camera[3]);
    // After the terrain and the water, with the depth they left.
    void record(VkCommandBuffer command, const float view_projection[16], const Camera& camera) const;

    bool ready() const { return ready_; }
    size_t rain_vertices() const { return rain_count_; }
    size_t snow_vertices() const { return snow_count_; }

    static constexpr uint32_t kMaxVertices = 2 * 31 * 31 * 6;

private:
    void draw(VkCommandBuffer command, const float view_projection[16], const Camera& camera,
              size_t first, size_t count, float layer) const;
    void destroy();

    Device& device_;
    const WorldRenderer& world_;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkDescriptorSet set_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    struct Image {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
    } texture_;
    struct Buffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void* mapped = nullptr;
    } vertices_;
    bool ready_ = false;
    uint32_t slot_ = 0;
    size_t rain_count_ = 0;
    size_t snow_count_ = 0;
    double origin_[3] = {0.0, 0.0, 0.0};
    std::vector<world::WeatherVertex> scratch_;
};

} // namespace mcpp::gpu
