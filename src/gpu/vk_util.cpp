#include "vk_util.hpp"

#include "mcpp/byte_buffer.hpp"

#include <cstring>
#include <format>

namespace mcpp::gpu::vk {

void check(VkResult result, std::string_view what) {
    if (result != VK_SUCCESS) {
        throw ProtocolError(std::format("{} failed with VkResult {}", what, static_cast<int>(result)));
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

void Buffer::destroy(VkDevice device) {
    if (buffer != VK_NULL_HANDLE) vkDestroyBuffer(device, buffer, nullptr);
    if (memory != VK_NULL_HANDLE) vkFreeMemory(device, memory, nullptr);
    *this = {};
}

Buffer mapped_buffer(const Device& device, VkDeviceSize size, VkBufferUsageFlags usage) {
    Buffer out;
    out.size = size;
    VkBufferCreateInfo create{};
    create.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    create.size = size;
    create.usage = usage;
    check(vkCreateBuffer(device.handle(), &create, nullptr, &out.buffer), "vkCreateBuffer");
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device.handle(), out.buffer, &requirements);
    VkMemoryAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex =
        memory_type(device.physical(), requirements.memoryTypeBits,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    check(vkAllocateMemory(device.handle(), &allocate, nullptr, &out.memory), "vkAllocateMemory");
    check(vkBindBufferMemory(device.handle(), out.buffer, out.memory, 0), "vkBindBufferMemory");
    check(vkMapMemory(device.handle(), out.memory, 0, VK_WHOLE_SIZE, 0, &out.mapped), "vkMapMemory");
    return out;
}

void Image::destroy(VkDevice device) {
    if (view != VK_NULL_HANDLE) vkDestroyImageView(device, view, nullptr);
    if (image != VK_NULL_HANDLE) vkDestroyImage(device, image, nullptr);
    if (memory != VK_NULL_HANDLE) vkFreeMemory(device, memory, nullptr);
    *this = {};
}

namespace {

Image create_image(const Device& device, VkFormat format, uint32_t width, uint32_t height,
                   uint32_t layers) {
    Image out;
    VkImageCreateInfo create{};
    create.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    create.imageType = VK_IMAGE_TYPE_2D;
    create.format = format;
    create.extent = {width, height, 1};
    create.mipLevels = 1;
    create.arrayLayers = layers;
    create.samples = VK_SAMPLE_COUNT_1_BIT;
    create.tiling = VK_IMAGE_TILING_OPTIMAL;
    create.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    check(vkCreateImage(device.handle(), &create, nullptr, &out.image), "vkCreateImage");
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device.handle(), out.image, &requirements);
    VkMemoryAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = memory_type(device.physical(), requirements.memoryTypeBits,
                                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    check(vkAllocateMemory(device.handle(), &allocate, nullptr, &out.memory), "vkAllocateMemory");
    check(vkBindImageMemory(device.handle(), out.image, out.memory, 0), "vkBindImageMemory");
    VkImageViewCreateInfo view{};
    view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image = out.image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    view.format = format;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, layers};
    check(vkCreateImageView(device.handle(), &view, nullptr, &out.view), "vkCreateImageView");
    return out;
}

void barrier(VkCommandBuffer command, VkImage image, uint32_t layers, VkImageLayout from,
             VkImageLayout to, VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
             VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access) {
    VkImageMemoryBarrier2 image_barrier{};
    image_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    image_barrier.srcStageMask = src_stage;
    image_barrier.srcAccessMask = src_access;
    image_barrier.dstStageMask = dst_stage;
    image_barrier.dstAccessMask = dst_access;
    image_barrier.oldLayout = from;
    image_barrier.newLayout = to;
    image_barrier.image = image;
    image_barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, layers};
    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &image_barrier;
    vkCmdPipelineBarrier2(command, &dependency);
}

} // namespace

Image sampled_array(const Device& device, VkFormat format, uint32_t width, uint32_t height,
                    uint32_t layers, std::span<const uint8_t> pixels, uint32_t bytes_per_texel) {
    Image out = create_image(device, format, width, height, layers);
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(width) * height * layers * bytes_per_texel;
    Buffer staging = mapped_buffer(device, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    std::memcpy(staging.mapped, pixels.data(), std::min<size_t>(pixels.size(), bytes));

    VkCommandPoolCreateInfo pool_create{};
    pool_create.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_create.queueFamilyIndex = device.graphics_family();
    VkCommandPool pool = VK_NULL_HANDLE;
    check(vkCreateCommandPool(device.handle(), &pool_create, nullptr, &pool), "vkCreateCommandPool");
    VkCommandBufferAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocate.commandPool = pool;
    allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate.commandBufferCount = 1;
    VkCommandBuffer command = VK_NULL_HANDLE;
    check(vkAllocateCommandBuffers(device.handle(), &allocate, &command), "vkAllocateCommandBuffers");
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(command, &begin), "vkBeginCommandBuffer");
    barrier(command, out.image, layers, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_2_COPY_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, layers};
    region.imageExtent = {width, height, 1};
    vkCmdCopyBufferToImage(command, staging.buffer, out.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                           &region);
    barrier(command, out.image, layers, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    check(vkEndCommandBuffer(command), "vkEndCommandBuffer");
    VkCommandBufferSubmitInfo command_submit{};
    command_submit.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    command_submit.commandBuffer = command;
    VkSubmitInfo2 submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &command_submit;
    check(vkQueueSubmit2(device.graphics_queue(), 1, &submit, VK_NULL_HANDLE), "vkQueueSubmit2");
    check(vkQueueWaitIdle(device.graphics_queue()), "vkQueueWaitIdle");
    vkDestroyCommandPool(device.handle(), pool, nullptr);
    staging.destroy(device.handle());
    return out;
}

Image updatable_image(const Device& device, VkFormat format, uint32_t width, uint32_t height) {
    return create_image(device, format, width, height, 1);
}

void record_image_update(VkCommandBuffer command, VkImage image, VkBuffer source,
                         VkDeviceSize offset, uint32_t width, uint32_t height, bool first) {
    barrier(command, image, 1,
            first ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_PIPELINE_STAGE_2_COPY_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT);
    VkBufferImageCopy region{};
    region.bufferOffset = offset;
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {width, height, 1};
    vkCmdCopyBufferToImage(command, source, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    barrier(command, image, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}

VkSampler sampler(VkDevice device, VkFilter filter, VkSamplerAddressMode address) {
    VkSamplerCreateInfo create{};
    create.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    create.magFilter = filter;
    create.minFilter = filter;
    create.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    create.addressModeU = address;
    create.addressModeV = address;
    create.addressModeW = address;
    create.maxLod = 0.0f;
    VkSampler out = VK_NULL_HANDLE;
    check(vkCreateSampler(device, &create, nullptr, &out), "vkCreateSampler");
    return out;
}

VkPipeline pipeline(const Device& device, const PipelineDesc& desc) {
    const auto module = [&](std::span<const uint32_t> words) {
        VkShaderModuleCreateInfo create{};
        create.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        create.codeSize = words.size() * sizeof(uint32_t);
        create.pCode = words.data();
        VkShaderModule out = VK_NULL_HANDLE;
        check(vkCreateShaderModule(device.handle(), &create, nullptr, &out), "vkCreateShaderModule");
        return out;
    };
    VkShaderModule vertex = module(desc.vertex);
    VkShaderModule fragment = module(desc.fragment);

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
    binding.stride = desc.stride;
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    if (desc.stride > 0) {
        vertex_input.vertexBindingDescriptionCount = 1;
        vertex_input.pVertexBindingDescriptions = &binding;
        vertex_input.vertexAttributeDescriptionCount = static_cast<uint32_t>(desc.attributes.size());
        vertex_input.pVertexAttributeDescriptions = desc.attributes.data();
    }

    VkPipelineInputAssemblyStateCreateInfo assembly{};
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = desc.topology;
    VkPipelineViewportStateCreateInfo viewport{};
    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = desc.cull;
    raster.frontFace = desc.front;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth.depthTestEnable = desc.depth_test ? VK_TRUE : VK_FALSE;
    depth.depthWriteEnable = desc.depth_write ? VK_TRUE : VK_FALSE;
    depth.depthCompareOp = desc.depth_compare;
    depth.maxDepthBounds = 1.0f;

    VkPipelineColorBlendAttachmentState attachment{};
    attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    if (desc.blend != Blend::None) {
        attachment.blendEnable = VK_TRUE;
        attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        attachment.colorBlendOp = VK_BLEND_OP_ADD;
        attachment.alphaBlendOp = VK_BLEND_OP_ADD;
        if (desc.blend == Blend::Translucent) {
            attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        } else {
            attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        }
    }
    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &attachment;

    const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamic_states;

    VkFormat colour_format = desc.colour_format;
    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &colour_format;
    rendering.depthAttachmentFormat = desc.depth_format;

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
    create.layout = desc.layout;
    VkPipeline out = VK_NULL_HANDLE;
    const VkResult result =
        vkCreateGraphicsPipelines(device.handle(), device.pipeline_cache(), 1, &create, nullptr, &out);
    vkDestroyShaderModule(device.handle(), vertex, nullptr);
    vkDestroyShaderModule(device.handle(), fragment, nullptr);
    check(result, "vkCreateGraphicsPipelines");
    return out;
}

} // namespace mcpp::gpu::vk
