#pragma once
// Small Vulkan helpers shared by the sky, the clouds and the world's frame
// data: buffers, sampled images filled once, and a graphics pipeline described
// by the few fields that differ between them.
#include "mcpp/gpu/device.hpp"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace mcpp::gpu::vk {

void check(VkResult result, std::string_view what);
uint32_t memory_type(VkPhysicalDevice physical, uint32_t mask, VkMemoryPropertyFlags wanted);

struct Buffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mapped = nullptr;
    VkDeviceSize size = 0;
    void destroy(VkDevice device);
};
// Host-visible and mapped for as long as it lives.
Buffer mapped_buffer(const Device& device, VkDeviceSize size, VkBufferUsageFlags usage);

struct Image {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    void destroy(VkDevice device);
};
// An array image filled once from tightly packed layers, left shader-readable.
Image sampled_array(const Device& device, VkFormat format, uint32_t width, uint32_t height,
                    uint32_t layers, std::span<const uint8_t> pixels, uint32_t bytes_per_texel);
// An image the frame rewrites from a buffer before it draws.
Image updatable_image(const Device& device, VkFormat format, uint32_t width, uint32_t height);
// Records a copy of a whole 2D image from a buffer, with the barriers around it.
void record_image_update(VkCommandBuffer command, VkImage image, VkBuffer source,
                         VkDeviceSize offset, uint32_t width, uint32_t height, bool first);

VkSampler sampler(VkDevice device, VkFilter filter, VkSamplerAddressMode address);

enum class Blend : uint8_t { None, Translucent, Overlay };

struct PipelineDesc {
    std::span<const uint32_t> vertex;
    std::span<const uint32_t> fragment;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    std::vector<VkVertexInputAttributeDescription> attributes;
    uint32_t stride = 0;
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkCullModeFlags cull = VK_CULL_MODE_NONE;
    VkFrontFace front = VK_FRONT_FACE_CLOCKWISE;
    bool depth_test = true;
    bool depth_write = false;
    VkCompareOp depth_compare = VK_COMPARE_OP_GREATER_OR_EQUAL;
    Blend blend = Blend::None;
    VkFormat colour_format = VK_FORMAT_R8G8B8A8_UNORM;
    VkFormat depth_format = VK_FORMAT_D32_SFLOAT;
};
VkPipeline pipeline(const Device& device, const PipelineDesc& desc);

} // namespace mcpp::gpu::vk
