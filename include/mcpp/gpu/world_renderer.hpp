#pragma once
#include "mcpp/gpu/device.hpp"
#include "mcpp/world/atlas.hpp"
#include "mcpp/world/mesher.hpp"

#include <cstdint>
#include <vector>

namespace mcpp::gpu {

struct Camera {
    float eye[3];
    float target[3];
    float fov_degrees = 70.0f;
    float near_plane = 0.1f;
    float far_plane = 2000.0f;
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
    size_t vertex_capacity() const { return vertex_capacity_; }
    size_t appended_sections() const { return sections_.size(); }
    bool out_of_vertex_room() const { return out_of_vertex_room_; }
    std::vector<uint8_t> render(const Camera& camera);

    // Draws a frame without reading it back, which is what a frame costs
    // in a running client; the readback alone is several milliseconds.
    double draw(const Camera& camera);

    // Records a frame and leaves the colour image in TRANSFER_SRC_OPTIMAL, so a
    // window can blit it into a swapchain image in the same submission.
    void record_present(VkCommandBuffer command, const Camera& camera);
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
                      bool leave_transfer_src);
    double submit(VkCommandBuffer command);
    void upload_merged(const world::Mesh& mesh);
    void upload_sections(const std::vector<world::SectionMesh>& sections,
                         const std::vector<size_t>& order);
    void create_targets();
    void create_atlas(const world::Atlas& atlas);
    void create_pipeline();
    void destroy_mesh();

    Device& device_;
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
        int32_t vertex_offset;
    };
    std::vector<Section> sections_;
    bool culling_ = true;
    size_t drawn_sections_ = 0;
    size_t drawn_triangles_ = 0;

    uint32_t index_count_ = 0;
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
};

} // namespace mcpp::gpu
