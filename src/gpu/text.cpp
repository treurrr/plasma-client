#include "mcpp/gpu/text.hpp"

#include "mcpp/byte_buffer.hpp"

#include <bit>
#include <cstddef>
#include <cstring>
#include <format>
#include <fstream>
#include <vector>

#include <mcpp/gpu/shaders/TextFrag.hpp>
#include <mcpp/gpu/shaders/TextVert.hpp>

namespace mcpp::gpu {
namespace {

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

struct Allocated {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
};

Allocated make_buffer(Device& device, VkDeviceSize size, VkBufferUsageFlags usage,
                      VkMemoryPropertyFlags properties) {
    VkBufferCreateInfo create{};
    create.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    create.size = size;
    create.usage = usage;
    create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    Allocated out;
    check(vkCreateBuffer(device.handle(), &create, nullptr, &out.buffer), "vkCreateBuffer");

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device.handle(), out.buffer, &requirements);

    VkMemoryAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = memory_type(device.physical(), requirements.memoryTypeBits,
                                           properties);
    check(vkAllocateMemory(device.handle(), &allocate, nullptr, &out.memory), "vkAllocateMemory");
    check(vkBindBufferMemory(device.handle(), out.buffer, out.memory, 0), "vkBindBufferMemory");
    return out;
}

void transition(VkCommandBuffer command, VkImage image, VkImageAspectFlags aspect,
                VkImageLayout from, VkImageLayout to, VkPipelineStageFlags2 src_stage,
                VkAccessFlags2 src_access, VkPipelineStageFlags2 dst_stage,
                VkAccessFlags2 dst_access, uint32_t levels = 1, uint32_t layers = 1) {
    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask = src_stage;
    barrier.srcAccessMask = src_access;
    barrier.dstStageMask = dst_stage;
    barrier.dstAccessMask = dst_access;
    barrier.oldLayout = from;
    barrier.newLayout = to;
    barrier.image = image;
    barrier.subresourceRange = {aspect, 0, levels, 0, layers};

    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(command, &dependency);
}

struct PushConstants {
    float inverse_size[2];
};

VkShaderModule make_module(VkDevice device, std::span<const uint32_t> words) {
    VkShaderModuleCreateInfo create{};
    create.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    create.codeSize = words.size() * sizeof(uint32_t);
    create.pCode = words.data();
    VkShaderModule module = VK_NULL_HANDLE;
    check(vkCreateShaderModule(device, &create, nullptr, &module), "vkCreateShaderModule");
    return module;
}

} // namespace

TextRenderer::TextRenderer(Device& device, const std::filesystem::path& font,
                           VkFormat colour_format)
    : device_(device) {
    std::ifstream input(font, std::ios::binary | std::ios::ate);
    if (!input) throw ProtocolError(std::format("cannot open font {}", font.string()));
    const auto size = input.tellg();
    if (size < 32) throw ProtocolError("truncated font header");
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), size)) {
        throw ProtocolError("cannot read font");
    }
    const auto read32 = [&](size_t at) {
        return uint32_t{bytes[at]} | (uint32_t{bytes[at + 1]} << 8) |
               (uint32_t{bytes[at + 2]} << 16) | (uint32_t{bytes[at + 3]} << 24);
    };
    if (std::memcmp(bytes.data(), "PLFT", 4) != 0 || read32(4) != 1 || read32(20) != 95) {
        throw ProtocolError("unsupported font format");
    }
    line_height_ = read32(12);
    atlas_width_ = read32(24);
    atlas_height_ = read32(28);
    constexpr size_t bitmap = 32 + 95 * 14;
    const uint64_t pixels = uint64_t{atlas_width_} * atlas_height_;
    if (read32(8) == 0 || line_height_ == 0 || atlas_width_ == 0 || atlas_height_ == 0 ||
        bytes.size() < bitmap || pixels != bytes.size() - bitmap) {
        throw ProtocolError("invalid font dimensions");
    }
    const auto read16 = [&](size_t at) {
        return std::bit_cast<int16_t>(static_cast<uint16_t>(
            uint16_t{bytes[at]} | (uint16_t{bytes[at + 1]} << 8)));
    };
    for (size_t i = 0; i < glyphs_.size(); ++i) {
        const size_t at = 32 + i * 14;
        auto& glyph = glyphs_[i];
        glyph = {read16(at), read16(at + 2), read16(at + 4), read16(at + 6),
                 read16(at + 8), read16(at + 10), read16(at + 12)};
        if (glyph.x < 0 || glyph.y < 0 || glyph.w < 0 || glyph.h < 0 ||
            static_cast<uint32_t>(glyph.x + glyph.w) > atlas_width_ ||
            static_cast<uint32_t>(glyph.y + glyph.h) > atlas_height_) {
            throw ProtocolError("font glyph outside atlas");
        }
    }

    try {
        VkCommandPoolCreateInfo pool{};
        pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool.queueFamilyIndex = device_.graphics_family();
        check(vkCreateCommandPool(device_.handle(), &pool, nullptr, &pool_), "vkCreateCommandPool");
        create_atlas(std::span<const uint8_t>(bytes).subspan(bitmap));
        create_pipeline(colour_format);
        auto vertices = make_buffer(device_, kVertexCapacity * sizeof(Vertex),
                                    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vertex_buffer_ = vertices.buffer;
        vertex_memory_ = vertices.memory;
        void* mapped = nullptr;
        check(vkMapMemory(device_.handle(), vertex_memory_, 0, VK_WHOLE_SIZE, 0, &mapped),
              "vkMapMemory vertices");
        mapped_ = static_cast<Vertex*>(mapped);
    } catch (...) {
        destroy();
        throw;
    }
}

