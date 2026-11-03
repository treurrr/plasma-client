#pragma once
#include "mcpp/gpu/device.hpp"
#include "mcpp/gpu/span_allocator.hpp"
#include "mcpp/world/atlas.hpp"
#include "mcpp/world/mesher.hpp"

#include <cstdint>
#include <map>
#include <climits>
#include <array>
#include <functional>
#include <span>
#include <vector>

namespace mcpp::gpu {

class Sky;
class Outline;

struct Camera {
    float eye[3];
    float target[3];
    float fov_degrees = 70.0f;
    // Tilt around the direction of view. Nothing in a standing world needs it;
    // a walking one leans into every step.
    float roll_degrees = 0.0f;
    float near_plane = 0.1f;
    float far_plane = 2000.0f;
    // Distance in blocks where the world starts and finishes fading into the
    // sky. Zero means no fog, which is what a screenshot of the raw geometry
    // wants and what a game never does.
    float fog_start = 0.0f;
    float fog_end = 0.0f;
};

// Draws a meshed world into an image we read back. The window path will reuse
// the same pipeline, buffers and recording.
class WorldRenderer {
public:
    WorldRenderer(Device& device, uint32_t width, uint32_t height,
                  const world::Atlas& atlas);
    ~WorldRenderer();

    WorldRenderer(const WorldRenderer&) = delete;
    WorldRenderer& operator=(const WorldRenderer&) = delete;

    void upload(const world::Mesh& mesh);
    void upload(const std::vector<world::SectionMesh>& sections);

    // Streaming: reserve once, then append chunks as the server sends them.
    // Appending never reallocates, so a chunk arriving mid-game cannot stall
    // the frame the way a full re-upload would.
    void reserve(size_t vertex_capacity, size_t index_capacity);
    bool append(const std::vector<world::SectionMesh>& sections);
    // Re-meshes one section in place. The old room is left behind rather than
    // compacted: one block change costs a section's worth of buffer, which is
    // cheap next to stalling the frame to shuffle everything down.
    bool replace(const std::vector<world::SectionMesh>& sections);
    // Stops drawing every section of a chunk. The room it held is not handed
    // back yet; that needs an allocator, not a flag.
    void forget(int32_t chunk_x, int32_t chunk_z);
    size_t vertex_capacity() const { return vertex_capacity_; }
    size_t appended_sections() const { return sections_.size(); }
    // How the streaming buffers stand: room in use, and how many separate free
    // pieces it is spread over, which is what decides whether a big chunk fits.
    const SpanAllocator& vertex_room() const { return vertex_spans_; }
    const SpanAllocator& index_room() const { return index_spans_; }
    bool out_of_vertex_room() const { return out_of_vertex_room_; }
    // Recorded into the same image after the world pass, so an interface can
    // be read back with the frame it belongs to.
    using Overlay = std::function<void(VkCommandBuffer, uint32_t, uint32_t)>;
    std::vector<uint8_t> render(const Camera& camera, const Overlay& overlay = {});

    // Draws a frame without reading it back, which is what a frame costs
    // in a running client; the readback alone is several milliseconds.
    double draw(const Camera& camera);

    // Records a frame and leaves the colour image in TRANSFER_SRC_OPTIMAL, so a
    // window can blit it into a swapchain image in the same submission.
    // Drawn inside the world's own pass, before the terrain, so it needs no
    // second attachment and no second clear.
    void set_sky(const Sky* sky) { sky_ = sky; }
    // The block to draw a box around, or nothing when the crosshair is on sky.
    void set_outline(const Outline* outline) { outline_ = outline; }
    void set_outlined_block(const int32_t block[3], bool present) {
        outlined_present_ = present;
        if (present) {
            outlined_[0] = block[0];
            outlined_[1] = block[1];
            outlined_[2] = block[2];
        }
    }
    // Recorded in the world's own pass after the terrain, against its depth:
    // entities, and whatever else is in the world without being a block.
    using InWorld = std::function<void(VkCommandBuffer, const float* view_projection,
                                       uint32_t width, uint32_t height, const float* fog,
                                       const Camera& camera)>;
    void set_in_world(InWorld draw) { in_world_ = std::move(draw); }
    // Four colours per biome - grass, foliage, water, dry foliage - packed
    // 0xRRGGBB, in the order the server listed the biomes.
    static constexpr uint32_t kBiomeSlots = 256;
    void set_biome_colours(std::span<const uint32_t> colours);
    // The blue straight up and the pale blue the land fades into, both
    // 0xRRGGBB. The game works them out from the biome rather than picking
    // them, so the client does the same.
    void set_sky_colours(uint32_t zenith, uint32_t fog);
    void record_present(VkCommandBuffer command, const Camera& camera);
    VkFormat colour_format() const;
    VkImage color_image() const { return color_; }
    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }
    void read_timestamps();

    double last_gpu_ms() const { return last_gpu_ms_; }
    size_t section_count() const { return sections_.size(); }
    size_t drawn_sections() const { return drawn_sections_; }
    size_t drawn_triangles() const { return drawn_triangles_; }
    double upload_ms() const { return upload_ms_; }
    double upload_alloc_ms() const { return upload_alloc_ms_; }
    double upload_copy_ms() const { return upload_copy_ms_; }
    double upload_gpu_ms() const { return upload_gpu_ms_; }
    size_t uploaded_bytes() const { return vertex_bytes_ + index_bytes_; }
    uint32_t index_count() const { return index_count_; }

private:
    void record_frame(VkCommandBuffer command, const Camera& camera, bool readback);
    void record_frame(VkCommandBuffer command, const Camera& camera, bool readback,
                      bool leave_transfer_src, const Overlay& overlay = {});
    double submit(VkCommandBuffer command);
    void upload_merged(const world::Mesh& mesh);
    void upload_sections(const std::vector<world::SectionMesh>& sections,
                         const std::vector<size_t>& order);
    void create_targets();
    void create_atlas(const world::Atlas& atlas);
    void create_pipeline();
    void draw_geometry(VkCommandBuffer command, struct PushConstants& push);
    void destroy_mesh();

