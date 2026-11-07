#include "mcpp/gpu/weather.hpp"

#include "vk_util.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <vector>

#include <mcpp/gpu/shaders/WeatherFrag.hpp>
#include <mcpp/gpu/shaders/WeatherVert.hpp>

namespace mcpp::gpu {
namespace {

constexpr uint32_t kSlots = 3;
constexpr uint32_t kSheetWidth = 64;
constexpr uint32_t kSheetHeight = 256;
constexpr size_t kSheetBytes = size_t{kSheetWidth} * kSheetHeight * 4;
// bake_sky.py's layout: the magic and version, the cloud sheet, the
// celestials, the end sky, then rain and snow.
constexpr size_t kBeforeRain = 8 + size_t{256} * 256 + size_t{64} * 64 * 4 * 10 +
                               size_t{128} * 128 * 4;

struct Push {
    float view_projection[16];
    float origin[4];
    float eye[4];
    float extra[4];
};

} // namespace

WeatherRenderer::WeatherRenderer(Device& device, const WorldRenderer& world,
                                 const std::filesystem::path& sky_textures)
    : device_(device), world_(world) {
    const VkDevice handle = device_.handle();

    std::vector<uint8_t> sheets(kSheetBytes * 2);
    {
        std::ifstream input(sky_textures, std::ios::binary);
        input.seekg(static_cast<std::streamoff>(kBeforeRain));
        input.read(reinterpret_cast<char*>(sheets.data()), static_cast<std::streamsize>(sheets.size()));
        if (!input) return;
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
    vk::check(vkCreateDescriptorSetLayout(handle, &set_layout, nullptr, &set_layout_),
              "vkCreateDescriptorSetLayout weather");

    const VkDescriptorSetLayout sets[2] = {world_.world_set_layout(), set_layout_};
    VkPushConstantRange range{};
    range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    range.size = WorldRenderer::world_push_size();
    VkPipelineLayoutCreateInfo layout{};
    layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout.setLayoutCount = 2;
    layout.pSetLayouts = sets;
    layout.pushConstantRangeCount = 1;
    layout.pPushConstantRanges = &range;
    vk::check(vkCreatePipelineLayout(handle, &layout, nullptr, &layout_),
              "vkCreatePipelineLayout weather");

    vk::PipelineDesc desc;
    desc.vertex = shaders::WeatherVert();
    desc.fragment = shaders::WeatherFrag();
    desc.layout = layout_;
    desc.stride = sizeof(world::WeatherVertex);
    desc.attributes = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(world::WeatherVertex, x)},
        {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(world::WeatherVertex, u)},
        {2, 0, VK_FORMAT_R32_UINT, offsetof(world::WeatherVertex, colour)},
        {3, 0, VK_FORMAT_R32_UINT, offsetof(world::WeatherVertex, light)},
    };
    desc.blend = vk::Blend::Translucent;
    // The game draws weather with the depth the terrain left and writes none,
    // so a column behind a hill is hidden and two columns do not fight.
    desc.depth_write = false;
    desc.colour_format = world_.colour_format();
    desc.depth_format = world_.depth_format();
    pipeline_ = vk::pipeline(device_, desc);

