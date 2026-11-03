#pragma once
#include "mcpp/entity/assets.hpp"
#include "mcpp/entity/model.hpp"
#include "mcpp/gpu/device.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mcpp::gpu {

// Everything in the world that is not a block: entity models, their name tags
// and their shadows. Drawn inside the world's pass, against its depth, from
// vertices the cpu poses each frame.
class EntityRenderer {
public:
    EntityRenderer(Device& device, VkFormat colour_format,
                   VkFormat depth_format = VK_FORMAT_D32_SFLOAT);
    ~EntityRenderer();

    EntityRenderer(const EntityRenderer&) = delete;
    EntityRenderer& operator=(const EntityRenderer&) = delete;

    // Textures by name: uploaded the first time, kept until forgotten.
    bool has_texture(std::string_view name) const;
    void upload_texture(const std::string& name, const entity::Texture& texture);
    // Let go a few frames later, once no frame in flight can still read it.
    void forget_texture(std::string_view name);
    void set_font(const entity::Font& font);

    enum class Layer : uint8_t { Cutout, CutoutNoCull, Translucent, Shadow, TagThrough, Tag };

    struct Draw {
        std::string texture;
        Layer layer = Layer::Cutout;
        float sky_light = 15.0f;
        float block_light = 0.0f;
        // Hurt red, creeper white: mixed over the texture by the last number.
        std::array<float, 4> overlay{0.0f, 0.0f, 0.0f, 0.0f};
        std::array<float, 4> colour{1.0f, 1.0f, 1.0f, 1.0f};
        bool lit = true;
    };

    // A new frame: what was queued for the last one is dropped.
    void begin();
    void add(std::span<const entity::Vertex> vertices, const Draw& draw);
    // A line of name-tag text centred on the origin of `place`, which turns it
    // to face the camera; one unit is a pixel of the font.
    void add_tag(std::string_view text, const entity::Matrix& place, bool through_walls,
                 float sky_light, float block_light);
    size_t queued_vertices() const { return queued_; }
    size_t queued_draws() const { return draws_.size(); }

    // Positions are relative to the camera, which keeps them precise far
    // from the world's origin.
    void record(VkCommandBuffer command, const float view_projection[16], uint32_t width,
                uint32_t height, const float fog[3], float fog_start, float fog_end) const;

private:
    struct Slot {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkDescriptorSet set = VK_NULL_HANDLE;
    };
    struct Queued {
        uint32_t first = 0;
        uint32_t count = 0;
        Draw draw;
    };
    void create_pipelines(VkFormat colour_format, VkFormat depth_format);
    void destroy_slot(Slot& slot);
    void destroy();

    Device& device_;
    const entity::Font* font_ = nullptr;
    entity::Font font_copy_;
    VkCommandPool pool_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    std::array<VkPipeline, 6> pipelines_{};

    std::map<std::string, Slot, std::less<>> slots_;
    struct Retired {
        Slot slot;
        uint64_t after = 0;
    };
    std::vector<Retired> retired_;
    uint64_t frame_ = 0;

    // A ring of vertex buffers, one per frame that can be in flight.
    static constexpr uint32_t kFrames = 3;
    static constexpr size_t kVerticesPerFrame = 1u << 17;
    VkBuffer vertices_ = VK_NULL_HANDLE;
    VkDeviceMemory vertex_memory_ = VK_NULL_HANDLE;
    entity::Vertex* mapped_ = nullptr;
    size_t queued_ = 0;
    std::vector<Queued> draws_;
};

} // namespace mcpp::gpu
