#include "mcpp/gpu/ui.hpp"

#include "mcpp/byte_buffer.hpp"

#include <cstddef>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>
#include <format>
#include <span>

#include <mcpp/gpu/shaders/UiFrag.hpp>
#include <mcpp/gpu/shaders/UiVert.hpp>

namespace mcpp::gpu {
namespace {

bool encodes_srgb(VkFormat format) {
    return format == VK_FORMAT_R8G8B8A8_SRGB || format == VK_FORMAT_B8G8R8A8_SRGB;
}

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

struct PushConstants {
    float inverse_size[2];
    // An sRGB attachment encodes what the shader writes, so colours written as
    // sRGB bytes have to be linearised first or everything comes out washed.
    float linearise;
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

UiRenderer::UiRenderer(Device& device, VkFormat colour_format,
                       const std::filesystem::path& items, const std::filesystem::path& gui)
    : device_(device), linearise_(encodes_srgb(colour_format)) {
    try {
        create_atlas(items, gui);
        create_pipeline(colour_format);

        VkBufferCreateInfo create{};
        create.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        create.size = kCapacity * sizeof(Quad);
        create.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        check(vkCreateBuffer(device_.handle(), &create, nullptr, &buffer_), "vkCreateBuffer ui");

        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device_.handle(), buffer_, &requirements);
        VkMemoryAllocateInfo allocate{};
        allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocate.allocationSize = requirements.size;
        allocate.memoryTypeIndex = memory_type(device_.physical(), requirements.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        check(vkAllocateMemory(device_.handle(), &allocate, nullptr, &memory_),
              "vkAllocateMemory ui");
        check(vkBindBufferMemory(device_.handle(), buffer_, memory_, 0), "vkBindBufferMemory ui");

        void* mapped = nullptr;
        check(vkMapMemory(device_.handle(), memory_, 0, VK_WHOLE_SIZE, 0, &mapped),
              "vkMapMemory ui");
        mapped_ = static_cast<Quad*>(mapped);
    } catch (...) {
        destroy();
        throw;
    }
}

// One image, however many layers, uploaded and left ready to sample.
void UiRenderer::upload_texture(const std::vector<uint8_t>& pixels, uint32_t width,
                                uint32_t height, uint32_t layers, VkImage& image,
                                VkDeviceMemory& memory, VkImageView& view,
                                VkImageViewType view_type) {
    VkImageCreateInfo create{};
    create.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    create.imageType = VK_IMAGE_TYPE_2D;
    create.format = VK_FORMAT_R8G8B8A8_UNORM;
    create.extent = {width, height, 1};
    create.mipLevels = 1;
    create.arrayLayers = layers;
    create.samples = VK_SAMPLE_COUNT_1_BIT;
    create.tiling = VK_IMAGE_TILING_OPTIMAL;
    create.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    check(vkCreateImage(device_.handle(), &create, nullptr, &image), "vkCreateImage ui texture");

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device_.handle(), image, &requirements);
    VkMemoryAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = memory_type(device_.physical(), requirements.memoryTypeBits,
                                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    check(vkAllocateMemory(device_.handle(), &allocate, nullptr, &memory),
          "vkAllocateMemory ui texture");
    check(vkBindImageMemory(device_.handle(), image, memory, 0), "vkBindImageMemory ui texture");

    VkBufferCreateInfo staging_create{};
    staging_create.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    staging_create.size = pixels.size();
    staging_create.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VkBuffer staging = VK_NULL_HANDLE;
    check(vkCreateBuffer(device_.handle(), &staging_create, nullptr, &staging),
          "vkCreateBuffer ui staging");
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
          "vkAllocateMemory ui staging");
    check(vkBindBufferMemory(device_.handle(), staging, staging_memory, 0),
          "vkBindBufferMemory ui staging");
    void* mapped = nullptr;
    check(vkMapMemory(device_.handle(), staging_memory, 0, VK_WHOLE_SIZE, 0, &mapped),
          "vkMapMemory ui staging");
    std::memcpy(mapped, pixels.data(), pixels.size());
    vkUnmapMemory(device_.handle(), staging_memory);

