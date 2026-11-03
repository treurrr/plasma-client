#pragma once
#include "mcpp/gpu/device.hpp"

#include <cstdint>

namespace mcpp::gpu {

// The thin dark box around the block under the crosshair. Twelve edges, no
// vertex buffer: the corners come out of the vertex index.
class Outline {
public:
    Outline(Device& device, VkFormat colour_format);
    ~Outline();

    Outline(const Outline&) = delete;
    Outline& operator=(const Outline&) = delete;

    void record(VkCommandBuffer command, const float view_projection[16],
                const int32_t block[3]) const;

private:
    void destroy();

    Device& device_;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    float width_ = 1.0f;
};

} // namespace mcpp::gpu
