#pragma once
#include "mcpp/gpu/device.hpp"
#include "mcpp/gpu/world_renderer.hpp"
#include "mcpp/world/atmosphere.hpp"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace mcpp::gpu {

// The sky as the game builds it, as geometry around the eye: a disc overhead
// and a dark one below, a fan of glow at sunrise and sunset, the sun and the
// moon, fifteen hundred stars, or in the end a box of its own texture. Fancy
// clouds are a second pass after the terrain, so a hill in front of a cloud
// still hides it.
class Sky {
public:
    Sky(Device& device, VkFormat colour_format, const std::filesystem::path& textures);
    ~Sky();

    Sky(const Sky&) = delete;
    Sky& operator=(const Sky&) = delete;

    // Blocks the cloud sheet has drifted west; the game moves it slowly and constantly.
    void set_drift(float blocks) { drift_ = blocks; }
    void set_clouds(bool on) { clouds_on_ = on; }
    void record(VkCommandBuffer command, const Camera& camera, uint32_t width, uint32_t height,
                const world::Atmosphere& atmosphere) const;
    void record_clouds(VkCommandBuffer command, const Camera& camera, uint32_t width,
                       uint32_t height, const world::Atmosphere& atmosphere,
                       const float view_projection[16]) const;

    static std::filesystem::path default_path(int32_t protocol);
    // Vertices of the star field, built from the game's own seed: six per star.
    static std::vector<float> star_positions();

private:
    struct Range {
        uint32_t first = 0;
        uint32_t count = 0;
    };
    void create_textures(const std::vector<uint8_t>& clouds, const std::vector<uint8_t>& celestial);
    void create_geometry();
    void create_pipelines(VkFormat colour_format);
    void upload(VkImage image, const std::vector<uint8_t>& pixels, uint32_t side, uint32_t layers);
    void destroy();

    Device& device_;
    float drift_ = 0.0f;
    bool clouds_on_ = true;
    VkCommandPool pool_ = VK_NULL_HANDLE;
    VkImage cloud_image_ = VK_NULL_HANDLE;
    VkDeviceMemory cloud_memory_ = VK_NULL_HANDLE;
    VkImageView cloud_view_ = VK_NULL_HANDLE;
    VkImage celestial_image_ = VK_NULL_HANDLE;
    VkDeviceMemory celestial_memory_ = VK_NULL_HANDLE;
    VkImageView celestial_view_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkBuffer vertices_ = VK_NULL_HANDLE;
    VkDeviceMemory vertex_memory_ = VK_NULL_HANDLE;
    VkBuffer cloud_uniform_ = VK_NULL_HANDLE;
    VkDeviceMemory cloud_uniform_memory_ = VK_NULL_HANDLE;
    void* cloud_uniform_mapped_ = nullptr;
    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline blended_ = VK_NULL_HANDLE;
    VkPipeline additive_ = VK_NULL_HANDLE;
    VkPipeline clouds_ = VK_NULL_HANDLE;

    Range disc_;
    Range dark_disc_;
    Range fan_;
    Range quad_;
    Range end_box_;
    Range stars_;
};

} // namespace mcpp::gpu