    VkCommandBufferAllocateInfo command_allocate{};
    command_allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    command_allocate.commandPool = pool_;
    command_allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_allocate.commandBufferCount = 1;
    VkCommandBuffer command = VK_NULL_HANDLE;
    check(vkAllocateCommandBuffers(device_.handle(), &command_allocate, &command),
          "vkAllocateCommandBuffers ui");
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(command, &begin), "vkBeginCommandBuffer ui");

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, layers};
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, layers};
    region.imageExtent = {width, height, 1};
    vkCmdCopyBufferToImage(command, staging, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                           &region);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &barrier);
    check(vkEndCommandBuffer(command), "vkEndCommandBuffer ui");

    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command;
    check(vkQueueSubmit(device_.graphics_queue(), 1, &submit, VK_NULL_HANDLE),
          "vkQueueSubmit ui");
    check(vkQueueWaitIdle(device_.graphics_queue()), "vkQueueWaitIdle ui");
    vkFreeCommandBuffers(device_.handle(), pool_, 1, &command);
    vkDestroyBuffer(device_.handle(), staging, nullptr);
    vkFreeMemory(device_.handle(), staging_memory, nullptr);

    VkImageViewCreateInfo view_create{};
    view_create.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_create.image = image;
    view_create.viewType = view_type;
    view_create.format = VK_FORMAT_R8G8B8A8_UNORM;
    view_create.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, layers};
    check(vkCreateImageView(device_.handle(), &view_create, nullptr, &view),
          "vkCreateImageView ui texture");
}

