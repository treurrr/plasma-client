#include "mcpp/gpu/ui.hpp"

#include "mcpp/byte_buffer.hpp"

#include <cstddef>
#include <algorithm>
#include <cstdio>
#include <cmath>
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
                if (code > 32 && glyph.draw_width > 0.0f) {
                    glyphs_by_width_[static_cast<int>(glyph.advance)].push_back(code);
                }
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

    {
        const std::vector<uint8_t> blank(static_cast<size_t>(kDynamicSide) * kDynamicSide * 4 *
                                             kDynamicLayers,
                                         0);
        upload_texture(blank, kDynamicSide, kDynamicSide, kDynamicLayers, dynamic_image_,
                       dynamic_memory_, dynamic_view_, VK_IMAGE_VIEW_TYPE_2D_ARRAY);
        const std::vector<uint8_t> faces(4 * 6, 0);
        upload_texture(faces, 1, 1, 6, panorama_image_, panorama_memory_, panorama_view_,
                       VK_IMAGE_VIEW_TYPE_2D_ARRAY);
    }
    sampler.magFilter = VK_FILTER_LINEAR;
    sampler.minFilter = VK_FILTER_LINEAR;
    check(vkCreateSampler(device_.handle(), &sampler, nullptr, &linear_sampler_),
          "vkCreateSampler ui linear");

    VkDescriptorSetLayoutBinding bindings[4]{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    for (uint32_t i = 1; i < 4; ++i) {
        bindings[i] = bindings[0];
        bindings[i].binding = i;
    }
    VkDescriptorSetLayoutCreateInfo set_layout{};
    set_layout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    set_layout.bindingCount = 4;
    set_layout.pBindings = bindings;
    check(vkCreateDescriptorSetLayout(device_.handle(), &set_layout, nullptr, &set_layout_),
          "vkCreateDescriptorSetLayout ui");

    VkDescriptorPoolSize size{};
    size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    size.descriptorCount = 4;
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

    write_descriptors();
}

void UiRenderer::write_descriptors() {
    VkDescriptorImageInfo image_info[4]{};
    image_info[0].sampler = sampler_;
    image_info[0].imageView = item_view_;
    image_info[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    image_info[1] = image_info[0];
    image_info[1].imageView = gui_view_;
    image_info[2] = image_info[0];
    image_info[2].imageView = dynamic_view_;
    image_info[3] = image_info[0];
    image_info[3].sampler = linear_sampler_;
    image_info[3].imageView = panorama_view_;
    VkWriteDescriptorSet writes[4]{};
    for (int i = 0; i < 4; ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = descriptor_set_;
        writes[i].dstBinding = static_cast<uint32_t>(i);
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].pImageInfo = &image_info[i];
    }
    vkUpdateDescriptorSets(device_.handle(), 4, writes, 0, nullptr);
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

    VkVertexInputAttributeDescription attributes[10]{};
    attributes[0] = {0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Quad, x)};
    attributes[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Quad, radius)};
    attributes[2] = {2, 0, VK_FORMAT_R32_UINT, offsetof(Quad, fill_top)};
    attributes[3] = {3, 0, VK_FORMAT_R32_UINT, offsetof(Quad, fill_bottom)};
    attributes[4] = {4, 0, VK_FORMAT_R32_UINT, offsetof(Quad, border_rgba)};
    attributes[5] = {5, 0, VK_FORMAT_R32_UINT, offsetof(Quad, glow_rgba)};
    attributes[6] = {6, 0, VK_FORMAT_R32_SINT, offsetof(Quad, layer)};
    attributes[7] = {7, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Quad, uv)};
    attributes[8] = {8, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Quad, transform)};
    attributes[9] = {9, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Quad, origin)};

    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input.vertexBindingDescriptionCount = 1;
    vertex_input.pVertexBindingDescriptions = &binding;
    vertex_input.vertexAttributeDescriptionCount = 10;
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

void UiRenderer::begin() {
    count_ = 0;
    segments_.clear();
    scissors_.clear();
    reset_transform();
}

