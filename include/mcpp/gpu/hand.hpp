#pragma once
// The arm and the item in front of the camera. What is drawn and where comes
// from the game (src/client/hand.cpp); this is only how. Every quad of the
// frame - both hands, the item's whole model, the sleeve - is posed on the cpu
// into one mapped buffer and goes down as a single draw, which is the whole
// hand: a few hundred vertices against two textures, once a frame.
#include "mcpp/entity/model.hpp"
#include "mcpp/gpu/device.hpp"
#include "mcpp/gpu/world_renderer.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace mcpp::gpu {

class HandRenderer {
public:
    // `held` is the item geometry tools/bake_held.py writes; without it the
    // arm still draws and nothing in the hand does.
    HandRenderer(Device& device, VkFormat colour_format, VkFormat depth_format,
                 const std::filesystem::path& held);
    ~HandRenderer();

    HandRenderer(const HandRenderer&) = delete;
    HandRenderer& operator=(const HandRenderer&) = delete;

    bool has_items() const { return item_count_ > 0; }
    uint32_t item_quads(int32_t item) const;
    // The model's own display transform for one hand, as rotation in degrees,
    // translation in blocks, scale, and whether the model names the context.
    struct Display {
        float rotation[3]{};
        float translation[3]{};
        float scale[3]{1.0f, 1.0f, 1.0f};
        bool present = false;
    };
    Display item_display(int32_t item, bool left_hand) const;

    // The arm's own geometry, from the player model. Both are kept, so a slim
    // skin and a wide one each get the arm that fits them.
    void load_arm(const entity::Model& wide, const entity::Model& slim);
    // The skin the arm wears, 64 by 64 rgba.
    void set_skin(std::span<const uint8_t> rgba);
    bool has_skin() const { return skin_ready_; }

    // One frame: whatever was added for the last is dropped.
    void begin();
    // `pose` takes the item's own space - a block from corner to corner, its
    // display transform already in it - to camera space. `pull` is how far a
    // bow or a crossbow is drawn, which is the only thing that swaps a model
    // while it is in the hand.
    void add_item(int32_t item, const entity::Matrix& pose, float pull = 0.0f);
    // `pose` takes the arm's hand end to camera space, as renderPlayerArm
    // leaves it.
    void add_arm(const entity::Matrix& pose, bool left_arm, bool slim, bool sleeve);
    size_t vertices() const { return vertices_.size(); }

    // `ItemInHandLayer`: an item in the hand of something in the world, which
    // is the same geometry posed into a different space, so it is a queue of
    // its own. `pose` takes the item's own space to camera-relative world
    // space - where the entity renderer's part matrices leave their
    // vertices - and `light` is the lightmap colour where that entity stands.
    // Items added one after another under the same light are one draw.
    void add_world_item(int32_t item, const entity::Matrix& pose, const float light[3],
                        float pull = 0.0f);
    // The world's own pass, after the terrain and against its depth: the queue
    // above, with the world's view-projection and the game's two lights
    // pointing the way they do in the world. `eye` is where the camera stands,
    // as `EntityRenderer::record` takes it, because the queue is relative to it.
    void record_world(VkCommandBuffer command, const float view_projection[16],
                      const double eye[3], uint32_t width, uint32_t height);
    size_t world_vertices() const { return world_.size(); }

    // `GameRenderer.getFov` for the hand: seventy degrees, whatever the fov
    // option says, and a hundred blocks deep.
    static void projection(float aspect, float out[16]);
    // `light` is what the world's lightmap says where the player stands, and
    // the camera's turn is what the world's own two lights are rotated by:
    // they point the same way in the world however the player faces.
    void record(VkCommandBuffer command, uint32_t width, uint32_t height, const float light[3],
                const Camera& camera);

    static std::filesystem::path default_path(int32_t protocol);

    struct Vertex {
        float position[3];
        float uv[2];
        float normal[3];
        uint32_t layer;    // the skin's own bit, then the texture's layer
        uint32_t colour;   // rgba8, the model's tint
    };
    // What this frame will draw, for a shot that wants to measure it.
    std::span<const Vertex> queued() const { return vertices_; }
    std::span<const Vertex> queued_world() const { return world_; }

    static constexpr uint32_t kSkinBit = 1u << 31;
    static constexpr float kFovDegrees = 70.0f;
    static constexpr float kNear = 0.05f;
    static constexpr float kFar = 100.0f;

private:
    // Exactly as tools/bake_held.py writes it: four corners, each a place
    // and a texture coordinate, then the face's normal and what it reads.
    struct Quad {
        struct Corner {
            float position[3];
            float uv[2];
        } corners[4];
        float normal[3];
        uint32_t layer;
        uint32_t colour;
    };
    static constexpr uint32_t kStages = 4;
    struct Entry {
        uint32_t first[kStages]{};
        uint32_t count[kStages]{};
        float threshold[kStages - 1]{2.0f, 2.0f, 2.0f};
        Display right;
        Display left;
    };
    struct ArmModel {
        std::vector<entity::Vertex> vertices;
        const entity::Model* model = nullptr;
        int32_t arm[2] = {-1, -1};      // right, left
        int32_t sleeve[2] = {-1, -1};
    };
    // One run of the world queue: the vertices one lightmap colour covers.
    struct WorldRun {
        uint32_t first = 0;
        uint32_t count = 0;
        float light[3]{1.0f, 1.0f, 1.0f};
    };
    void load(const std::filesystem::path& held);
    void bake_arm(const entity::Model& model, ArmModel& out);
    void add_triangle(const entity::Vertex& vertex, const entity::Matrix& pose);
    // One item's quads at the stage `pull` picks, posed into whichever queue.
    void add_quads(std::vector<Vertex>& out, uint32_t limit, int32_t item,
                   const entity::Matrix& pose, float pull);
    void destroy();

    Device& device_;
    std::vector<Quad> quads_;
    std::vector<Entry> items_;
    ArmModel wide_, slim_;
    uint32_t item_count_ = 0;
    uint32_t tile_ = 16;
    uint32_t layers_ = 0;

    std::vector<Vertex> vertices_;
    std::vector<Vertex> world_;
    std::vector<WorldRun> runs_;
    static constexpr uint32_t kSlots = 3;
    static constexpr uint32_t kMaxVertices = 1u << 16;
    static constexpr uint32_t kMaxWorldVertices = 1u << 16;
    // Each slot holds the frame's camera-space queue, then its world one.
    static constexpr uint32_t kSlotVertices = kMaxVertices + kMaxWorldVertices;
    uint32_t slot_ = 0;

    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkDescriptorSet set_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkImage items_image_ = VK_NULL_HANDLE;
    VkDeviceMemory items_memory_ = VK_NULL_HANDLE;
    VkImageView items_view_ = VK_NULL_HANDLE;
    VkImage skin_image_ = VK_NULL_HANDLE;
    VkDeviceMemory skin_memory_ = VK_NULL_HANDLE;
    VkImageView skin_view_ = VK_NULL_HANDLE;
    bool skin_ready_ = false;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    void* mapped_ = nullptr;
};

} // namespace mcpp::gpu
