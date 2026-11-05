#pragma once
// Particles and block cracks on the gpu. What is drawn comes from the game's
// code (mcpp/world/particles.hpp, mcpp/world/destruction.hpp); this is only how:
// every particle of a frame in one instanced draw from a persistently mapped
// ring, the cracks in one more, both inside the world's own pass and with its
// textures, fog and lightmap.
#include "mcpp/gpu/device.hpp"
#include "mcpp/gpu/world_renderer.hpp"
#include "mcpp/world/destruction.hpp"
#include "mcpp/world/particles.hpp"

#include <filesystem>
#include <span>

namespace mcpp::gpu {

class ParticleRenderer {
public:
    // The cracks come from destroy_stage_0.png to _9.png in `cracks`; without
    // them no crack is drawn.
    ParticleRenderer(Device& device, const WorldRenderer& world, const std::filesystem::path& cracks);
    ~ParticleRenderer();
    ParticleRenderer(const ParticleRenderer&) = delete;
    ParticleRenderer& operator=(const ParticleRenderer&) = delete;

    // This frame's quads and crack corners, relative to `origin`. Copied into
    // the next slot of the ring, which no frame in flight still reads.
    void set_frame(std::span<const world::ParticleQuad> quads, std::span<const world::CrackVertex> cracks,
                   const double origin[3]);
    void record_cracks(VkCommandBuffer command, const float view_projection[16], const Camera& camera) const;
    void record_particles(VkCommandBuffer command, const float view_projection[16], const Camera& camera) const;

    size_t quads() const { return quad_count_; }
    bool has_cracks() const { return cracks_ready_; }

    static constexpr uint32_t kMaxQuads = 2 * world::ParticleEngine::kGroupLimit;
    static constexpr uint32_t kMaxCrackVertices = 8192;

private:
    void push(VkCommandBuffer command, const float view_projection[16], const Camera& camera) const;
    void destroy();

    Device& device_;
    const WorldRenderer& world_;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline opaque_ = VK_NULL_HANDLE;
    VkPipeline translucent_ = VK_NULL_HANDLE;
    VkPipeline crack_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout crack_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkDescriptorSet crack_set_ = VK_NULL_HANDLE;
    VkSampler crack_sampler_ = VK_NULL_HANDLE;
    struct Buffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void* mapped = nullptr;
    } quads_buffer_, crack_buffer_;
    struct Image {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
    } crack_image_;
    bool cracks_ready_ = false;
    uint32_t slot_ = 0;
    size_t quad_count_ = 0;
    size_t opaque_count_ = 0;
    size_t crack_count_ = 0;
    double origin_[3] = {0.0, 0.0, 0.0};
};

} // namespace mcpp::gpu
