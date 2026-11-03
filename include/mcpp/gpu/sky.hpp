#pragma once
#include "mcpp/gpu/device.hpp"
#include "mcpp/gpu/world_renderer.hpp"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace mcpp::gpu {

// The sky, drawn as one triangle. A flat cloud deck and a sun disc are both
// just the view ray crossing a plane, so neither needs geometry and both stay
// correct at any view distance.
class Sky {
public:
    Sky(Device& device, VkFormat colour_format, const std::filesystem::path& textures);
    ~Sky();

    Sky(const Sky&) = delete;
    Sky& operator=(const Sky&) = delete;

    // Blocks the deck has drifted; the game moves it slowly and constantly.
    void set_drift(float blocks) { drift_ = blocks; }
    void record(VkCommandBuffer command, const Camera& camera, uint32_t width, uint32_t height,
                const float zenith[3], const float horizon[3]) const;

    static std::filesystem::path default_path(int32_t protocol);

private:
    void create_texture(const std::vector<uint8_t>& pixels, uint32_t side, VkFormat format,
                        VkImage& image, VkDeviceMemory& memory, VkImageView& view);
    void create_pipeline(VkFormat colour_format);
    void destroy();

    Device& device_;
    float drift_ = 0.0f;
    VkCommandPool pool_ = VK_NULL_HANDLE;
    VkImage cloud_image_ = VK_NULL_HANDLE;
    VkDeviceMemory cloud_memory_ = VK_NULL_HANDLE;
    VkImageView cloud_view_ = VK_NULL_HANDLE;
    VkImage sun_image_ = VK_NULL_HANDLE;
    VkDeviceMemory sun_memory_ = VK_NULL_HANDLE;
    VkImageView sun_view_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
};

} // namespace mcpp::gpu