    Device& device_;
    const Sky* sky_ = nullptr;
    const Outline* outline_ = nullptr;
    int32_t outlined_[3]{};
    bool outlined_present_ = false;
    float zenith_[3] = {0.4745f, 0.6549f, 1.0f};
    float fog_[3] = {0.7529f, 0.8471f, 1.0f};
    uint32_t width_;
    uint32_t height_;

    VkImage color_ = VK_NULL_HANDLE;
    VkDeviceMemory color_memory_ = VK_NULL_HANDLE;
    VkImageView color_view_ = VK_NULL_HANDLE;
    VkImage depth_ = VK_NULL_HANDLE;
    VkDeviceMemory depth_memory_ = VK_NULL_HANDLE;
    VkImageView depth_view_ = VK_NULL_HANDLE;
    VkBuffer readback_ = VK_NULL_HANDLE;
    VkDeviceMemory readback_memory_ = VK_NULL_HANDLE;

    VkBuffer vertex_buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory vertex_memory_ = VK_NULL_HANDLE;
    VkBuffer index_buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory index_memory_ = VK_NULL_HANDLE;

    VkImage atlas_image_ = VK_NULL_HANDLE;
    VkDeviceMemory atlas_memory_ = VK_NULL_HANDLE;
    VkImageView atlas_view_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set_ = VK_NULL_HANDLE;
    uint32_t atlas_tile_ = 0;
    uint32_t atlas_layers_ = 0;

    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipeline blended_pipeline_ = VK_NULL_HANDLE;
    VkBuffer biome_buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory biome_memory_ = VK_NULL_HANDLE;
    uint32_t* biome_colours_ = nullptr;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkCommandPool pool_ = VK_NULL_HANDLE;
    // Allocated once. A command buffer and a fence per frame were costing more
    // CPU than the whole draw cost GPU.
    VkCommandBuffer frame_command_ = VK_NULL_HANDLE;
    VkFence frame_fence_ = VK_NULL_HANDLE;
    VkQueryPool timestamps_ = VK_NULL_HANDLE;

    // One entry per section: where its geometry sits and where it is in the
    // world, so a section outside the view can be skipped entirely.
    struct Section {
        float origin[3];
        float bounds_min[3];
        float bounds_max[3];
        uint32_t first_index;
        uint32_t index_count;
        // The opaque indices come first; the rest are blended afterwards.
        uint32_t opaque_count;
        int32_t vertex_offset;
        uint32_t vertex_count;
    };
    std::vector<Section> sections_;

    // Room in the two streaming buffers, counted in vertices and in indices.
    SpanAllocator vertex_spans_;
    SpanAllocator index_spans_;
    // Room let go of is held back a few frames before it is handed out again:
    // a frame still in flight may be reading from it.
    struct Releasing {
        size_t vertex_offset;
        size_t vertex_count;
        size_t first_index;
        size_t index_count;
        uint64_t free_after;
    };
    std::vector<Releasing> releasing_;
    uint64_t frames_recorded_ = 0;

    // The see-through quads of a section, kept on the cpu so they can be put
    // back to front again as the camera moves: inside one section the mesher
    // emits them in block order, which is right from one side only.
    struct Blend {
        uint32_t first = 0;               // where they sit in the index buffer
        std::vector<uint32_t> indices;    // six per quad, as uploaded
        std::vector<float> centres;       // three per quad, in world space
        int32_t sorted_for[3]{INT32_MIN, INT32_MIN, INT32_MIN};
    };
    std::map<std::array<int32_t, 3>, Blend> blends_;
    void sort_translucent(VkCommandBuffer command, const Camera& camera);
    void keep_blends(const std::vector<world::SectionMesh>& sections,
                     const std::vector<size_t>& source, size_t first_section);
    template <typename Drop> void drop_sections(Drop&& drop);
    void hand_back_released();
    bool culling_ = true;
    size_t drawn_sections_ = 0;
    size_t drawn_triangles_ = 0;

    uint32_t index_count_ = 0;
    uint32_t opaque_index_count_ = 0;
    std::vector<const Section*> visible_;
    bool out_of_vertex_room_ = false;
    size_t vertex_capacity_ = 0;
    size_t index_capacity_ = 0;
    VkBuffer append_staging_ = VK_NULL_HANDLE;
    VkDeviceMemory append_staging_memory_ = VK_NULL_HANDLE;
    void* append_mapped_ = nullptr;
    size_t append_staging_bytes_ = 0;
    size_t vertex_bytes_ = 0;
    size_t index_bytes_ = 0;
    double last_gpu_ms_ = 0.0;
    double upload_ms_ = 0.0;
    double upload_alloc_ms_ = 0.0;
    double upload_copy_ms_ = 0.0;
    double upload_gpu_ms_ = 0.0;
    float timestamp_period_ = 1.0f;
    InWorld in_world_;
};

} // namespace mcpp::gpu
