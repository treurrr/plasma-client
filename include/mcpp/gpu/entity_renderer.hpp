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
// and their shadows, drawn inside the world's pass against its depth.
//
// Every model is baked once into one vertex buffer in part space; a frame
// hands over each entity's part matrices and the gpu poses the vertices, one
// instanced draw per model and render type. Every entity texture lives in one
// atlas, every player skin in one layer of an array, so nothing is bound per
// entity.
class EntityRenderer {
public:
    EntityRenderer(Device& device, VkFormat colour_format,
                   VkFormat depth_format = VK_FORMAT_D32_SFLOAT);
    ~EntityRenderer();

    EntityRenderer(const EntityRenderer&) = delete;
    EntityRenderer& operator=(const EntityRenderer&) = delete;

    // The models, textures and font, once.
    void load(const entity::Assets& assets);

    // -1 when there is no such model or texture.
    int32_t model(std::string_view name) const;
    const entity::Model* model_data(int32_t model) const;
    int32_t texture(std::string_view name) const;

    // Skins live in layers of their own; a texture number for a skin is the
    // layer with kSkinBit set.
    static constexpr uint32_t kSkinBit = 0x80000000u;
    static constexpr uint32_t kSkinLayers = 256;
    // A free layer filled with a 64x64 skin, or -1 when all are taken.
    int32_t upload_skin(const entity::Texture& skin);
    void release_skin(int32_t layer);

    // The game's render types for entity models.
    enum class Type : uint8_t { Cutout, CutoutNoCull, Translucent };

    struct Instance {
        int32_t model = -1;
        Type type = Type::CutoutNoCull;
        uint32_t texture = 0;
        uint8_t sky_light = 15;
        uint8_t block_light = 0;
        // The overlay texture's column (the white flash) and row (hurt red).
        uint8_t white = 0;
        bool red = false;
        uint32_t colour = 0xFFFFFFFF;   // ARGB, multiplied in
    };

    // A new frame: what was queued for the last one is dropped.
    void begin();
    // One entity: its part matrices, camera-relative, in the model's part
    // order. False when the frame is full.
    bool add(const Instance& instance, std::span<const entity::Matrix> parts);

    // A name tag: `place` puts the tag's own space (pixels of the font, y
    // down, centred on x) in camera-relative space. See-through tags are
    // drawn dim over everything, then normal ones against the depth.
    void add_tag(std::string_view text, const entity::Matrix& place, bool see_through,
                 uint32_t text_colour, uint32_t background, uint8_t sky_light,
                 uint8_t block_light);
    // A flat textured quad, four corners camera-relative, for shadows.
    void add_shadow(const std::array<std::array<float, 5>, 4>& corners, float alpha);

    size_t queued_instances() const { return queued_.size(); }
    size_t queued_parts() const { return matrices_used_; }

    void record(VkCommandBuffer command, const float view_projection[16], const double eye[3],
                uint32_t width, uint32_t height, const float fog[3], float fog_start,
                float fog_end);

    struct FlatVertex {
        float position[3];
        float uv[2];
        uint32_t colour;   // ABGR, as the gpu reads RGBA8
        uint32_t light;    // sky | block << 4 | 0x100 when unlit
    };

private:
    struct Image {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
    };
    struct Buffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void* mapped = nullptr;
    };
    struct BakedModel {
        uint32_t first = 0;
        uint32_t count = 0;
        uint32_t parts = 0;
    };
    struct Queued {
        uint32_t key = 0;   // type << 24 | model
        uint32_t matrix_base = 0;
        uint32_t texture = 0;
        uint32_t light = 0;
        uint32_t colour = 0;
    };

    void create_pipelines(VkFormat colour_format, VkFormat depth_format);
    Buffer make_buffer(VkDeviceSize bytes, VkBufferUsageFlags usage, bool host);
    void destroy_buffer(Buffer& buffer);
    Image make_image(uint32_t width, uint32_t height, uint32_t layers);
    void destroy_image(Image& image);
    void upload(Image& image, uint32_t layer, uint32_t x, uint32_t y, uint32_t width,
                uint32_t height, const uint8_t* rgba, bool first_use);
    void write_descriptors();
    void quad(const FlatVertex (&corners)[4], std::vector<FlatVertex>& out);
    void destroy();

    Device& device_;
    VkCommandPool pool_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet set_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    std::array<VkPipeline, 3> model_pipelines_{};
    // See-through tags, normal tags, shadows.
    std::array<VkPipeline, 3> flat_pipelines_{};

    std::vector<entity::Model> models_;
    std::map<std::string, int32_t, std::less<>> model_index_;
    std::vector<BakedModel> baked_;
    Buffer vertices_;

    Image atlas_;
    uint32_t atlas_width_ = 0;
    uint32_t atlas_height_ = 0;
    std::map<std::string, int32_t, std::less<>> texture_index_;
    std::vector<std::array<float, 4>> rects_;
    Buffer rects_buffer_;
    entity::Font font_;
    int32_t font_texture_ = -1;
    int32_t white_texture_ = -1;
    int32_t shadow_texture_ = -1;

    Image skins_;
    std::array<bool, kSkinLayers> skin_used_{};
    bool skins_ready_ = false;

    static constexpr uint32_t kFrames = 3;
    static constexpr uint32_t kMaxMatrices = 1u << 16;
    static constexpr uint32_t kMaxInstances = 1u << 13;
    static constexpr uint32_t kMaxFlat = 1u << 15;
    uint64_t frame_ = 0;
    Buffer matrices_;    // kFrames rings of 3x4 matrices
    Buffer instances_;   // kFrames rings of instance records
    Buffer flat_;        // kFrames rings of tag and shadow vertices
    uint32_t matrices_used_ = 0;
    std::vector<Queued> queued_;
    std::vector<FlatVertex> see_through_;
    std::vector<FlatVertex> tags_;
    std::vector<FlatVertex> shadows_;
};

} // namespace mcpp::gpu
