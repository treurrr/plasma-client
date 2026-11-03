#include "mcpp/gpu/entity_renderer.hpp"

#include "mcpp/byte_buffer.hpp"

#include <cstring>
#include <format>

#include <mcpp/gpu/shaders/EntityFrag.hpp>
#include <mcpp/gpu/shaders/EntityVert.hpp>

namespace mcpp::gpu {
namespace {

constexpr const char* kWhite = "$white";
constexpr const char* kFont = "$font";

void check(VkResult result, std::string_view what) {
    if (result != VK_SUCCESS) {
        throw ProtocolError(std::format("{} failed with VkResult {}", what,
                                        static_cast<int>(result)));
    }
}

uint32_t memory_type(VkPhysicalDevice physical, uint32_t mask, VkMemoryPropertyFlags wanted) {
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(physical, &properties);
    for (uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
        if ((mask & (1u << i)) == 0) continue;
        if ((properties.memoryTypes[i].propertyFlags & wanted) == wanted) return i;
    }
    throw ProtocolError("no suitable memory type on this gpu");
}

VkShaderModule make_module(VkDevice device, std::span<const uint32_t> words) {
    VkShaderModuleCreateInfo create{};
    create.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    create.codeSize = words.size() * sizeof(uint32_t);
    create.pCode = words.data();
    VkShaderModule module = VK_NULL_HANDLE;
    check(vkCreateShaderModule(device, &create, nullptr, &module), "vkCreateShaderModule");
    return module;
}

struct EntityPush {
    float view_projection[16];
    float fog[4];      // rgb, and where it finishes
    float params[4];   // where fog starts, sky light, block light, 1 when lit
    float overlay[4];
    float colour[4];
};
static_assert(sizeof(EntityPush) <= 128, "push constants must fit the guaranteed minimum");

} // namespace

EntityRenderer::EntityRenderer(Device& device, VkFormat colour_format, VkFormat depth_format)
    : device_(device) {
    try {
        VkCommandPoolCreateInfo pool{};
        pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool.queueFamilyIndex = device_.graphics_family();
        check(vkCreateCommandPool(device_.handle(), &pool, nullptr, &pool_), "vkCreateCommandPool");

        VkBufferCreateInfo buffer{};
        buffer.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer.size = sizeof(entity::Vertex) * kVerticesPerFrame * kFrames;
        buffer.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        check(vkCreateBuffer(device_.handle(), &buffer, nullptr, &vertices_),
              "vkCreateBuffer entities");
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device_.handle(), vertices_, &requirements);
        VkMemoryAllocateInfo allocate{};
        allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocate.allocationSize = requirements.size;
        allocate.memoryTypeIndex =
            memory_type(device_.physical(), requirements.memoryTypeBits,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        check(vkAllocateMemory(device_.handle(), &allocate, nullptr, &vertex_memory_),
              "vkAllocateMemory entities");
        check(vkBindBufferMemory(device_.handle(), vertices_, vertex_memory_, 0),
              "vkBindBufferMemory entities");
        void* mapped = nullptr;
        check(vkMapMemory(device_.handle(), vertex_memory_, 0, VK_WHOLE_SIZE, 0, &mapped),
              "vkMapMemory entities");
        mapped_ = static_cast<entity::Vertex*>(mapped);

        create_pipelines(colour_format, depth_format);

        entity::Texture white;
        white.width = white.height = 1;
        white.rgba = {255, 255, 255, 255};
        upload_texture(kWhite, white);
    } catch (...) {
        destroy();
        throw;
    }
}

EntityRenderer::~EntityRenderer() {
    if (device_.handle() == VK_NULL_HANDLE) return;
    vkDeviceWaitIdle(device_.handle());
    destroy();
}

bool EntityRenderer::has_texture(std::string_view name) const { return slots_.contains(name); }

void EntityRenderer::upload_texture(const std::string& name, const entity::Texture& texture) {
    if (texture.width == 0 || texture.height == 0) return;
    if (const auto found = slots_.find(name); found != slots_.end()) {
        retired_.push_back({found->second, frame_ + kFrames});
        slots_.erase(found);
    }
    Slot slot;
    VkImageCreateInfo create{};
    create.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    create.imageType = VK_IMAGE_TYPE_2D;
    create.format = VK_FORMAT_R8G8B8A8_UNORM;
    create.extent = {texture.width, texture.height, 1};
    create.mipLevels = 1;
    create.arrayLayers = 1;
    create.samples = VK_SAMPLE_COUNT_1_BIT;
    create.tiling = VK_IMAGE_TILING_OPTIMAL;
    create.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    check(vkCreateImage(device_.handle(), &create, nullptr, &slot.image), "vkCreateImage entity");

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device_.handle(), slot.image, &requirements);
    VkMemoryAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = memory_type(device_.physical(), requirements.memoryTypeBits,
                                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    check(vkAllocateMemory(device_.handle(), &allocate, nullptr, &slot.memory),
          "vkAllocateMemory entity");
    check(vkBindImageMemory(device_.handle(), slot.image, slot.memory, 0),
          "vkBindImageMemory entity");

    const size_t bytes = texture.rgba.size();
    VkBufferCreateInfo staging_create{};
    staging_create.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    staging_create.size = bytes;
    staging_create.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VkBuffer staging = VK_NULL_HANDLE;
    check(vkCreateBuffer(device_.handle(), &staging_create, nullptr, &staging),
          "vkCreateBuffer entity staging");
    VkMemoryRequirements staging_requirements{};
    vkGetBufferMemoryRequirements(device_.handle(), staging, &staging_requirements);
    VkMemoryAllocateInfo staging_allocate{};
    staging_allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    staging_allocate.allocationSize = staging_requirements.size;
    staging_allocate.memoryTypeIndex =
        memory_type(device_.physical(), staging_requirements.memoryTypeBits,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkDeviceMemory staging_memory = VK_NULL_HANDLE;
    check(vkAllocateMemory(device_.handle(), &staging_allocate, nullptr, &staging_memory),
          "vkAllocateMemory entity staging");
    check(vkBindBufferMemory(device_.handle(), staging, staging_memory, 0),
          "vkBindBufferMemory entity staging");
    void* mapped = nullptr;
    check(vkMapMemory(device_.handle(), staging_memory, 0, VK_WHOLE_SIZE, 0, &mapped),
          "vkMapMemory entity staging");
    std::memcpy(mapped, texture.rgba.data(), bytes);
    vkUnmapMemory(device_.handle(), staging_memory);

    VkCommandBufferAllocateInfo command_allocate{};
    command_allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    command_allocate.commandPool = pool_;
    command_allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_allocate.commandBufferCount = 1;
    VkCommandBuffer command = VK_NULL_HANDLE;
    check(vkAllocateCommandBuffers(device_.handle(), &command_allocate, &command),
          "vkAllocateCommandBuffers entity");
    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(command, &begin_info), "vkBeginCommandBuffer entity");

    const auto barrier = [&](VkImageLayout from, VkImageLayout to, VkPipelineStageFlags2 src_stage,
                             VkAccessFlags2 src_access, VkPipelineStageFlags2 dst_stage,
                             VkAccessFlags2 dst_access) {
        VkImageMemoryBarrier2 image_barrier{};
        image_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        image_barrier.srcStageMask = src_stage;
        image_barrier.srcAccessMask = src_access;
        image_barrier.dstStageMask = dst_stage;
        image_barrier.dstAccessMask = dst_access;
        image_barrier.oldLayout = from;
        image_barrier.newLayout = to;
        image_barrier.image = slot.image;
        image_barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkDependencyInfo dependency{};
        dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependency.imageMemoryBarrierCount = 1;
        dependency.pImageMemoryBarriers = &image_barrier;
        vkCmdPipelineBarrier2(command, &dependency);
    };
    barrier(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_2_COPY_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {texture.width, texture.height, 1};
    vkCmdCopyBufferToImage(command, staging, slot.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                           &region);
    barrier(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    check(vkEndCommandBuffer(command), "vkEndCommandBuffer entity");

    VkCommandBufferSubmitInfo command_submit{};
    command_submit.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    command_submit.commandBuffer = command;
    VkSubmitInfo2 submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &command_submit;
    check(vkQueueSubmit2(device_.graphics_queue(), 1, &submit, VK_NULL_HANDLE),
          "vkQueueSubmit2 entity");
    check(vkQueueWaitIdle(device_.graphics_queue()), "vkQueueWaitIdle entity");
    vkFreeCommandBuffers(device_.handle(), pool_, 1, &command);
    vkDestroyBuffer(device_.handle(), staging, nullptr);
    vkFreeMemory(device_.handle(), staging_memory, nullptr);

    VkImageViewCreateInfo view_create{};
    view_create.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_create.image = slot.image;
    view_create.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_create.format = VK_FORMAT_R8G8B8A8_UNORM;
    view_create.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    check(vkCreateImageView(device_.handle(), &view_create, nullptr, &slot.view),
          "vkCreateImageView entity");

    VkDescriptorSetAllocateInfo set_allocate{};
    set_allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    set_allocate.descriptorPool = descriptor_pool_;
    set_allocate.descriptorSetCount = 1;
    set_allocate.pSetLayouts = &set_layout_;
    check(vkAllocateDescriptorSets(device_.handle(), &set_allocate, &slot.set),
          "vkAllocateDescriptorSets entity");
    const VkDescriptorImageInfo image{sampler_, slot.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = slot.set;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &image;
    vkUpdateDescriptorSets(device_.handle(), 1, &write, 0, nullptr);
    slots_.emplace(name, slot);
}

void EntityRenderer::forget_texture(std::string_view name) {
    const auto found = slots_.find(name);
    if (found == slots_.end()) return;
    retired_.push_back({found->second, frame_ + kFrames});
    slots_.erase(found);
}

void EntityRenderer::set_font(const entity::Font& font) {
    font_copy_ = font;
    font_ = &font_copy_;
    upload_texture(kFont, font.sheet);
}

void EntityRenderer::begin() {
    ++frame_;
    queued_ = 0;
    draws_.clear();
    std::erase_if(retired_, [&](Retired& retired) {
        if (retired.after > frame_) return false;
        destroy_slot(retired.slot);
        return true;
    });
}

void EntityRenderer::add(std::span<const entity::Vertex> vertices, const Draw& draw) {
    if (vertices.empty() || queued_ + vertices.size() > kVerticesPerFrame) return;
    const size_t base = (frame_ % kFrames) * kVerticesPerFrame;
    std::memcpy(mapped_ + base + queued_, vertices.data(), vertices.size_bytes());
    draws_.push_back({static_cast<uint32_t>(base + queued_),
                      static_cast<uint32_t>(vertices.size()), draw});
    queued_ += vertices.size();
}

void EntityRenderer::add_tag(std::string_view text, const entity::Matrix& place,
                             bool through_walls, float sky_light, float block_light) {
    if (font_ == nullptr || text.empty()) return;
    const auto corner = [&](float x, float y, float u, float v) {
        entity::Vertex vertex{};
        const float in[4] = {x, y, 0.0f, 1.0f};
        for (int row = 0; row < 3; ++row) {
            vertex.position[row] = place[row] * in[0] + place[4 + row] * in[1] +
                                   place[8 + row] * in[2] + place[12 + row];
        }
        vertex.uv[0] = u;
        vertex.uv[1] = v;
        return vertex;
    };
    const auto quad = [&](std::vector<entity::Vertex>& out, float x0, float y0, float x1, float y1,
                          float u0, float v0, float u1, float v1) {
        const entity::Vertex a = corner(x0, y0, u0, v0);
        const entity::Vertex b = corner(x1, y0, u1, v0);
        const entity::Vertex c = corner(x1, y1, u1, v1);
        const entity::Vertex d = corner(x0, y1, u0, v1);
        for (const auto& v : {a, b, c, a, c, d}) out.push_back(v);
    };

    const float width = font_->width(text);
    const float left = -width / 2.0f;
    std::vector<entity::Vertex> glyphs;
    float pen = left;
    const float sheet_w = static_cast<float>(font_->sheet.width);
    const float sheet_h = static_cast<float>(font_->sheet.height);
    const auto cell_w = static_cast<float>(font_->cell_width);
    const auto cell_h = static_cast<float>(font_->cell_height);
    for (const char c : text) {
        const auto code = static_cast<uint8_t>(c);
        const float u = static_cast<float>(code % 16) * cell_w;
        const float v = static_cast<float>(code / 16) * cell_h;
        quad(glyphs, pen, 0.0f, pen + cell_w, cell_h, u / sheet_w, v / sheet_h,
             (u + cell_w) / sheet_w, (v + cell_h) / sheet_h);
        pen += font_->advance[code];
    }

    Draw draw;
    draw.lit = false;
    draw.sky_light = sky_light;
    draw.block_light = block_light;
    if (through_walls) {
        std::vector<entity::Vertex> back;
        quad(back, left - 1.0f, -1.0f, left + width, cell_h, 0, 0, 1, 1);
        draw.texture = kWhite;
        draw.layer = Layer::TagThrough;
        draw.colour = {0.0f, 0.0f, 0.0f, 0.25f};
        add(back, draw);
        draw.texture = kFont;
        draw.colour = {1.0f, 1.0f, 1.0f, 32.0f / 255.0f};
        add(glyphs, draw);
    }
    draw.texture = kFont;
    draw.layer = Layer::Tag;
    draw.colour = {1.0f, 1.0f, 1.0f, 1.0f};
    add(glyphs, draw);
}

void EntityRenderer::record(VkCommandBuffer command, const float view_projection[16],
                            uint32_t width, uint32_t height, const float fog[3], float fog_start,
                            float fog_end) const {
    if (draws_.empty() || width == 0 || height == 0) return;
    const VkViewport viewport{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height),
                              0.0f, 1.0f};
    const VkRect2D scissor{{0, 0}, {width, height}};
    vkCmdSetViewport(command, 0, 1, &viewport);
    vkCmdSetScissor(command, 0, 1, &scissor);
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(command, 0, 1, &vertices_, &offset);

    EntityPush push{};
    std::memcpy(push.view_projection, view_projection, sizeof(push.view_projection));
    push.fog[0] = fog[0];
    push.fog[1] = fog[1];
    push.fog[2] = fog[2];
    push.fog[3] = fog_end;
    push.params[0] = fog_start;

    // Solid things first, then what is blended over them, then the name tags,
    // the faint see-through half before the solid half.
    for (const Layer layer : {Layer::Cutout, Layer::CutoutNoCull, Layer::Shadow,
                              Layer::Translucent, Layer::TagThrough, Layer::Tag}) {
        bool bound = false;
        VkDescriptorSet last = VK_NULL_HANDLE;
        for (const auto& queued : draws_) {
            if (queued.draw.layer != layer) continue;
            const auto slot = slots_.find(queued.draw.texture);
            if (slot == slots_.end()) continue;
            if (!bound) {
                vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  pipelines_[static_cast<size_t>(layer)]);
                bound = true;
                last = VK_NULL_HANDLE;
            }
            if (slot->second.set != last) {
                vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1,
                                        &slot->second.set, 0, nullptr);
                last = slot->second.set;
            }
            push.params[1] = queued.draw.sky_light;
            push.params[2] = queued.draw.block_light;
            push.params[3] = queued.draw.lit ? 1.0f : 0.0f;
            std::memcpy(push.overlay, queued.draw.overlay.data(), sizeof(push.overlay));
            std::memcpy(push.colour, queued.draw.colour.data(), sizeof(push.colour));
            vkCmdPushConstants(command, layout_,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                               sizeof(push), &push);
            vkCmdDraw(command, queued.count, 1, queued.first, 0);
        }
    }
}

void EntityRenderer::create_pipelines(VkFormat colour_format, VkFormat depth_format) {
    VkSamplerCreateInfo sampler{};
    sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler.magFilter = VK_FILTER_NEAREST;
    sampler.minFilter = VK_FILTER_NEAREST;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    check(vkCreateSampler(device_.handle(), &sampler, nullptr, &sampler_),
          "vkCreateSampler entity");

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo set_layout{};
    set_layout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    set_layout.bindingCount = 1;
    set_layout.pBindings = &binding;
    check(vkCreateDescriptorSetLayout(device_.handle(), &set_layout, nullptr, &set_layout_),
          "vkCreateDescriptorSetLayout entity");

    // Enough for every texture under textures/entity/ and a full server's skins.
    constexpr uint32_t kSets = 2048;
    VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kSets};
    VkDescriptorPoolCreateInfo pool_create{};
    pool_create.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_create.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_create.maxSets = kSets;
    pool_create.poolSizeCount = 1;
    pool_create.pPoolSizes = &size;
    check(vkCreateDescriptorPool(device_.handle(), &pool_create, nullptr, &descriptor_pool_),
          "vkCreateDescriptorPool entity");