void UiRenderer::add(const Quad& quad) {
    if (count_ >= kCapacity || quad.width <= 0.0f || quad.height <= 0.0f) return;
    Quad& out = mapped_[count_++];
    out = quad;
    if (transform_[0] != 1.0f || transform_[1] != 0.0f || transform_[2] != 0.0f ||
        transform_[3] != 1.0f) {
        for (int i = 0; i < 4; ++i) out.transform[i] = transform_[i];
        out.origin[0] = transform_[4];
        out.origin[1] = transform_[5];
    }
}

void UiRenderer::fill(float x, float y, float width, float height, uint32_t rgba) {
    panel(x, y, width, height, 0.0f, rgba);
}

void UiRenderer::start_segment() {
    VkRect2D rect{{0, 0}, {0x7FFFFFFFu, 0x7FFFFFFFu}};
    if (!scissors_.empty()) rect = scissors_.back();
    if (!segments_.empty() && segments_.back().first == count_) {
        segments_.back().scissor = rect;
    } else {
        segments_.push_back({count_, rect});
    }
}

void UiRenderer::push_scissor(float x, float y, float width, float height) {
    int32_t x0 = static_cast<int32_t>(std::lround(x));
    int32_t y0 = static_cast<int32_t>(std::lround(y));
    int32_t x1 = static_cast<int32_t>(std::lround(x + width));
    int32_t y1 = static_cast<int32_t>(std::lround(y + height));
    if (!scissors_.empty()) {
        const auto& outer = scissors_.back();
        x0 = std::max(x0, outer.offset.x);
        y0 = std::max(y0, outer.offset.y);
        x1 = std::min(x1, outer.offset.x + static_cast<int32_t>(outer.extent.width));
        y1 = std::min(y1, outer.offset.y + static_cast<int32_t>(outer.extent.height));
    }
    x0 = std::max(x0, 0);
    y0 = std::max(y0, 0);
    scissors_.push_back({{x0, y0},
                         {static_cast<uint32_t>(std::max(0, x1 - x0)),
                          static_cast<uint32_t>(std::max(0, y1 - y0))}});
    start_segment();
}

void UiRenderer::pop_scissor() {
    if (!scissors_.empty()) scissors_.pop_back();
    start_segment();
}

void UiRenderer::set_transform(float a, float b, float c, float d, float origin_x,
                               float origin_y) {
    transform_[0] = a;
    transform_[1] = b;
    transform_[2] = c;
    transform_[3] = d;
    transform_[4] = origin_x;
    transform_[5] = origin_y;
}

void UiRenderer::reset_transform() { set_transform(1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f); }

void UiRenderer::copy_layer(VkImage image, uint32_t layer, uint32_t width, uint32_t height,
                            const std::vector<uint8_t>& pixels) {
    check(vkQueueWaitIdle(device_.graphics_queue()), "vkQueueWaitIdle ui copy");
    VkBufferCreateInfo staging_create{};
    staging_create.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    staging_create.size = pixels.size();
    staging_create.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VkBuffer staging = VK_NULL_HANDLE;
    check(vkCreateBuffer(device_.handle(), &staging_create, nullptr, &staging),
          "vkCreateBuffer ui layer");
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device_.handle(), staging, &requirements);
    VkMemoryAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex =
        memory_type(device_.physical(), requirements.memoryTypeBits,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkDeviceMemory memory = VK_NULL_HANDLE;
    check(vkAllocateMemory(device_.handle(), &allocate, nullptr, &memory),
          "vkAllocateMemory ui layer");
    check(vkBindBufferMemory(device_.handle(), staging, memory, 0), "vkBindBufferMemory ui layer");
    void* mapped = nullptr;
    check(vkMapMemory(device_.handle(), memory, 0, VK_WHOLE_SIZE, 0, &mapped), "vkMapMemory");
    std::memcpy(mapped, pixels.data(), pixels.size());
    vkUnmapMemory(device_.handle(), memory);

    VkCommandBufferAllocateInfo command_allocate{};
    command_allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    command_allocate.commandPool = pool_;
    command_allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_allocate.commandBufferCount = 1;
    VkCommandBuffer command = VK_NULL_HANDLE;
    check(vkAllocateCommandBuffers(device_.handle(), &command_allocate, &command),
          "vkAllocateCommandBuffers ui layer");
    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(command, &begin_info), "vkBeginCommandBuffer ui layer");
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, layer, 1};
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, layer, 1};
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
    check(vkEndCommandBuffer(command), "vkEndCommandBuffer ui layer");
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command;
    check(vkQueueSubmit(device_.graphics_queue(), 1, &submit, VK_NULL_HANDLE),
          "vkQueueSubmit ui layer");
    check(vkQueueWaitIdle(device_.graphics_queue()), "vkQueueWaitIdle ui layer");
    vkFreeCommandBuffers(device_.handle(), pool_, 1, &command);
    vkDestroyBuffer(device_.handle(), staging, nullptr);
    vkFreeMemory(device_.handle(), memory, nullptr);
}

