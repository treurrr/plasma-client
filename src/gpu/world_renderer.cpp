#include "mcpp/gpu/world_renderer.hpp"

#include "mcpp/byte_buffer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <format>

#include <mcpp/gpu/shaders/WorldFrag.hpp>
#include <mcpp/gpu/shaders/WorldVert.hpp>

namespace mcpp::gpu {
namespace {

constexpr VkFormat kColorFormat = VK_FORMAT_R8G8B8A8_SRGB;
constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;

void check(VkResult result, std::string_view what) {
    if (result != VK_SUCCESS) {
        throw ProtocolError(std::format("{} faalde met VkResult {}", what,
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
    throw ProtocolError("geen geschikt geheugentype op deze gpu");
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

struct Mat4 {
    float m[16];
};

struct PushConstants {
    Mat4 view_projection;
};

void normalise(float v[3]) {
    const float length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (length > 0.0f) {
        v[0] /= length;
        v[1] /= length;
        v[2] /= length;
    }
}

void cross(const float a[3], const float b[3], float out[3]) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

float dot(const float a[3], const float b[3]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

// Column-major, Vulkan clip space: y points down. Depth is reversed - near maps
// to 1 and far to 0 - which pairs with a depth buffer cleared to 0 and a GREATER
// test, and keeps float precision where it is needed.
Mat4 view_projection(const Camera& camera, float aspect) {
    float forward[3] = {camera.target[0] - camera.eye[0], camera.target[1] - camera.eye[1],
                        camera.target[2] - camera.eye[2]};
    normalise(forward);

    const float world_up[3] = {0.0f, 1.0f, 0.0f};
    float right[3];
    cross(forward, world_up, right);
    normalise(right);
    float up[3];
    cross(right, forward, up);

    const float f = 1.0f / std::tan(camera.fov_degrees * 3.14159265f / 360.0f);
    const float a = camera.near_plane;
    const float b = camera.far_plane;

    const float view[16] = {
        right[0],              up[0],              -forward[0],            0.0f,
        right[1],              up[1],              -forward[1],            0.0f,
        right[2],              up[2],              -forward[2],            0.0f,
        -dot(right, camera.eye), -dot(up, camera.eye), dot(forward, camera.eye), 1.0f,
    };
    const float projection[16] = {
        f / aspect, 0.0f,  0.0f,            0.0f,
        0.0f,       -f,    0.0f,            0.0f,
        0.0f,       0.0f,  a / (b - a),    -1.0f,
        0.0f,       0.0f,  a * b / (b - a), 0.0f,
    };

    Mat4 out{};
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) sum += projection[k * 4 + row] * view[column * 4 + k];
            out.m[column * 4 + row] = sum;
        }
    }
    return out;
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

} // namespace

WorldRenderer::WorldRenderer(Device& device, uint32_t width, uint32_t height,
                             const world::Atlas& atlas)
    : device_(device), width_(width), height_(height) {
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(device_.physical(), &properties);
    timestamp_period_ = properties.limits.timestampPeriod;

    VkCommandPoolCreateInfo pool{};
    pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool.queueFamilyIndex = device_.graphics_family();
    check(vkCreateCommandPool(device_.handle(), &pool, nullptr, &pool_), "vkCreateCommandPool");

    VkQueryPoolCreateInfo query{};
    query.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    query.queryType = VK_QUERY_TYPE_TIMESTAMP;
    query.queryCount = 2;
    check(vkCreateQueryPool(device_.handle(), &query, nullptr, &timestamps_), "vkCreateQueryPool");

    create_targets();
    create_atlas(atlas);
    create_pipeline();
}

void WorldRenderer::create_atlas(const world::Atlas& atlas) {
    atlas_tile_ = atlas.tile_size();
    atlas_layers_ = atlas.layers();

    VkPhysicalDeviceProperties limits{};
    vkGetPhysicalDeviceProperties(device_.physical(), &limits);
    if (atlas_layers_ > limits.limits.maxImageArrayLayers) {
        throw ProtocolError(std::format("atlas heeft {} lagen, deze gpu kan er {}",
                                        atlas_layers_, limits.limits.maxImageArrayLayers));
    }

    VkImageCreateInfo create{};
    create.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    create.imageType = VK_IMAGE_TYPE_2D;
    create.format = VK_FORMAT_R8G8B8A8_SRGB;
    create.extent = {atlas_tile_, atlas_tile_, 1};
    create.mipLevels = atlas.mip_levels();
    create.arrayLayers = atlas_layers_;
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

    auto staging = make_buffer(device_, atlas.pixels().size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    void* mapped = nullptr;
    check(vkMapMemory(device_.handle(), staging.memory, 0, VK_WHOLE_SIZE, 0, &mapped),
          "vkMapMemory atlas");
    std::memcpy(mapped, atlas.pixels().data(), atlas.pixels().size());
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

    const uint32_t mips = atlas.mip_levels();
    transition(command, atlas_image_, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
               VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, mips, atlas_layers_);

    std::vector<VkBufferImageCopy> regions(mips);
    for (uint32_t mip = 0; mip < mips; ++mip) {
        regions[mip] = {};
        regions[mip].bufferOffset = atlas.level_offset(mip);
        regions[mip].imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip, 0, atlas_layers_};
        regions[mip].imageExtent = {atlas_tile_ >> mip, atlas_tile_ >> mip, 1};
    }
    vkCmdCopyBufferToImage(command, staging.buffer, atlas_image_,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           static_cast<uint32_t>(regions.size()), regions.data());

    transition(command, atlas_image_, VK_IMAGE_ASPECT_COLOR_BIT,
               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
               VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
               VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, mips,
               atlas_layers_);

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
    view.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    view.format = VK_FORMAT_R8G8B8A8_SRGB;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mips, 0, atlas_layers_};
    check(vkCreateImageView(device_.handle(), &view, nullptr, &atlas_view_),
          "vkCreateImageView atlas");

    // Nearest magnification keeps the blocky look up close; minification needs
    // mips and anisotropy or distant surfaces crawl with aliasing.
    VkSamplerCreateInfo sampler{};
    sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler.magFilter = VK_FILTER_NEAREST;
    sampler.minFilter = VK_FILTER_LINEAR;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler.maxLod = static_cast<float>(mips);
    if (device_.adapter().capabilities.sampler_anisotropy) {
        sampler.anisotropyEnable = VK_TRUE;
        sampler.maxAnisotropy = std::min(16.0f, device_.adapter().capabilities.max_anisotropy);
    }
    // Layers cannot bleed into one another, so uvs outside 0..1 can repeat the
    // way some block models expect.
    sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
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

void WorldRenderer::create_targets() {
    const auto make_image = [&](VkFormat format, VkImageUsageFlags usage, VkImage& image,
                                VkDeviceMemory& memory, VkImageView& view,
                                VkImageAspectFlags aspect) {
        VkImageCreateInfo create{};
        create.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        create.imageType = VK_IMAGE_TYPE_2D;
        create.format = format;
        create.extent = {width_, height_, 1};
        create.mipLevels = 1;
        create.arrayLayers = 1;
        create.samples = VK_SAMPLE_COUNT_1_BIT;
        create.tiling = VK_IMAGE_TILING_OPTIMAL;
        create.usage = usage;
        create.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        check(vkCreateImage(device_.handle(), &create, nullptr, &image), "vkCreateImage");

        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(device_.handle(), image, &requirements);
        VkMemoryAllocateInfo allocate{};
        allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocate.allocationSize = requirements.size;
        allocate.memoryTypeIndex = memory_type(device_.physical(), requirements.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        check(vkAllocateMemory(device_.handle(), &allocate, nullptr, &memory), "vkAllocateMemory");
        check(vkBindImageMemory(device_.handle(), image, memory, 0), "vkBindImageMemory");

        VkImageViewCreateInfo view_create{};
        view_create.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_create.image = image;
        view_create.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_create.format = format;
        view_create.subresourceRange = {aspect, 0, 1, 0, 1};
        check(vkCreateImageView(device_.handle(), &view_create, nullptr, &view),
              "vkCreateImageView");
    };

    make_image(kColorFormat,
               VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, color_,
               color_memory_, color_view_, VK_IMAGE_ASPECT_COLOR_BIT);
    make_image(kDepthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, depth_, depth_memory_,
               depth_view_, VK_IMAGE_ASPECT_DEPTH_BIT);

    auto readback = make_buffer(device_, static_cast<VkDeviceSize>(width_) * height_ * 4,
                                VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    readback_ = readback.buffer;
    readback_memory_ = readback.memory;
}

void WorldRenderer::create_pipeline() {
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

    VkShaderModule vertex = make_module(device_.handle(), shaders::WorldVert());
    VkShaderModule fragment = make_module(device_.handle(), shaders::WorldFrag());

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
    binding.stride = sizeof(world::Vertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attributes[4]{};
    attributes[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(world::Vertex, position)};
    attributes[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(world::Vertex, uv)};
    attributes[2] = {2, 0, VK_FORMAT_R32_UINT, offsetof(world::Vertex, texture)};
    attributes[3] = {3, 0, VK_FORMAT_R32_UINT, offsetof(world::Vertex, face)};

    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input.vertexBindingDescriptionCount = 1;
    vertex_input.pVertexBindingDescriptions = &binding;
    vertex_input.vertexAttributeDescriptionCount = 4;
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
    raster.cullMode = VK_CULL_MODE_BACK_BIT;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth.depthTestEnable = VK_TRUE;
    depth.depthWriteEnable = VK_TRUE;
    depth.depthCompareOp = VK_COMPARE_OP_GREATER;
    depth.maxDepthBounds = 1.0f;

    VkPipelineColorBlendAttachmentState blend_attachment{};
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

    VkFormat color_format = kColorFormat;
    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &color_format;
    rendering.depthAttachmentFormat = kDepthFormat;

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

void WorldRenderer::upload(const world::Mesh& mesh) {
    const auto start = std::chrono::steady_clock::now();
    destroy_mesh();

    vertex_bytes_ = mesh.vertices.size() * sizeof(world::Vertex);
    index_bytes_ = mesh.indices.size() * sizeof(uint32_t);
    index_count_ = static_cast<uint32_t>(mesh.indices.size());
    if (index_count_ == 0) return;

    auto staging = make_buffer(device_, vertex_bytes_ + index_bytes_,
                               VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    void* mapped = nullptr;
    check(vkMapMemory(device_.handle(), staging.memory, 0, VK_WHOLE_SIZE, 0, &mapped),
          "vkMapMemory");
    std::memcpy(mapped, mesh.vertices.data(), vertex_bytes_);
    std::memcpy(static_cast<uint8_t*>(mapped) + vertex_bytes_, mesh.indices.data(), index_bytes_);
    vkUnmapMemory(device_.handle(), staging.memory);

    auto vertex = make_buffer(device_, vertex_bytes_,
                              VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    auto index = make_buffer(device_, index_bytes_,
                             VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vertex_buffer_ = vertex.buffer;
    vertex_memory_ = vertex.memory;
    index_buffer_ = index.buffer;
    index_memory_ = index.memory;

    VkCommandBufferAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocate.commandPool = pool_;
    allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate.commandBufferCount = 1;
    VkCommandBuffer command = VK_NULL_HANDLE;
    check(vkAllocateCommandBuffers(device_.handle(), &allocate, &command),
          "vkAllocateCommandBuffers");

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(command, &begin), "vkBeginCommandBuffer");
    VkBufferCopy vertex_copy{0, 0, vertex_bytes_};
    vkCmdCopyBuffer(command, staging.buffer, vertex_buffer_, 1, &vertex_copy);
    VkBufferCopy index_copy{vertex_bytes_, 0, index_bytes_};
    vkCmdCopyBuffer(command, staging.buffer, index_buffer_, 1, &index_copy);
    check(vkEndCommandBuffer(command), "vkEndCommandBuffer");

    VkCommandBufferSubmitInfo command_submit{};
    command_submit.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    command_submit.commandBuffer = command;
    VkSubmitInfo2 submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &command_submit;
    check(vkQueueSubmit2(device_.graphics_queue(), 1, &submit, VK_NULL_HANDLE), "vkQueueSubmit2");
    check(vkQueueWaitIdle(device_.graphics_queue()), "vkQueueWaitIdle");

    vkFreeCommandBuffers(device_.handle(), pool_, 1, &command);
    vkDestroyBuffer(device_.handle(), staging.buffer, nullptr);
    vkFreeMemory(device_.handle(), staging.memory, nullptr);

    upload_ms_ = std::chrono::duration<double, std::milli>(
                     std::chrono::steady_clock::now() - start).count();
}

std::vector<uint8_t> WorldRenderer::render(const Camera& camera) {
    VkCommandBufferAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocate.commandPool = pool_;
    allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate.commandBufferCount = 1;
    VkCommandBuffer command = VK_NULL_HANDLE;
    check(vkAllocateCommandBuffers(device_.handle(), &allocate, &command),
          "vkAllocateCommandBuffers");

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(command, &begin), "vkBeginCommandBuffer");

    vkCmdResetQueryPool(command, timestamps_, 0, 2);
    vkCmdWriteTimestamp2(command, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, timestamps_, 0);

    transition(command, color_, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
               VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
               VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
               VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    transition(command, depth_, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
               VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
               VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
               VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

    VkRenderingAttachmentInfo color{};
    color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color.imageView = color_view_;
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue.color = {{0.45f, 0.62f, 0.86f, 1.0f}};

    VkRenderingAttachmentInfo depth_attachment{};
    depth_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depth_attachment.imageView = depth_view_;
    depth_attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attachment.clearValue.depthStencil = {0.0f, 0};

    VkRenderingInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering.renderArea = {{0, 0}, {width_, height_}};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &color;
    rendering.pDepthAttachment = &depth_attachment;

    vkCmdBeginRendering(command, &rendering);
    if (index_count_ > 0) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
        const VkViewport viewport{0.0f, 0.0f, static_cast<float>(width_),
                                  static_cast<float>(height_), 0.0f, 1.0f};
        const VkRect2D scissor{{0, 0}, {width_, height_}};
        vkCmdSetViewport(command, 0, 1, &viewport);
        vkCmdSetScissor(command, 0, 1, &scissor);

        PushConstants push{};
        push.view_projection = view_projection(camera, static_cast<float>(width_) /
                                                           static_cast<float>(height_));
        vkCmdPushConstants(command, layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1,
                                &descriptor_set_, 0, nullptr);

        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(command, 0, 1, &vertex_buffer_, &offset);
        vkCmdBindIndexBuffer(command, index_buffer_, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(command, index_count_, 1, 0, 0, 0);
    }
    vkCmdEndRendering(command);

    transition(command, color_, VK_IMAGE_ASPECT_COLOR_BIT,
               VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
               VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
               VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT,
               VK_ACCESS_2_TRANSFER_READ_BIT);

    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {width_, height_, 1};
    vkCmdCopyImageToBuffer(command, color_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback_, 1,
                           &region);

    vkCmdWriteTimestamp2(command, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, timestamps_, 1);
    check(vkEndCommandBuffer(command), "vkEndCommandBuffer");

    VkFenceCreateInfo fence_create{};
    fence_create.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence = VK_NULL_HANDLE;
    check(vkCreateFence(device_.handle(), &fence_create, nullptr, &fence), "vkCreateFence");

    VkCommandBufferSubmitInfo command_submit{};
    command_submit.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    command_submit.commandBuffer = command;
    VkSubmitInfo2 submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &command_submit;
    check(vkQueueSubmit2(device_.graphics_queue(), 1, &submit, fence), "vkQueueSubmit2");
    check(vkWaitForFences(device_.handle(), 1, &fence, VK_TRUE, UINT64_MAX), "vkWaitForFences");

    uint64_t marks[2] = {0, 0};
    if (vkGetQueryPoolResults(device_.handle(), timestamps_, 0, 2, sizeof(marks), marks,
                              sizeof(uint64_t),
                              VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT) == VK_SUCCESS) {
        last_gpu_ms_ = static_cast<double>(marks[1] - marks[0]) * timestamp_period_ / 1e6;
    }

    std::vector<uint8_t> pixels(static_cast<size_t>(width_) * height_ * 4);
    void* mapped = nullptr;
    check(vkMapMemory(device_.handle(), readback_memory_, 0, VK_WHOLE_SIZE, 0, &mapped),
          "vkMapMemory");
    std::memcpy(pixels.data(), mapped, pixels.size());
    vkUnmapMemory(device_.handle(), readback_memory_);

    vkDestroyFence(device_.handle(), fence, nullptr);
    vkFreeCommandBuffers(device_.handle(), pool_, 1, &command);
    return pixels;
}

void WorldRenderer::destroy_mesh() {
    if (vertex_buffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_.handle(), vertex_buffer_, nullptr);
        vkFreeMemory(device_.handle(), vertex_memory_, nullptr);
        vertex_buffer_ = VK_NULL_HANDLE;
    }
    if (index_buffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_.handle(), index_buffer_, nullptr);
        vkFreeMemory(device_.handle(), index_memory_, nullptr);
        index_buffer_ = VK_NULL_HANDLE;
    }
}

WorldRenderer::~WorldRenderer() {
    if (device_.handle() == VK_NULL_HANDLE) return;
    vkDeviceWaitIdle(device_.handle());
    destroy_mesh();
    vkDestroyPipeline(device_.handle(), pipeline_, nullptr);
    vkDestroyPipelineLayout(device_.handle(), layout_, nullptr);
    vkDestroyDescriptorPool(device_.handle(), descriptor_pool_, nullptr);
    vkDestroyDescriptorSetLayout(device_.handle(), set_layout_, nullptr);
    vkDestroySampler(device_.handle(), sampler_, nullptr);
    vkDestroyImageView(device_.handle(), atlas_view_, nullptr);
    vkDestroyImage(device_.handle(), atlas_image_, nullptr);
    vkFreeMemory(device_.handle(), atlas_memory_, nullptr);
    vkDestroyBuffer(device_.handle(), readback_, nullptr);
    vkFreeMemory(device_.handle(), readback_memory_, nullptr);
    vkDestroyImageView(device_.handle(), depth_view_, nullptr);
    vkDestroyImage(device_.handle(), depth_, nullptr);
    vkFreeMemory(device_.handle(), depth_memory_, nullptr);
    vkDestroyImageView(device_.handle(), color_view_, nullptr);
    vkDestroyImage(device_.handle(), color_, nullptr);
    vkFreeMemory(device_.handle(), color_memory_, nullptr);
    vkDestroyQueryPool(device_.handle(), timestamps_, nullptr);
    vkDestroyCommandPool(device_.handle(), pool_, nullptr);
}

} // namespace mcpp::gpu