    VkPushConstantRange push{};
    push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    push.size = sizeof(EntityPush);
    VkPipelineLayoutCreateInfo layout{};
    layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout.setLayoutCount = 1;
    layout.pSetLayouts = &set_layout_;
    layout.pushConstantRangeCount = 1;
    layout.pPushConstantRanges = &push;
    check(vkCreatePipelineLayout(device_.handle(), &layout, nullptr, &layout_),
          "vkCreatePipelineLayout entity");

    VkShaderModule vertex = make_module(device_.handle(), shaders::EntityVert());
    VkShaderModule fragment = make_module(device_.handle(), shaders::EntityFrag());
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertex;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragment;
    stages[1].pName = "main";

    VkVertexInputBindingDescription vertex_binding{0, sizeof(entity::Vertex),
                                                   VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attributes[3]{};
    attributes[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(entity::Vertex, position)};
    attributes[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(entity::Vertex, uv)};
    attributes[2] = {2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(entity::Vertex, normal)};
    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input.vertexBindingDescriptionCount = 1;
    vertex_input.pVertexBindingDescriptions = &vertex_binding;
    vertex_input.vertexAttributeDescriptionCount = 3;
    vertex_input.pVertexAttributeDescriptions = attributes;

    VkPipelineInputAssemblyStateCreateInfo assembly{};
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{};
    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamic_states;
    VkFormat format = colour_format;
    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &format;
    rendering.depthAttachmentFormat = depth_format;

    struct Variant {
        bool cull;
        bool blend;
        bool depth_test;
        bool depth_write;
    };
    // In Layer's order.
    const Variant variants[6] = {
        {true, false, true, true},     // cutout
        {false, false, true, true},    // cutout, both sides
        {false, true, true, true},     // translucent
        {false, true, true, false},    // shadow
        {false, true, false, false},   // name tag through walls
        {false, true, true, false},    // name tag
    };
    VkResult result = VK_SUCCESS;
    for (size_t i = 0; i < 6 && result == VK_SUCCESS; ++i) {
        const Variant& v = variants[i];
        VkPipelineRasterizationStateCreateInfo raster{};
        raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = v.cull ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
        raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
        raster.lineWidth = 1.0f;

        VkPipelineDepthStencilStateCreateInfo depth{};
        depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depth.depthTestEnable = v.depth_test ? VK_TRUE : VK_FALSE;
        depth.depthWriteEnable = v.depth_write ? VK_TRUE : VK_FALSE;
        depth.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
        depth.maxDepthBounds = 1.0f;

        VkPipelineColorBlendAttachmentState blend_attachment{};
        blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        if (v.blend) {
            blend_attachment.blendEnable = VK_TRUE;
            blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
            blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
        }
        VkPipelineColorBlendStateCreateInfo blend{};
        blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        blend.attachmentCount = 1;
        blend.pAttachments = &blend_attachment;

        VkGraphicsPipelineCreateInfo create{};
        create.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        create.pNext = &rendering;
        create.stageCount = 2;
        create.pStages = stages;
        create.pVertexInputState = &vertex_input;
        create.pInputAssemblyState = &assembly;
        create.pViewportState = &viewport;
        create.pRasterizationState = &raster;
        create.pMultisampleState = &multisample;
        create.pDepthStencilState = &depth;
        create.pColorBlendState = &blend;
        create.pDynamicState = &dynamic;
        create.layout = layout_;
        result = vkCreateGraphicsPipelines(device_.handle(), device_.pipeline_cache(), 1, &create,
                                           nullptr, &pipelines_[i]);
    }
    vkDestroyShaderModule(device_.handle(), vertex, nullptr);
    vkDestroyShaderModule(device_.handle(), fragment, nullptr);
    check(result, "vkCreateGraphicsPipelines entity");
}

void EntityRenderer::destroy_slot(Slot& slot) {
    if (slot.set != VK_NULL_HANDLE) {
        vkFreeDescriptorSets(device_.handle(), descriptor_pool_, 1, &slot.set);
    }
    vkDestroyImageView(device_.handle(), slot.view, nullptr);
    vkDestroyImage(device_.handle(), slot.image, nullptr);
    vkFreeMemory(device_.handle(), slot.memory, nullptr);
    slot = Slot{};
}

void EntityRenderer::destroy() {
    for (auto& [name, slot] : slots_) destroy_slot(slot);
    slots_.clear();
    for (auto& retired : retired_) destroy_slot(retired.slot);
    retired_.clear();
    for (VkPipeline pipeline : pipelines_) vkDestroyPipeline(device_.handle(), pipeline, nullptr);
    pipelines_ = {};
    vkDestroyPipelineLayout(device_.handle(), layout_, nullptr);
    vkDestroyDescriptorPool(device_.handle(), descriptor_pool_, nullptr);
    vkDestroyDescriptorSetLayout(device_.handle(), set_layout_, nullptr);
    vkDestroySampler(device_.handle(), sampler_, nullptr);
    if (mapped_ != nullptr) vkUnmapMemory(device_.handle(), vertex_memory_);
    mapped_ = nullptr;
    vkDestroyBuffer(device_.handle(), vertices_, nullptr);
    vkFreeMemory(device_.handle(), vertex_memory_, nullptr);
    vkDestroyCommandPool(device_.handle(), pool_, nullptr);
}

} // namespace mcpp::gpu
