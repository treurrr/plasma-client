#pragma once
#include "mcpp/gpu/device.hpp"

#include <cstdint>
#include <vector>

namespace mcpp::gpu {

// Renders into an image we read back, so a frame can be inspected instead of
// only counted. The window path reuses the same pipeline and command recording.
class Offscreen {
public:
    Offscreen(Device& device, uint32_t width, uint32_t height);
    ~Offscreen();

    Offscreen(const Offscreen&) = delete;
    Offscreen& operator=(const Offscreen&) = delete;

    std::vector<uint8_t> render();

    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }
    double last_gpu_ms() const { return last_gpu_ms_; }
    double pipeline_build_ms() const { return pipeline_build_ms_; }

private:
    void create_image();
    void create_readback_buffer();
    void create_pipeline();
    void record(VkCommandBuffer command);

    Device& device_;
    uint32_t width_;
    uint32_t height_;

    VkImage image_ = VK_NULL_HANDLE;
    VkDeviceMemory image_memory_ = VK_NULL_HANDLE;
    VkImageView view_ = VK_NULL_HANDLE;
    VkBuffer readback_ = VK_NULL_HANDLE;
    VkDeviceMemory readback_memory_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkCommandPool pool_ = VK_NULL_HANDLE;
    VkQueryPool timestamps_ = VK_NULL_HANDLE;

    double last_gpu_ms_ = 0.0;
    double pipeline_build_ms_ = 0.0;
    float timestamp_period_ = 1.0f;
};

} // namespace mcpp::gpu