    auto ring = vk::mapped_buffer(device_, VkDeviceSize{kSlots} * kMaxVertices * sizeof(world::WeatherVertex),
                                  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    vertices_ = {ring.buffer, ring.memory, ring.mapped};

    auto image = vk::sampled_array(device_, VK_FORMAT_R8G8B8A8_UNORM, kSheetWidth, kSheetHeight, 2,
                                   sheets, 4);
    texture_ = {image.image, image.memory, image.view};
    // The texture runs far past its own height down a column, so it repeats.
    sampler_ = vk::sampler(handle, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_REPEAT);

    VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    VkDescriptorPoolCreateInfo pool{};
    pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool.maxSets = 1;
    pool.poolSizeCount = 1;
    pool.pPoolSizes = &size;
    vk::check(vkCreateDescriptorPool(handle, &pool, nullptr, &pool_), "vkCreateDescriptorPool weather");
    VkDescriptorSetAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocate.descriptorPool = pool_;
    allocate.descriptorSetCount = 1;
    allocate.pSetLayouts = &set_layout_;
    vk::check(vkAllocateDescriptorSets(handle, &allocate, &set_), "vkAllocateDescriptorSets weather");
    VkDescriptorImageInfo info{sampler_, texture_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set_;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &info;
    vkUpdateDescriptorSets(handle, 1, &write, 0, nullptr);
    ready_ = true;
}

WeatherRenderer::~WeatherRenderer() { destroy(); }

void WeatherRenderer::destroy() {
    const VkDevice handle = device_.handle();
    if (pipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(handle, pipeline_, nullptr);
    if (layout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(handle, layout_, nullptr);
    if (pool_ != VK_NULL_HANDLE) vkDestroyDescriptorPool(handle, pool_, nullptr);
    if (set_layout_ != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(handle, set_layout_, nullptr);
    if (sampler_ != VK_NULL_HANDLE) vkDestroySampler(handle, sampler_, nullptr);
    vk::Image image{texture_.image, texture_.memory, texture_.view};
    image.destroy(handle);
    vk::Buffer buffer{vertices_.buffer, vertices_.memory, vertices_.mapped, 0};
    buffer.destroy(handle);
}

// `WeatherEffectRenderer.render`: rain at full alpha and snow at four fifths,
// each column four corners, written out as the two triangles of its quad.
void WeatherRenderer::set_frame(const world::WeatherState& state, const double camera[3]) {
    slot_ = (slot_ + 1) % kSlots;
    std::copy(camera, camera + 3, origin_);
    rain_count_ = 0;
    snow_count_ = 0;
    if (!ready_) return;

    auto* out = reinterpret_cast<world::WeatherVertex*>(
        static_cast<uint8_t*>(vertices_.mapped) + size_t{slot_} * kMaxVertices * sizeof(world::WeatherVertex));
    size_t written = 0;
    const auto emit = [&](const std::vector<world::WeatherColumn>& columns, float alpha) {
        scratch_.clear();
        world::weather_quads(columns, camera, alpha, state.radius, state.intensity, scratch_);
        size_t count = 0;
        for (size_t i = 0; i + 4 <= scratch_.size() && written + 6 <= kMaxVertices; i += 4) {
            for (const size_t k : {0, 1, 2, 2, 3, 0}) out[written++] = scratch_[i + k];
            count += 6;
        }
        return count;
    };
    rain_count_ = emit(state.rain, 1.0f);
    snow_count_ = emit(state.snow, 0.8f);
}

void WeatherRenderer::draw(VkCommandBuffer command, const float view_projection[16],
                           const Camera& camera, size_t first, size_t count, float layer) const {
    if (count == 0) return;
    Push push{};
    std::memcpy(push.view_projection, view_projection, sizeof(push.view_projection));
    for (int i = 0; i < 3; ++i) {
        push.origin[i] = static_cast<float>(origin_[i]);
        push.eye[i] = camera.eye[i];
    }
    push.extra[0] = layer;
    vkCmdPushConstants(command, layout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       WorldRenderer::world_push_size(), &push);
    const VkDeviceSize offset = VkDeviceSize{slot_} * kMaxVertices * sizeof(world::WeatherVertex);
    vkCmdBindVertexBuffers(command, 0, 1, &vertices_.buffer, &offset);
    vkCmdDraw(command, static_cast<uint32_t>(count), 1, static_cast<uint32_t>(first), 0);
}

void WeatherRenderer::record(VkCommandBuffer command, const float view_projection[16],
                             const Camera& camera) const {
    if (!ready_ || (rain_count_ == 0 && snow_count_ == 0)) return;
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    world_.bind_world_set(command, layout_);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 1, 1, &set_, 0, nullptr);
    draw(command, view_projection, camera, 0, rain_count_, 0.0f);
    draw(command, view_projection, camera, rain_count_, snow_count_, 1.0f);
}

} // namespace mcpp::gpu