void UiRenderer::create_atlas(const std::filesystem::path& items,
                              const std::filesystem::path& gui) {
    VkCommandPoolCreateInfo pool_create{};
    pool_create.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_create.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    pool_create.queueFamilyIndex = device_.graphics_family();
    check(vkCreateCommandPool(device_.handle(), &pool_create, nullptr, &pool_),
          "vkCreateCommandPool ui");

    constexpr uint32_t kTile = 16;
    std::vector<uint8_t> pixels;
    uint32_t tile = kTile;
    item_layers_ = 1;
    std::ifstream input(items, std::ios::binary);
    if (input) {
        char magic[4]{};
        input.read(magic, 4);
        uint32_t header[6]{};
        input.read(reinterpret_cast<char*>(header), sizeof(header));
        // A file from another version of the baker is said out loud: read as
        // if it fit, the icons silently vanish, which is how this once went
        // unnoticed for a whole series of changes.
        constexpr uint32_t kItemsVersion = 4;
        if (std::memcmp(magic, "PLIT", 4) == 0 && header[0] != kItemsVersion) {
            std::cerr << std::format("{} is baker version {}, this client reads {}; "
                                     "bake it again with tools/bake_all.py\n",
                                     items.string(), header[0], kItemsVersion);
        }
        if (std::memcmp(magic, "PLIT", 4) == 0 && header[0] == kItemsVersion && header[3] > 0) {
            tile = header[2];
            item_layers_ = header[3];
            input.seekg(static_cast<std::streamoff>(((4 + sizeof(header)) + 7) & ~size_t{7}));
            pixels.resize(static_cast<size_t>(tile) * tile * 4 * item_layers_);
            input.read(reinterpret_cast<char*>(pixels.data()),
                       static_cast<std::streamsize>(pixels.size()));
            item_names_.reserve(item_layers_);
            for (uint32_t i = 0; i < item_layers_; ++i) {
                uint16_t length = 0;
                input.read(reinterpret_cast<char*>(&length), sizeof(length));
                std::string name(length, '\0');
                if (length != 0) input.read(name.data(), length);
                uint8_t armour = 0;
                input.read(reinterpret_cast<char*>(&armour), sizeof(armour));
                if (!input) break;
                item_names_.push_back(std::move(name));
                item_armour_.push_back(armour);
            }
        }
    }
    // Without the baked files there is still a texture bound, because the
    // pipeline needs one; it is simply one transparent pixel nothing asks for.
    if (pixels.empty()) {
        item_layers_ = 1;
        tile = kTile;
        pixels.assign(static_cast<size_t>(tile) * tile * 4, 0);
        item_names_.clear();
        item_armour_.clear();
    }
    upload_texture(pixels, tile, tile, item_layers_, item_image_, item_memory_, item_view_,
                   VK_IMAGE_VIEW_TYPE_2D_ARRAY);

    uint32_t gui_side = 1;
    std::vector<uint8_t> gui_pixels;
    std::ifstream gui_input(gui, std::ios::binary);
    if (gui_input) {
        char magic[4]{};
        gui_input.read(magic, 4);
        uint32_t header[7]{};
        gui_input.read(reinterpret_cast<char*>(header), sizeof(header));
        constexpr uint32_t kGuiVersion = 3;
        if (std::memcmp(magic, "PLGU", 4) == 0 && header[0] != kGuiVersion) {
            std::cerr << std::format("{} is baker version {}, this client reads {}; "
                                     "bake it again with tools/bake_all.py\n",
                                     gui.string(), header[0], kGuiVersion);
        }
        if (std::memcmp(magic, "PLGU", 4) == 0 && header[0] == kGuiVersion && header[2] > 0) {
            gui_side = header[2];
            gui_input.seekg(static_cast<std::streamoff>(((4 + sizeof(header)) + 7) & ~size_t{7}));
            gui_pixels.resize(static_cast<size_t>(gui_side) * gui_side * 4);
            gui_input.read(reinterpret_cast<char*>(gui_pixels.data()),
                           static_cast<std::streamsize>(gui_pixels.size()));
            const float scale = 1.0f / static_cast<float>(gui_side);
            for (uint32_t i = 0; i < header[3]; ++i) {
                uint16_t length = 0;
                gui_input.read(reinterpret_cast<char*>(&length), sizeof(length));
                std::string name(length, '\0');
                if (length != 0) gui_input.read(name.data(), length);
                uint16_t rect[4]{};
                gui_input.read(reinterpret_cast<char*>(rect), sizeof(rect));
                if (!gui_input) break;
                sprites_.emplace(std::move(name),
                                 Sprite{static_cast<float>(rect[0]) * scale,
                                        static_cast<float>(rect[1]) * scale,
                                        static_cast<float>(rect[2]) * scale,
                                        static_cast<float>(rect[3]) * scale,
                                        static_cast<float>(rect[2]),
                                        static_cast<float>(rect[3])});
            }
            // The font table: a codepoint, how far it advances, and the sheet
            // it came from. A space has an advance and no picture at all.
            for (uint32_t i = 0; i < header[5]; ++i) {
                uint32_t code = 0;
                int16_t advance = 0;
                uint8_t box[2]{};
                gui_input.read(reinterpret_cast<char*>(&code), sizeof(code));
                gui_input.read(reinterpret_cast<char*>(&advance), sizeof(advance));
                gui_input.read(reinterpret_cast<char*>(box), sizeof(box));
                if (!gui_input) break;
                Glyph glyph;
                glyph.advance = static_cast<float>(advance);
                glyph.draw_height = static_cast<float>(box[0]);
                glyph.ascent = static_cast<float>(box[1]);
                const auto found = sprites_.find("glyph/" + std::to_string(code));
                if (found != sprites_.end()) {
                    glyph.sprite = found->second;
                    glyph.draw_width = found->second.width * glyph.draw_height /
                                       std::max(found->second.height, 1.0f);
                }
                glyphs_.emplace(code, glyph);
            }
            for (uint32_t i = 0; i < header[6]; ++i) {
                uint16_t length = 0;
                gui_input.read(reinterpret_cast<char*>(&length), sizeof(length));
                std::string name(length, '\0');
                if (length != 0) gui_input.read(name.data(), length);
                uint8_t values[5]{};
                gui_input.read(reinterpret_cast<char*>(values), sizeof(values));
                if (!gui_input) break;
                slicing_.emplace(std::move(name),
                                 Slicing{static_cast<float>(values[0]),
                                         static_cast<float>(values[1]),
                                         static_cast<float>(values[2]),
                                         static_cast<float>(values[3]), values[4] != 0});
            }
        }
    }
    if (gui_pixels.empty()) {
        gui_side = 1;
        gui_pixels.assign(4, 0);
        sprites_.clear();
        glyphs_.clear();
        slicing_.clear();
    }
    upload_texture(gui_pixels, gui_side, gui_side, 1, gui_image_, gui_memory_, gui_view_,
                   VK_IMAGE_VIEW_TYPE_2D);

    VkSamplerCreateInfo sampler{};
    sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    // Pixel art: smoothing a sixteen pixel icon up to forty turns it to mush.
    sampler.magFilter = VK_FILTER_NEAREST;
    sampler.minFilter = VK_FILTER_NEAREST;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    check(vkCreateSampler(device_.handle(), &sampler, nullptr, &sampler_), "vkCreateSampler ui");

    VkDescriptorSetLayoutBinding bindings[2]{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[1] = bindings[0];
    bindings[1].binding = 1;
    VkDescriptorSetLayoutCreateInfo set_layout{};
    set_layout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    set_layout.bindingCount = 2;
    set_layout.pBindings = bindings;
    check(vkCreateDescriptorSetLayout(device_.handle(), &set_layout, nullptr, &set_layout_),
          "vkCreateDescriptorSetLayout ui");

    VkDescriptorPoolSize size{};
    size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    size.descriptorCount = 2;
    VkDescriptorPoolCreateInfo descriptor_pool{};
    descriptor_pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptor_pool.maxSets = 1;
    descriptor_pool.poolSizeCount = 1;
    descriptor_pool.pPoolSizes = &size;
    check(vkCreateDescriptorPool(device_.handle(), &descriptor_pool, nullptr, &descriptor_pool_),
          "vkCreateDescriptorPool ui");

    VkDescriptorSetAllocateInfo set_allocate{};
    set_allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    set_allocate.descriptorPool = descriptor_pool_;
    set_allocate.descriptorSetCount = 1;
    set_allocate.pSetLayouts = &set_layout_;
    check(vkAllocateDescriptorSets(device_.handle(), &set_allocate, &descriptor_set_),
          "vkAllocateDescriptorSets ui");

    VkDescriptorImageInfo image_info[2]{};
    image_info[0].sampler = sampler_;
    image_info[0].imageView = item_view_;
    image_info[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    image_info[1] = image_info[0];
    image_info[1].imageView = gui_view_;
    VkWriteDescriptorSet writes[2]{};
    for (int i = 0; i < 2; ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = descriptor_set_;
        writes[i].dstBinding = static_cast<uint32_t>(i);
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].pImageInfo = &image_info[i];
    }
    vkUpdateDescriptorSets(device_.handle(), 2, writes, 0, nullptr);
}

void UiRenderer::create_pipeline(VkFormat colour_format) {
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
          "vkCreatePipelineLayout ui");

    VkShaderModule vertex = make_module(device_.handle(), shaders::UiVert());
    VkShaderModule fragment = make_module(device_.handle(), shaders::UiFrag());

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
    binding.stride = sizeof(Quad);
    binding.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    VkVertexInputAttributeDescription attributes[8]{};
    attributes[0] = {0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Quad, x)};
    attributes[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Quad, radius)};
    attributes[2] = {2, 0, VK_FORMAT_R32_UINT, offsetof(Quad, fill_top)};
    attributes[3] = {3, 0, VK_FORMAT_R32_UINT, offsetof(Quad, fill_bottom)};
    attributes[4] = {4, 0, VK_FORMAT_R32_UINT, offsetof(Quad, border_rgba)};
    attributes[5] = {5, 0, VK_FORMAT_R32_UINT, offsetof(Quad, glow_rgba)};
    attributes[6] = {6, 0, VK_FORMAT_R32_SINT, offsetof(Quad, layer)};
    attributes[7] = {7, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Quad, uv)};

    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input.vertexBindingDescriptionCount = 1;
    vertex_input.pVertexBindingDescriptions = &binding;
    vertex_input.vertexAttributeDescriptionCount = 8;
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
    check(result, "vkCreateGraphicsPipelines ui");
}

