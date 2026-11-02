#pragma once
#include "mcpp/gpu/device.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>

namespace mcpp::gpu {

class TextRenderer {
public:
    TextRenderer(Device& device, const std::filesystem::path& font, VkFormat colour_format);
    ~TextRenderer();

    TextRenderer(const TextRenderer&) = delete;
    TextRenderer& operator=(const TextRenderer&) = delete;

    // The caller must finish the previous GPU use before reusing the mapped buffer.
    void begin();
    // x and y locate the line top. Glyphs beyond the fixed capacity are dropped.
    void draw(std::string_view text, float x, float y, float scale, uint32_t rgba);
    // Panels and buttons go through the same pipeline, using a solid patch
    // baked into the corner of the atlas.
    void fill(float x, float y, float w, float h, uint32_t rgba);
    float width(std::string_view text, float scale) const;
    float line_height(float scale) const;
    void record(VkCommandBuffer command, uint32_t width, uint32_t height);

    static std::filesystem::path default_font_path();

private:
    void create_atlas(std::span<const uint8_t> pixels);
    void create_pipeline(VkFormat colour_format);
    void destroy();

    struct Glyph {
        int16_t x, y, w, h, advance, bearing_x, bearing_y;
    };
    struct Vertex {
        float position[2];
        float uv[2];
        uint32_t colour;
    };

    Device& device_;
    std::array<Glyph, 95> glyphs_{};
    uint32_t line_height_ = 0;
    uint32_t atlas_width_ = 0;
    uint32_t atlas_height_ = 0;
    uint32_t vertex_count_ = 0;
    static constexpr uint32_t kVertexCapacity = 4096 * 6;
    Vertex* mapped_ = nullptr;
    VkBuffer vertex_buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory vertex_memory_ = VK_NULL_HANDLE;
    VkImage atlas_image_ = VK_NULL_HANDLE;
    VkDeviceMemory atlas_memory_ = VK_NULL_HANDLE;
    VkImageView atlas_view_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkCommandPool pool_ = VK_NULL_HANDLE;
};

} // namespace mcpp::gpu