void UiRenderer::set_dynamic(uint32_t layer, const std::vector<uint8_t>& rgba, uint32_t width,
                             uint32_t height) {
    if (layer >= kDynamicLayers || width == 0 || height == 0 ||
        rgba.size() != static_cast<size_t>(width) * height * 4) {
        return;
    }
    // Every layer is the same size; a picture of another size is sampled to
    // fit, nearest texel, which is what the game does to a server's icon.
    std::vector<uint8_t> fitted(static_cast<size_t>(kDynamicSide) * kDynamicSide * 4);
    for (uint32_t y = 0; y < kDynamicSide; ++y) {
        for (uint32_t x = 0; x < kDynamicSide; ++x) {
            const uint32_t sx = x * width / kDynamicSide;
            const uint32_t sy = y * height / kDynamicSide;
            std::memcpy(&fitted[(static_cast<size_t>(y) * kDynamicSide + x) * 4],
                        &rgba[(static_cast<size_t>(sy) * width + sx) * 4], 4);
        }
    }
    copy_layer(dynamic_image_, layer, kDynamicSide, kDynamicSide, fitted);
}

void UiRenderer::dynamic(uint32_t layer, float x, float y, float width, float height,
                         uint32_t rgba) {
    if (layer >= kDynamicLayers) return;
    Quad quad;
    quad.x = x;
    quad.y = y;
    quad.width = width;
    quad.height = height;
    quad.fill_top = rgba;
    quad.fill_bottom = rgba;
    quad.layer = -16 - static_cast<int32_t>(layer);
    add(quad);
}

void UiRenderer::set_panorama(const std::vector<uint8_t> (&faces)[6], uint32_t side) {
    for (const auto& face : faces) {
        if (face.size() != static_cast<size_t>(side) * side * 4) return;
    }
    check(vkDeviceWaitIdle(device_.handle()), "vkDeviceWaitIdle ui panorama");
    vkDestroyImageView(device_.handle(), panorama_view_, nullptr);
    vkDestroyImage(device_.handle(), panorama_image_, nullptr);
    vkFreeMemory(device_.handle(), panorama_memory_, nullptr);
    std::vector<uint8_t> all;
    all.reserve(static_cast<size_t>(side) * side * 4 * 6);
    for (const auto& face : faces) all.insert(all.end(), face.begin(), face.end());
    upload_texture(all, side, side, 6, panorama_image_, panorama_memory_, panorama_view_,
                   VK_IMAGE_VIEW_TYPE_2D_ARRAY);
    panorama_side_ = side;
    write_descriptors();
}

