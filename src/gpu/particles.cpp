#include "mcpp/gpu/particles.hpp"

#include "vk_util.hpp"
#include "mcpp/image_png.hpp"

#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

#include <mcpp/gpu/shaders/CrackFrag.hpp>
#include <mcpp/gpu/shaders/CrackVert.hpp>
#include <mcpp/gpu/shaders/ParticleFrag.hpp>
#include <mcpp/gpu/shaders/ParticleVert.hpp>

namespace mcpp::gpu {
namespace {

constexpr uint32_t kSlots = 3;

struct Push {
    float view_projection[16];
    float origin[4];
    float eye[4];
    float extra[4];
};

std::vector<uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

} // namespace

ParticleRenderer::ParticleRenderer(Device& device, const WorldRenderer& world,
                                   const std::filesystem::path& cracks)
    : device_(device), world_(world) {
    const VkDevice handle = device_.handle();

    std::vector<uint8_t> layers;
    for (int stage = 0; stage < 10; ++stage) {
        const auto bytes = read_file(cracks / ("destroy_stage_" + std::to_string(stage) + ".png"));
        const auto image = read_png(bytes);
        if (!image || image->width != 16 || image->height != 16) {
            layers.clear();
            break;
        }
        layers.insert(layers.end(), image->rgba.begin(), image->rgba.end());
    }

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo set_layout{};
    set_layout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    set_layout.bindingCount = 1;
    set_layout.pBindings = &binding;
    vk::check(vkCreateDescriptorSetLayout(handle, &set_layout, nullptr, &crack_set_layout_),
              "vkCreateDescriptorSetLayout cracks");

    const VkDescriptorSetLayout sets[2] = {world_.world_set_layout(), crack_set_layout_};
    VkPushConstantRange range{};
    range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    range.size = WorldRenderer::world_push_size();
    VkPipelineLayoutCreateInfo layout{};
    layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout.setLayoutCount = 2;
    layout.pSetLayouts = sets;
    layout.pushConstantRangeCount = 1;
    layout.pPushConstantRanges = &range;
    vk::check(vkCreatePipelineLayout(handle, &layout, nullptr, &layout_), "vkCreatePipelineLayout particles");

    vk::PipelineDesc quads;
    quads.vertex = shaders::ParticleVert();
    quads.fragment = shaders::ParticleFrag();
    quads.layout = layout_;
    quads.stride = sizeof(world::ParticleQuad);
    quads.input_rate = VK_VERTEX_INPUT_RATE_INSTANCE;
    quads.attributes = {
        {0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(world::ParticleQuad, x)},
        {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(world::ParticleQuad, rotation)},
        {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(world::ParticleQuad, u0)},
        {3, 0, VK_FORMAT_R32G32B32A32_UINT, offsetof(world::ParticleQuad, colour)},
    };
    quads.depth_write = true;
    quads.colour_format = world_.colour_format();
    quads.depth_format = world_.depth_format();
    opaque_ = vk::pipeline(device_, quads);
    quads.blend = vk::Blend::Translucent;
    translucent_ = vk::pipeline(device_, quads);

    vk::PipelineDesc crack;
    crack.vertex = shaders::CrackVert();
    crack.fragment = shaders::CrackFrag();
    crack.layout = layout_;
    crack.stride = sizeof(world::CrackVertex);
    crack.attributes = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(world::CrackVertex, position)},
        {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(world::CrackVertex, uv)},
        {2, 0, VK_FORMAT_R32_UINT, offsetof(world::CrackVertex, stage)},
    };
    crack.blend = vk::Blend::Multiply;
    crack.depth_write = false;
    // The game's polygon offset of -1, -10 towards the viewer; depth is
    // reversed here, so towards the viewer is up.
    crack.depth_bias_constant = 10.0f;
    crack.depth_bias_slope = 1.0f;
    crack.colour_format = world_.colour_format();
    crack.depth_format = world_.depth_format();
    crack_ = vk::pipeline(device_, crack);

    auto quad_ring = vk::mapped_buffer(device_, VkDeviceSize{kSlots} * kMaxQuads * sizeof(world::ParticleQuad),
                                       VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    quads_buffer_ = {quad_ring.buffer, quad_ring.memory, quad_ring.mapped};
    auto crack_ring = vk::mapped_buffer(device_, VkDeviceSize{kSlots} * kMaxCrackVertices * sizeof(world::CrackVertex),
                                        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    crack_buffer_ = {crack_ring.buffer, crack_ring.memory, crack_ring.mapped};

    if (layers.empty()) return;
    auto image = vk::sampled_array(device_, VK_FORMAT_R8G8B8A8_UNORM, 16, 16, 10, layers, 4);
    crack_image_ = {image.image, image.memory, image.view};
    crack_sampler_ = vk::sampler(handle, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_REPEAT);
    VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    VkDescriptorPoolCreateInfo pool{};
    pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool.maxSets = 1;
    pool.poolSizeCount = 1;
    pool.pPoolSizes = &size;
    vk::check(vkCreateDescriptorPool(handle, &pool, nullptr, &pool_), "vkCreateDescriptorPool cracks");
    VkDescriptorSetAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocate.descriptorPool = pool_;
    allocate.descriptorSetCount = 1;
    allocate.pSetLayouts = &crack_set_layout_;
    vk::check(vkAllocateDescriptorSets(handle, &allocate, &crack_set_), "vkAllocateDescriptorSets cracks");
    VkDescriptorImageInfo info{crack_sampler_, crack_image_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = crack_set_;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &info;
    vkUpdateDescriptorSets(handle, 1, &write, 0, nullptr);
    cracks_ready_ = true;
}

ParticleRenderer::~ParticleRenderer() { destroy(); }

void ParticleRenderer::destroy() {
    const VkDevice handle = device_.handle();
    for (VkPipeline p : {opaque_, translucent_, crack_}) {
        if (p != VK_NULL_HANDLE) vkDestroyPipeline(handle, p, nullptr);
    }
    if (layout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(handle, layout_, nullptr);
    if (pool_ != VK_NULL_HANDLE) vkDestroyDescriptorPool(handle, pool_, nullptr);
    if (crack_set_layout_ != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(handle, crack_set_layout_, nullptr);
    if (crack_sampler_ != VK_NULL_HANDLE) vkDestroySampler(handle, crack_sampler_, nullptr);
    vk::Image image{crack_image_.image, crack_image_.memory, crack_image_.view};
    image.destroy(handle);
    for (auto* b : {&quads_buffer_, &crack_buffer_}) {
        vk::Buffer buffer{b->buffer, b->memory, b->mapped, 0};
        buffer.destroy(handle);
    }
}

void ParticleRenderer::set_frame(std::span<const world::ParticleQuad> quads,
                                 std::span<const world::CrackVertex> cracks, const double origin[3]) {
    slot_ = (slot_ + 1) % kSlots;
    quad_count_ = std::min<size_t>(quads.size(), kMaxQuads);
    opaque_count_ = 0;
    while (opaque_count_ < quad_count_ &&
           quads[opaque_count_].layer == static_cast<uint32_t>(world::QuadLayer::Opaque)) {
        ++opaque_count_;
    }
    crack_count_ = std::min<size_t>(cracks.size(), kMaxCrackVertices) / 4 * 4;
    auto* q = static_cast<uint8_t*>(quads_buffer_.mapped) + size_t{slot_} * kMaxQuads * sizeof(world::ParticleQuad);
    std::memcpy(q, quads.data(), quad_count_ * sizeof(world::ParticleQuad));
    // Two triangles per crack quad, as a list: the corners repeated in place.
    auto* c = reinterpret_cast<world::CrackVertex*>(static_cast<uint8_t*>(crack_buffer_.mapped) +
                                                    size_t{slot_} * kMaxCrackVertices * sizeof(world::CrackVertex));
    size_t written = 0;
    for (size_t i = 0; i + 4 <= crack_count_ && written + 6 <= kMaxCrackVertices; i += 4) {
        for (const size_t k : {0, 1, 2, 2, 3, 0}) c[written++] = cracks[i + k];
    }
    crack_count_ = written;
    std::copy(origin, origin + 3, origin_);
}

void ParticleRenderer::push(VkCommandBuffer command, const float view_projection[16], const Camera& camera) const {
    Push push{};
    std::memcpy(push.view_projection, view_projection, sizeof(push.view_projection));
    for (int i = 0; i < 3; ++i) {
        push.origin[i] = static_cast<float>(origin_[i]);
        push.eye[i] = camera.eye[i];
    }
    vkCmdPushConstants(command, layout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       WorldRenderer::world_push_size(), &push);
}

void ParticleRenderer::record_cracks(VkCommandBuffer command, const float view_projection[16],
                                     const Camera& camera) const {
    if (!cracks_ready_ || crack_count_ == 0) return;
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, crack_);
    world_.bind_world_set(command, layout_);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 1, 1, &crack_set_, 0, nullptr);
    push(command, view_projection, camera);
    const VkDeviceSize offset = VkDeviceSize{slot_} * kMaxCrackVertices * sizeof(world::CrackVertex);
    vkCmdBindVertexBuffers(command, 0, 1, &crack_buffer_.buffer, &offset);
    vkCmdDraw(command, static_cast<uint32_t>(crack_count_), 1, 0, 0);
}

void ParticleRenderer::record_particles(VkCommandBuffer command, const float view_projection[16],
                                        const Camera& camera) const {
    if (quad_count_ == 0) return;
    world_.bind_world_set(command, layout_);
    push(command, view_projection, camera);
    const VkDeviceSize offset = VkDeviceSize{slot_} * kMaxQuads * sizeof(world::ParticleQuad);
    vkCmdBindVertexBuffers(command, 0, 1, &quads_buffer_.buffer, &offset);
    if (opaque_count_ > 0) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, opaque_);
        vkCmdDraw(command, 6, static_cast<uint32_t>(opaque_count_), 0, 0);
    }
    if (quad_count_ > opaque_count_) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, translucent_);
        vkCmdDraw(command, 6, static_cast<uint32_t>(quad_count_ - opaque_count_), 0,
                  static_cast<uint32_t>(opaque_count_));
    }
}

} // namespace mcpp::gpu
