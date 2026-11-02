#pragma once
#include "mcpp/gpu/device.hpp"
#include "mcpp/world/atlas.hpp"
#include "mcpp/world/mesher.hpp"

#include <cstdint>
#include <vector>

namespace mcpp::gpu {

struct Camera {
    float eye[3];
    float target[3];
    float fov_degrees = 70.0f;
    float near_plane = 0.1f;
    float far_plane = 2000.0f;
};

// Draws a meshed world into an image we read back. The window path will reuse
// the same pipeline, buffers and recording.
class WorldRenderer {
public:
    WorldRenderer(Device& device, uint32_t width, uint32_t height,
                  const world::Atlas& atlas);
    ~WorldRenderer();

    WorldRenderer(const WorldRenderer&) = delete;
    WorldRenderer& operator=(const WorldRenderer&) = delete;

    void upload(const world::Mesh& mesh);
    std::vector<uint8_t> render(const Camera& camera);

    double last_gpu_ms() const { return last_gpu_ms_; }
    double upload_ms() const { return upload_ms_; }
    size_t uploaded_bytes() const { return vertex_bytes_ + index_bytes_; }
    uint32_t index_count() const { return index_count_; }

private:
    void create_targets();
    void create_atlas(const world::Atlas& atlas);
    void create_pipeline();
    void destroy_mesh();

    Device& device_;
    uint32_t width_;
    uint32_t height_;

    VkImage color_ = VK_NULL_HANDLE;
    VkDeviceMemory color_memory_ = VK_NULL_HANDLE;
    VkImageView color_view_ = VK_NULL_HANDLE;
    VkImage depth_ = VK_NULL_HANDLE;
    VkDeviceMemory depth_memory_ = VK_NULL_HANDLE;
    VkImageView depth_view_ = VK_NULL_HANDLE;
    VkBuffer readback_ = VK_NULL_HANDLE;
    VkDeviceMemory readback_memory_ = VK_NULL_HANDLE;

    VkBuffer vertex_buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory vertex_memory_ = VK_NULL_HANDLE;
    VkBuffer index_buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory index_memory_ = VK_NULL_HANDLE;

    VkImage atlas_image_ = VK_NULL_HANDLE;
    VkDeviceMemory atlas_memory_ = VK_NULL_HANDLE;
    VkImageView atlas_view_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set_ = VK_NULL_HANDLE;
    uint32_t atlas_tile_ = 0;
    uint32_t atlas_layers_ = 0;

    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkCommandPool pool_ = VK_NULL_HANDLE;
    VkQueryPool timestamps_ = VK_NULL_HANDLE;

    uint32_t index_count_ = 0;
    size_t vertex_bytes_ = 0;
    size_t index_bytes_ = 0;
    double last_gpu_ms_ = 0.0;
    double upload_ms_ = 0.0;
    float timestamp_period_ = 1.0f;
};

} // namespace mcpp::gpu
