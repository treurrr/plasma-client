#pragma once
#include "mcpp/gpu/device.hpp"

#include <cstdint>
#include <span>
#include <unordered_map>

namespace mcpp::gpu {

// The thin dark lines around the block under the crosshair: every edge of its
// outline shape, each drawn as a screen-facing band the game's width. A shape's
// edges are stored once, the first time it is aimed at, and never move after.
class Outline {
public:
    struct Range {
        uint32_t first = 0;
        uint32_t count = 0;
    };

    Outline(Device& device, VkFormat colour_format);
    ~Outline();

    Outline(const Outline&) = delete;
    Outline& operator=(const Outline&) = delete;

    // The stored edges of the shape with this key, if any.
    const Range* find(uint64_t key) const;
    // Stores a shape's edges, six floats each in block coordinates.
    Range add(uint64_t key, std::span<const float> edges);
    // What the next frames draw: the edges, where the block's corner is from
    // the eye (in doubles, then rounded), the eye itself, and the window.
    void aim(Range range, const double corner_from_eye[3], const float eye[3], uint32_t width,
             uint32_t height);

    void record(VkCommandBuffer command, const float view_projection[16],
                const int32_t block[3]) const;

private:
    void destroy();

    Device& device_;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    float* mapped_ = nullptr;
    uint32_t used_ = 0;
    std::unordered_map<uint64_t, Range> ranges_;
    Range range_;
    float corner_[3] = {0.0f, 0.0f, 0.0f};
    float eye_[3] = {0.0f, 0.0f, 0.0f};
    float screen_[2] = {1.0f, 1.0f};
};

} // namespace mcpp::gpu