void TextRenderer::create_atlas(std::span<const uint8_t> pixels) {
    VkImageCreateInfo create{};
    create.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    create.imageType = VK_IMAGE_TYPE_2D;
    create.format = VK_FORMAT_R8_UNORM;
    create.extent = {atlas_width_, atlas_height_, 1};
    create.mipLevels = 1;
    create.arrayLayers = 1;
    create.samples = VK_SAMPLE_COUNT_1_BIT;
    create.tiling = VK_IMAGE_TILING_OPTIMAL;
    create.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    create.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    check(vkCreateImage(device_.handle(), &create, nullptr, &atlas_image_), "vkCreateImage atlas");

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device_.handle(), atlas_image_, &requirements);
    VkMemoryAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = memory_type(device_.physical(), requirements.memoryTypeBits,
                                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    check(vkAllocateMemory(device_.handle(), &allocate, nullptr, &atlas_memory_),
          "vkAllocateMemory atlas");
    check(vkBindImageMemory(device_.handle(), atlas_image_, atlas_memory_, 0),
          "vkBindImageMemory atlas");

    auto staging = make_buffer(device_, pixels.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    void* mapped = nullptr;
    check(vkMapMemory(device_.handle(), staging.memory, 0, VK_WHOLE_SIZE, 0, &mapped),
          "vkMapMemory atlas");
    std::memcpy(mapped, pixels.data(), pixels.size());
    vkUnmapMemory(device_.handle(), staging.memory);

    VkCommandBufferAllocateInfo command_allocate{};
    command_allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    command_allocate.commandPool = pool_;
    command_allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_allocate.commandBufferCount = 1;
    VkCommandBuffer command = VK_NULL_HANDLE;
    check(vkAllocateCommandBuffers(device_.handle(), &command_allocate, &command),
          "vkAllocateCommandBuffers atlas");

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(command, &begin), "vkBeginCommandBuffer atlas");

    transition(command, atlas_image_, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
               VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);

    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {atlas_width_, atlas_height_, 1};
    vkCmdCopyBufferToImage(command, staging.buffer, atlas_image_,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    transition(command, atlas_image_, VK_IMAGE_ASPECT_COLOR_BIT,
               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
               VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
               VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

    check(vkEndCommandBuffer(command), "vkEndCommandBuffer atlas");

    VkCommandBufferSubmitInfo command_submit{};
    command_submit.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    command_submit.commandBuffer = command;
    VkSubmitInfo2 submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &command_submit;
    check(vkQueueSubmit2(device_.graphics_queue(), 1, &submit, VK_NULL_HANDLE),
          "vkQueueSubmit2 atlas");
    check(vkQueueWaitIdle(device_.graphics_queue()), "vkQueueWaitIdle atlas");

    vkFreeCommandBuffers(device_.handle(), pool_, 1, &command);
    vkDestroyBuffer(device_.handle(), staging.buffer, nullptr);
    vkFreeMemory(device_.handle(), staging.memory, nullptr);

    VkImageViewCreateInfo view{};
    view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image = atlas_image_;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = VK_FORMAT_R8_UNORM;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    check(vkCreateImageView(device_.handle(), &view, nullptr, &atlas_view_),
          "vkCreateImageView atlas");

    VkSamplerCreateInfo sampler{};
    sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler.magFilter = VK_FILTER_LINEAR;
    sampler.minFilter = VK_FILTER_LINEAR;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    check(vkCreateSampler(device_.handle(), &sampler, nullptr, &sampler_), "vkCreateSampler");

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layout{};
    layout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout.bindingCount = 1;
    layout.pBindings = &binding;
    check(vkCreateDescriptorSetLayout(device_.handle(), &layout, nullptr, &set_layout_),
          "vkCreateDescriptorSetLayout");

    VkDescriptorPoolSize size{};
    size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    size.descriptorCount = 1;
    VkDescriptorPoolCreateInfo pool_create{};
    pool_create.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_create.maxSets = 1;
    pool_create.poolSizeCount = 1;
    pool_create.pPoolSizes = &size;
    check(vkCreateDescriptorPool(device_.handle(), &pool_create, nullptr, &descriptor_pool_),
          "vkCreateDescriptorPool");

    VkDescriptorSetAllocateInfo set_allocate{};
    set_allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    set_allocate.descriptorPool = descriptor_pool_;
    set_allocate.descriptorSetCount = 1;
    set_allocate.pSetLayouts = &set_layout_;
    check(vkAllocateDescriptorSets(device_.handle(), &set_allocate, &descriptor_set_),
          "vkAllocateDescriptorSets");

    VkDescriptorImageInfo image_info{};
    image_info.sampler = sampler_;
    image_info.imageView = atlas_view_;
    image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptor_set_;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &image_info;
    vkUpdateDescriptorSets(device_.handle(), 1, &write, 0, nullptr);
}

void TextRenderer::create_pipeline(VkFormat colour_format) {
    VkPushConstantRange push{};
    push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push.size = sizeof(PushConstants);

    VkPipelineLayoutCreateInfo layout{};
    layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout.setLayoutCount = 1;
    layout.pSetLayouts = &set_layout_;
    layout.pushConstantRangeCount = 1;
    layout.pPushConstantRanges = &push;
    check(vkCreatePipelineLayout(device_.handle(), &layout, nullptr, &layout_),
          "vkCreatePipelineLayout");

    VkShaderModule vertex = make_module(device_.handle(), shaders::TextVert());
    VkShaderModule fragment = make_module(device_.handle(), shaders::TextFrag());

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertex;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragment;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(Vertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attributes[3]{};
    attributes[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, position)};
    attributes[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)};
    attributes[2] = {2, 0, VK_FORMAT_R32_UINT, offsetof(Vertex, colour)};

    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input.vertexBindingDescriptionCount = 1;
    vertex_input.pVertexBindingDescriptions = &binding;
    vertex_input.vertexAttributeDescriptionCount = 3;
    vertex_input.pVertexAttributeDescriptions = attributes;

    VkPipelineInputAssemblyStateCreateInfo assembly{};
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewport{};
    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth.depthTestEnable = VK_FALSE;
    depth.depthWriteEnable = VK_FALSE;
    depth.depthCompareOp = VK_COMPARE_OP_GREATER;
    depth.maxDepthBounds = 1.0f;

    VkPipelineColorBlendAttachmentState blend_attachment{};
    blend_attachment.blendEnable = VK_TRUE;
    blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
    blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &blend_attachment;

    const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamic_states;

    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &colour_format;

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

    const VkResult result = vkCreateGraphicsPipelines(device_.handle(), device_.pipeline_cache(), 1,
                                                      &create, nullptr, &pipeline_);
    vkDestroyShaderModule(device_.handle(), vertex, nullptr);
    vkDestroyShaderModule(device_.handle(), fragment, nullptr);
    check(result, "vkCreateGraphicsPipelines");
}

void TextRenderer::begin() { vertex_count_ = 0; }

void TextRenderer::fill(float x, float y, float w, float h, uint32_t rgba) {
    if (vertex_count_ + 6 > kVertexCapacity) return;
    const float u = (static_cast<float>(atlas_width_) - 2.0f) / static_cast<float>(atlas_width_);
    const float v = (static_cast<float>(atlas_height_) - 2.0f) / static_cast<float>(atlas_height_);
    mapped_[vertex_count_++] = {{x, y}, {u, v}, rgba};
    mapped_[vertex_count_++] = {{x + w, y}, {u, v}, rgba};
    mapped_[vertex_count_++] = {{x + w, y + h}, {u, v}, rgba};
    mapped_[vertex_count_++] = {{x, y}, {u, v}, rgba};
    mapped_[vertex_count_++] = {{x + w, y + h}, {u, v}, rgba};
    mapped_[vertex_count_++] = {{x, y + h}, {u, v}, rgba};
}

void TextRenderer::draw(std::string_view text, float x, float y, float scale, uint32_t rgba) {
    for (unsigned char code : text) {
        if (code < 32 || code > 126) continue;
        if (vertex_count_ + 6 > kVertexCapacity) return;
        const auto& glyph = glyphs_[code - 32];
        const float left = x + static_cast<float>(glyph.bearing_x) * scale;
        const float top = y + static_cast<float>(glyph.bearing_y) * scale;
        const float right = left + static_cast<float>(glyph.w) * scale;
        const float bottom = top + static_cast<float>(glyph.h) * scale;
        const float u0 = static_cast<float>(glyph.x) / static_cast<float>(atlas_width_);
        const float v0 = static_cast<float>(glyph.y) / static_cast<float>(atlas_height_);
        const float u1 = static_cast<float>(glyph.x + glyph.w) / static_cast<float>(atlas_width_);
        const float v1 = static_cast<float>(glyph.y + glyph.h) / static_cast<float>(atlas_height_);
        mapped_[vertex_count_++] = {{left, top}, {u0, v0}, rgba};
        mapped_[vertex_count_++] = {{right, top}, {u1, v0}, rgba};
        mapped_[vertex_count_++] = {{right, bottom}, {u1, v1}, rgba};
        mapped_[vertex_count_++] = {{left, top}, {u0, v0}, rgba};
        mapped_[vertex_count_++] = {{right, bottom}, {u1, v1}, rgba};
        mapped_[vertex_count_++] = {{left, bottom}, {u0, v1}, rgba};
        x += static_cast<float>(glyph.advance) * scale;
    }
}

float TextRenderer::width(std::string_view text, float scale) const {
    float out = 0.0f;
    for (unsigned char code : text) {
        if (code >= 32 && code <= 126) out += static_cast<float>(glyphs_[code - 32].advance);
    }
    return out * scale;
}

float TextRenderer::line_height(float scale) const {
    return static_cast<float>(line_height_) * scale;
}

void TextRenderer::record(VkCommandBuffer command, uint32_t width, uint32_t height) {
    if (vertex_count_ == 0 || width == 0 || height == 0) return;
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    const VkViewport viewport{0.0f, 0.0f, static_cast<float>(width),
                              static_cast<float>(height), 0.0f, 1.0f};
    const VkRect2D scissor{{0, 0}, {width, height}};
    vkCmdSetViewport(command, 0, 1, &viewport);
    vkCmdSetScissor(command, 0, 1, &scissor);
    const PushConstants push{{1.0f / static_cast<float>(width),
                              1.0f / static_cast<float>(height)}};
    vkCmdPushConstants(command, layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1,
                            &descriptor_set_, 0, nullptr);
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(command, 0, 1, &vertex_buffer_, &offset);
    vkCmdDraw(command, vertex_count_, 1, 0, 0);
}

std::filesystem::path TextRenderer::default_font_path() {
    std::error_code error;
    const auto executable = std::filesystem::read_symlink("/proc/self/exe", error);
    if (!error) {
        const auto path = executable.parent_path() / "assets" / "font-32.plft";
        if (std::filesystem::exists(path)) return path;
    }
    return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() /
           "assets" / "font-32.plft";
}

TextRenderer::~TextRenderer() {
    if (device_.handle() == VK_NULL_HANDLE) return;
    vkDeviceWaitIdle(device_.handle());
    destroy();
}

void TextRenderer::destroy() {
    if (mapped_ != nullptr) vkUnmapMemory(device_.handle(), vertex_memory_);
    vkDestroyBuffer(device_.handle(), vertex_buffer_, nullptr);
    vkFreeMemory(device_.handle(), vertex_memory_, nullptr);
    vkDestroyPipeline(device_.handle(), pipeline_, nullptr);
    vkDestroyPipelineLayout(device_.handle(), layout_, nullptr);
    vkDestroyDescriptorPool(device_.handle(), descriptor_pool_, nullptr);
    vkDestroyDescriptorSetLayout(device_.handle(), set_layout_, nullptr);
    vkDestroySampler(device_.handle(), sampler_, nullptr);
    vkDestroyImageView(device_.handle(), atlas_view_, nullptr);
    vkDestroyImage(device_.handle(), atlas_image_, nullptr);
    vkFreeMemory(device_.handle(), atlas_memory_, nullptr);
    vkDestroyCommandPool(device_.handle(), pool_, nullptr);
}

} // namespace mcpp::gpu