void UiRenderer::begin() { count_ = 0; }

void UiRenderer::add(const Quad& quad) {
    if (count_ >= kCapacity || quad.width <= 0.0f || quad.height <= 0.0f) return;
    mapped_[count_++] = quad;
}

void UiRenderer::panel(float x, float y, float width, float height, float radius, uint32_t fill,
                       uint32_t border_rgba, float border) {
    Quad quad;
    quad.x = x;
    quad.y = y;
    quad.width = width;
    quad.height = height;
    quad.radius = radius;
    quad.border = border;
    quad.fill_top = fill;
    quad.fill_bottom = fill;
    quad.border_rgba = border_rgba;
    add(quad);
}

void UiRenderer::record(VkCommandBuffer command, uint32_t width, uint32_t height) {
    if (count_ == 0 || width == 0 || height == 0) return;
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    const VkViewport viewport{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height),
                              0.0f, 1.0f};
    const VkRect2D scissor{{0, 0}, {width, height}};
    vkCmdSetViewport(command, 0, 1, &viewport);
    vkCmdSetScissor(command, 0, 1, &scissor);
    const PushConstants push{{1.0f / static_cast<float>(width),
                              1.0f / static_cast<float>(height)},
                             linearise_ ? 1.0f : 0.0f};
    vkCmdPushConstants(command, layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1,
                            &descriptor_set_, 0, nullptr);
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(command, 0, 1, &buffer_, &offset);
    vkCmdDraw(command, 6, count_, 0, 0);
}