void UiRenderer::panorama(float x, float y, float width, float height, float yaw, float pitch,
                          float blur, uint32_t rgba) {
    Quad quad;
    quad.x = x;
    quad.y = y;
    quad.width = width;
    quad.height = height;
    quad.fill_top = rgba;
    quad.fill_bottom = rgba;
    quad.layer = -3;
    quad.uv[0] = yaw;
    quad.uv[1] = pitch;
    quad.uv[2] = blur;
    add(quad);
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
    vkCmdSetViewport(command, 0, 1, &viewport);
    const PushConstants push{{1.0f / static_cast<float>(width),
                              1.0f / static_cast<float>(height)},
                             linearise_ ? 1.0f : 0.0f};
    vkCmdPushConstants(command, layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1,
                            &descriptor_set_, 0, nullptr);
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(command, 0, 1, &buffer_, &offset);
    if (segments_.empty() || segments_.front().first != 0) {
        segments_.insert(segments_.begin(), Segment{0, {{0, 0}, {width, height}}});
    }
    for (size_t i = 0; i < segments_.size(); ++i) {
        const uint32_t first = segments_[i].first;
        const uint32_t last = i + 1 < segments_.size() ? segments_[i + 1].first : count_;
        if (last <= first) continue;
        VkRect2D scissor = segments_[i].scissor;
        scissor.extent.width = std::min(scissor.extent.width,
                                        width - std::min<uint32_t>(width, scissor.offset.x));
        scissor.extent.height = std::min(scissor.extent.height,
                                         height - std::min<uint32_t>(height, scissor.offset.y));
        if (scissor.extent.width == 0 || scissor.extent.height == 0) continue;
        vkCmdSetScissor(command, 0, 1, &scissor);
        vkCmdDraw(command, 6, last - first, 0, first);
    }
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
    vkDestroySampler(device_.handle(), linear_sampler_, nullptr);
    vkDestroyImageView(device_.handle(), dynamic_view_, nullptr);
    vkDestroyImage(device_.handle(), dynamic_image_, nullptr);
    vkFreeMemory(device_.handle(), dynamic_memory_, nullptr);
    vkDestroyImageView(device_.handle(), panorama_view_, nullptr);
    vkDestroyImage(device_.handle(), panorama_image_, nullptr);
    vkFreeMemory(device_.handle(), panorama_memory_, nullptr);
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

float UiRenderer::glyph(uint32_t code, float x, float y, float scale, uint32_t rgba) {
    const auto found = glyphs_.find(code);
    if (found == glyphs_.end()) return 0.0f;
    const Glyph& g = found->second;
    if (g.draw_width > 0.0f) {
        Quad quad;
        quad.x = x;
        quad.y = y + (7.0f - g.ascent) * scale;
        quad.width = g.draw_width * scale;
        quad.height = g.draw_height * scale;
        quad.fill_top = rgba;
        quad.fill_bottom = rgba;
        quad.layer = -2;
        quad.uv[0] = g.sprite.u;
        quad.uv[1] = g.sprite.v;
        quad.uv[2] = g.sprite.du;
        quad.uv[3] = g.sprite.dv;
        add(quad);
    }
    return g.advance;
}

float UiRenderer::advance(uint32_t code) const {
    const auto found = glyphs_.find(code);
    return found == glyphs_.end() ? 0.0f : found->second.advance;
}

uint32_t UiRenderer::glyph_like(uint32_t code, uint32_t seed) const {
    const auto found = glyphs_by_width_.find(static_cast<int>(advance(code)));
    if (found == glyphs_by_width_.end() || found->second.empty()) return code;
    return found->second[seed % found->second.size()];
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

void UiRenderer::sprite_region(std::string_view name, float sx, float sy, float sw, float sh,
                               float x, float y, float width, float height, uint32_t rgba) {
    const auto found = sprites_.find(name);
    if (found == sprites_.end()) return;
    region(found->second, sx, sy, sw, sh, x, y, width, height, rgba);
}

void UiRenderer::sprite_tiled(std::string_view name, float sx, float sy, float sw, float sh,
                              float x, float y, float width, float height, float scale,
                              uint32_t rgba) {
    const auto found = sprites_.find(name);
    if (found == sprites_.end()) return;
    tile(found->second, sx, sy, sw, sh, x, y, width, height, scale, rgba);
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
