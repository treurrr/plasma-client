#pragma once
#include "mcpp/gpu/device.hpp"

#include <cstdint>

namespace mcpp::gpu {

// The menu blur: the frame so far, box-blurred in place across and then down,
// three times over, the way the game's blur post chain does it.
class BoxBlur {
public:
    BoxBlur(Device& device, VkFormat format);
    ~BoxBlur();
    BoxBlur(const BoxBlur&) = delete;
    BoxBlur& operator=(const BoxBlur&) = delete;

    // `image` is in COLOR_ATTACHMENT_OPTIMAL before and after, and must be
    // sampleable.
    void record(VkCommandBuffer command, VkImage image, VkImageView view, uint32_t width,
                uint32_t height, int radius);

private:
    void ensure_scratch(uint32_t width, uint32_t height);
    void point_at(VkDescriptorSet set, VkImageView view);
    void pass(VkCommandBuffer command, VkImageView target, VkDescriptorSet source, uint32_t width,
              uint32_t height, float dx, float dy, int radius);
    void destroy_scratch();

    Device& device_;
    VkFormat format_;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkDescriptorSet from_target_ = VK_NULL_HANDLE;
    VkDescriptorSet from_scratch_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkImage scratch_ = VK_NULL_HANDLE;
    VkDeviceMemory scratch_memory_ = VK_NULL_HANDLE;
    VkImageView scratch_view_ = VK_NULL_HANDLE;
    uint32_t scratch_width_ = 0;
    uint32_t scratch_height_ = 0;
    VkImageView target_view_ = VK_NULL_HANDLE;
};

} // namespace mcpp::gpu