UiRenderer::~UiRenderer() {
    if (device_.handle() == VK_NULL_HANDLE) return;
    vkDeviceWaitIdle(device_.handle());
    destroy();
}

void UiRenderer::destroy() {
    if (mapped_ != nullptr) vkUnmapMemory(device_.handle(), memory_);
    vkDestroyBuffer(device_.handle(), buffer_, nullptr);
    vkFreeMemory(device_.handle(), memory_, nullptr);
    vkDestroyPipeline(device_.handle(), pipeline_, nullptr);
    vkDestroyPipelineLayout(device_.handle(), layout_, nullptr);
    vkDestroyDescriptorPool(device_.handle(), descriptor_pool_, nullptr);
    vkDestroyDescriptorSetLayout(device_.handle(), set_layout_, nullptr);
    vkDestroySampler(device_.handle(), sampler_, nullptr);
    vkDestroyImageView(device_.handle(), item_view_, nullptr);
    vkDestroyImage(device_.handle(), item_image_, nullptr);
    vkFreeMemory(device_.handle(), item_memory_, nullptr);
    vkDestroyImageView(device_.handle(), gui_view_, nullptr);
    vkDestroyImage(device_.handle(), gui_image_, nullptr);
    vkFreeMemory(device_.handle(), gui_memory_, nullptr);
    vkDestroyCommandPool(device_.handle(), pool_, nullptr);
}

void UiRenderer::icon(int32_t id, float x, float y, float size, uint32_t rgba) {
    if (id < 0 || id >= static_cast<int32_t>(item_layers_)) return;
    Quad quad;
    quad.x = x;
    quad.y = y;
    quad.width = size;
    quad.height = size;
    quad.fill_top = rgba;
    quad.fill_bottom = rgba;
    quad.layer = id;
    add(quad);
}

void UiRenderer::sprite(std::string_view name, float x, float y, float width, float height,
                        uint32_t rgba) {
    const auto found = sprites_.find(name);
    if (found == sprites_.end()) return;
    Quad quad;
    quad.x = x;
    quad.y = y;
    quad.width = width;
    quad.height = height;
    quad.fill_top = rgba;
    quad.fill_bottom = rgba;
    quad.layer = -2;
    quad.uv[0] = found->second.u;
    quad.uv[1] = found->second.v;
    quad.uv[2] = found->second.du;
    quad.uv[3] = found->second.dv;
    add(quad);
}

namespace {

// Enough of utf-8 to walk a string one codepoint at a time; anything malformed
// is taken as one byte so a bad string cannot stall the loop.
uint32_t next_codepoint(std::string_view text, size_t& at) {
    const auto lead = static_cast<unsigned char>(text[at]);
    size_t length = 1;
    uint32_t code = lead;
    if ((lead & 0xE0u) == 0xC0u) {
        length = 2;
        code = lead & 0x1Fu;
    } else if ((lead & 0xF0u) == 0xE0u) {
        length = 3;
        code = lead & 0x0Fu;
    } else if ((lead & 0xF8u) == 0xF0u) {
        length = 4;
        code = lead & 0x07u;
    }
    if (at + length > text.size()) length = 1;
    for (size_t i = 1; i < length; ++i) {
        const auto byte = static_cast<unsigned char>(text[at + i]);
        if ((byte & 0xC0u) != 0x80u) {
            length = 1;
            code = lead;
            break;
        }
        code = (code << 6) | (byte & 0x3Fu);
    }
    at += length;
    return code;
}

} // namespace

