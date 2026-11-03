#pragma once
#include "mcpp/gpu/device.hpp"

#include <cstdint>

namespace mcpp::gpu {

// One rounded rectangle. Everything the interface is built from is one of
// these: cards, pills, bars, dividers, the glow behind the accent.
struct Quad {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float radius = 0.0f;
    float border = 0.0f;
    float glow = 0.0f;
    uint32_t fill_top = 0;
    uint32_t fill_bottom = 0;
    uint32_t border_rgba = 0;
    uint32_t glow_rgba = 0;
};

// Shapes are signed distance fields in the fragment shader rather than
// geometry, so a corner radius costs nothing and every edge is antialiased at
// whatever resolution the window happens to be.
class UiRenderer {
public:
    UiRenderer(Device& device, VkFormat colour_format);
    ~UiRenderer();

    UiRenderer(const UiRenderer&) = delete;
    UiRenderer& operator=(const UiRenderer&) = delete;

    void begin();
    void add(const Quad& quad);
    void panel(float x, float y, float width, float height, float radius, uint32_t fill,
               uint32_t border_rgba = 0, float border = 0.0f);
    void record(VkCommandBuffer command, uint32_t width, uint32_t height);

private:
    void create_pipeline(VkFormat colour_format);
    void destroy();

    Device& device_;
    bool linearise_ = false;
    uint32_t count_ = 0;
    static constexpr uint32_t kCapacity = 4096;
    Quad* mapped_ = nullptr;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
};

} // namespace mcpp::gpu