float UiRenderer::text(std::string_view utf8, float x, float y, float scale, uint32_t rgba,
                       bool shadow) {
    if (glyphs_.empty()) return 0.0f;
    if (shadow) {
        // The game darkens to a quarter and keeps the alpha, which is what
        // stops white text disappearing against a bright sky.
        const uint32_t dark = ((((rgba >> 24) & 0xFFu) / 4u) << 24) |
                              ((((rgba >> 16) & 0xFFu) / 4u) << 16) |
                              ((((rgba >> 8) & 0xFFu) / 4u) << 8) | (rgba & 0xFFu);
        text(utf8, x + scale, y + scale, scale, dark, false);
    }

    float pen = x;
    for (size_t at = 0; at < utf8.size();) {
        const uint32_t code = next_codepoint(utf8, at);
        const auto found = glyphs_.find(code);
        if (found == glyphs_.end()) continue;
        const Glyph& glyph = found->second;
        if (glyph.draw_width > 0.0f) {
            Quad quad;
            quad.x = pen;
            // Seven is where the baseline sits on a line of this font, so a
            // taller sheet hangs its accents above the line rather than below.
            quad.y = y + (7.0f - glyph.ascent) * scale;
            quad.width = glyph.draw_width * scale;
            quad.height = glyph.draw_height * scale;
            quad.fill_top = rgba;
            quad.fill_bottom = rgba;
            quad.layer = -2;
            quad.uv[0] = glyph.sprite.u;
            quad.uv[1] = glyph.sprite.v;
            quad.uv[2] = glyph.sprite.du;
            quad.uv[3] = glyph.sprite.dv;
            add(quad);
        }
        pen += glyph.advance * scale;
    }
    return pen - x;
}

float UiRenderer::text_width(std::string_view utf8, float scale) const {
    float width = 0.0f;
    for (size_t at = 0; at < utf8.size();) {
        const auto found = glyphs_.find(next_codepoint(utf8, at));
        if (found != glyphs_.end()) width += found->second.advance * scale;
    }
    return width;
}

void UiRenderer::sprite_part(std::string_view name, float x, float y, float width, float height,
                             float fraction_x, float fraction_y, uint32_t rgba) {
    const auto found = sprites_.find(name);
    if (found == sprites_.end() || fraction_x <= 0.0f || fraction_y <= 0.0f) return;
    Quad quad;
    quad.x = x;
    quad.y = y;
    quad.width = width;
    quad.height = height;
    quad.fill_top = rgba;
    quad.fill_bottom = rgba;
    quad.layer = -2;
    quad.uv[0] = found->second.u;
    quad.uv[1] = found->second.v;
    quad.uv[2] = found->second.du * fraction_x;
    quad.uv[3] = found->second.dv * fraction_y;
    add(quad);
}

void UiRenderer::region(const Sprite& sprite, float sx, float sy, float sw, float sh, float dx,
                        float dy, float dw, float dh, uint32_t rgba) {
    if (dw <= 0.0f || dh <= 0.0f || sw <= 0.0f || sh <= 0.0f) return;
    Quad quad;
    quad.x = dx;
    quad.y = dy;
    quad.width = dw;
    quad.height = dh;
    quad.fill_top = rgba;
    quad.fill_bottom = rgba;
    quad.layer = -2;
    quad.uv[0] = sprite.u + sprite.du * (sx / sprite.width);
    quad.uv[1] = sprite.v + sprite.dv * (sy / sprite.height);
    quad.uv[2] = sprite.du * (sw / sprite.width);
    quad.uv[3] = sprite.dv * (sh / sprite.height);
    add(quad);
}

// Repeats a piece of a sprite across a rectangle at its natural size, cutting
// the last copy short rather than squeezing every copy to fit.
void UiRenderer::tile(const Sprite& sprite, float sx, float sy, float sw, float sh, float dx,
                      float dy, float dw, float dh, float scale, uint32_t rgba) {
    const float step_x = sw * scale;
    const float step_y = sh * scale;
    if (step_x <= 0.0f || step_y <= 0.0f) return;
    for (float y = 0.0f; y < dh; y += step_y) {
        const float h = std::min(step_y, dh - y);
        for (float x = 0.0f; x < dw; x += step_x) {
            const float w = std::min(step_x, dw - x);
            region(sprite, sx, sy, w / scale, h / scale, dx + x, dy + y, w, h, rgba);
        }
    }
}

void UiRenderer::nine_slice(std::string_view name, float x, float y, float width, float height,
                            float scale, uint32_t rgba) {
    const auto found = sprites_.find(name);
    if (found == sprites_.end()) return;
    const Sprite& sprite = found->second;
    const auto slicing = slicing_.find(name);
    if (slicing == slicing_.end()) {
        region(sprite, 0.0f, 0.0f, sprite.width, sprite.height, x, y, width, height, rgba);
        return;
    }
    const Slicing& s = slicing->second;
    // In game pixels on the sprite, and in screen pixels on the target.
    const float W = sprite.width;
    const float H = sprite.height;
    const float inner_w = W - s.left - s.right;
    const float inner_h = H - s.top - s.bottom;
    const float l = s.left * scale;
    const float t = s.top * scale;
    const float r = s.right * scale;
    const float b = s.bottom * scale;
    const float mid_w = width - l - r;
    const float mid_h = height - t - b;

    region(sprite, 0, 0, s.left, s.top, x, y, l, t, rgba);
    region(sprite, W - s.right, 0, s.right, s.top, x + width - r, y, r, t, rgba);
    region(sprite, 0, H - s.bottom, s.left, s.bottom, x, y + height - b, l, b, rgba);
    region(sprite, W - s.right, H - s.bottom, s.right, s.bottom, x + width - r, y + height - b, r,
           b, rgba);

    tile(sprite, s.left, 0, inner_w, s.top, x + l, y, mid_w, t, scale, rgba);
    tile(sprite, s.left, H - s.bottom, inner_w, s.bottom, x + l, y + height - b, mid_w, b, scale,
         rgba);
    tile(sprite, 0, s.top, s.left, inner_h, x, y + t, l, mid_h, scale, rgba);
    tile(sprite, W - s.right, s.top, s.right, inner_h, x + width - r, y + t, r, mid_h, scale,
         rgba);

    if (s.stretch_inner) {
        region(sprite, s.left, s.top, inner_w, inner_h, x + l, y + t, mid_w, mid_h, rgba);
    } else {
        tile(sprite, s.left, s.top, inner_w, inner_h, x + l, y + t, mid_w, mid_h, scale, rgba);
    }
}

std::pair<float, float> UiRenderer::sprite_size(std::string_view name) const {
    const auto found = sprites_.find(name);
    if (found == sprites_.end()) return {0.0f, 0.0f};
    return {found->second.width, found->second.height};
}

int32_t UiRenderer::item_armour(int32_t id) const {
    if (id < 0 || id >= static_cast<int32_t>(item_armour_.size())) return 0;
    return item_armour_[static_cast<size_t>(id)];
}

std::filesystem::path UiRenderer::default_gui_path(int32_t protocol) {
    std::filesystem::path base;
    if (const char* data_home = std::getenv("XDG_DATA_HOME")) {
        base = data_home;
    } else if (const char* home = std::getenv("HOME")) {
        base = std::filesystem::path(home) / ".local" / "share";
    }
    return base / "plasma-client" / "data" / ("gui-" + std::to_string(protocol) + ".plgu");
}

std::string_view UiRenderer::item_name(int32_t id) const {
    if (id < 0 || id >= static_cast<int32_t>(item_names_.size())) return {};
    return item_names_[static_cast<size_t>(id)];
}

std::filesystem::path UiRenderer::default_items_path(int32_t protocol) {
    std::filesystem::path base;
    if (const char* data_home = std::getenv("XDG_DATA_HOME")) {
        base = data_home;
    } else if (const char* home = std::getenv("HOME")) {
        base = std::filesystem::path(home) / ".local" / "share";
    }
    return base / "plasma-client" / "data" / ("items-" + std::to_string(protocol) + ".plit");
}

} // namespace mcpp::gpu
