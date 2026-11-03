#include "mcpp/gpu/entity_renderer.hpp"

#include "mcpp/byte_buffer.hpp"

#include <algorithm>
#include <cstring>
#include <format>

#include <mcpp/gpu/shaders/EntityFlatFrag.hpp>
#include <mcpp/gpu/shaders/EntityFlatVert.hpp>
#include <mcpp/gpu/shaders/EntityFrag.hpp>
#include <mcpp/gpu/shaders/EntityVert.hpp>

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

VkShaderModule make_module(VkDevice device, std::span<const uint32_t> words) {
    VkShaderModuleCreateInfo create{};
    create.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    create.codeSize = words.size() * sizeof(uint32_t);
    create.pCode = words.data();
    VkShaderModule module = VK_NULL_HANDLE;
    check(vkCreateShaderModule(device, &create, nullptr, &module), "vkCreateShaderModule");
    return module;
}

struct Push {
    float view_projection[16];
    float fog[4];      // rgb, and where it finishes
    float params[4];   // where fog starts
    uint32_t bases[4];
};
static_assert(sizeof(Push) <= 128, "push constants must fit the guaranteed minimum");

struct GpuInstance {
    uint32_t matrix_base;
    uint32_t texture;
    uint32_t light;
    uint32_t colour;
};

// Shelf packing, tallest first, every texture with a one-pixel border copied
// from its own edge so nearest sampling at a seam never reads a neighbour.
struct Packed {
    std::string name;
    const entity::Texture* texture;
    uint32_t x = 0, y = 0;
};

uint32_t abgr(uint32_t argb) {
    return (argb & 0xFF00FF00u) | ((argb >> 16) & 0xFFu) | ((argb & 0xFFu) << 16);
}

} // namespace

EntityRenderer::EntityRenderer(Device& device, VkFormat colour_format, VkFormat depth_format)
    : device_(device) {
    try {
        VkCommandPoolCreateInfo pool{};
        pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        pool.queueFamilyIndex = device_.graphics_family();
        check(vkCreateCommandPool(device_.handle(), &pool, nullptr, &pool_), "vkCreateCommandPool");

        matrices_ = make_buffer(sizeof(float) * 12 * kMaxMatrices * kFrames,
                                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);
        instances_ = make_buffer(sizeof(GpuInstance) * kMaxInstances * kFrames,
                                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);
        flat_ = make_buffer(sizeof(FlatVertex) * kMaxFlat * kFrames,
                            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true);
        skins_ = make_image(64, 64, kSkinLayers);
        queued_.reserve(kMaxInstances);
        see_through_.reserve(4096);
        tags_.reserve(4096);
        shadows_.reserve(4096);
        create_pipelines(colour_format, depth_format);
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

EntityRenderer::Buffer EntityRenderer::make_buffer(VkDeviceSize bytes, VkBufferUsageFlags usage,
                                                   bool host) {
    Buffer out;
    VkBufferCreateInfo create{};
    create.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    create.size = bytes;
    create.usage = usage;
    check(vkCreateBuffer(device_.handle(), &create, nullptr, &out.buffer), "vkCreateBuffer entity");
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device_.handle(), out.buffer, &requirements);
    VkMemoryAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = memory_type(
        device_.physical(), requirements.memoryTypeBits,
        host ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
             : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    check(vkAllocateMemory(device_.handle(), &allocate, nullptr, &out.memory),
          "vkAllocateMemory entity");
    check(vkBindBufferMemory(device_.handle(), out.buffer, out.memory, 0),
          "vkBindBufferMemory entity");
    if (host) {
        check(vkMapMemory(device_.handle(), out.memory, 0, VK_WHOLE_SIZE, 0, &out.mapped),
              "vkMapMemory entity");
    }
    return out;
}

void EntityRenderer::destroy_buffer(Buffer& buffer) {
    if (buffer.buffer != VK_NULL_HANDLE) vkDestroyBuffer(device_.handle(), buffer.buffer, nullptr);
    if (buffer.memory != VK_NULL_HANDLE) vkFreeMemory(device_.handle(), buffer.memory, nullptr);
    buffer = {};
}

EntityRenderer::Image EntityRenderer::make_image(uint32_t width, uint32_t height,
                                                 uint32_t layers) {
    Image out;
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
    check(vkCreateImage(device_.handle(), &create, nullptr, &out.image), "vkCreateImage entity");
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device_.handle(), out.image, &requirements);
    VkMemoryAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = memory_type(device_.physical(), requirements.memoryTypeBits,
                                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    check(vkAllocateMemory(device_.handle(), &allocate, nullptr, &out.memory),
          "vkAllocateMemory entity image");
    check(vkBindImageMemory(device_.handle(), out.image, out.memory, 0),
          "vkBindImageMemory entity");
    VkImageViewCreateInfo view{};
    view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image = out.image;
    view.viewType = layers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
    view.format = VK_FORMAT_R8G8B8A8_UNORM;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, layers};
    check(vkCreateImageView(device_.handle(), &view, nullptr, &out.view),
          "vkCreateImageView entity");
    return out;
}

void EntityRenderer::destroy_image(Image& image) {
    if (image.view != VK_NULL_HANDLE) vkDestroyImageView(device_.handle(), image.view, nullptr);
    if (image.image != VK_NULL_HANDLE) vkDestroyImage(device_.handle(), image.image, nullptr);
    if (image.memory != VK_NULL_HANDLE) vkFreeMemory(device_.handle(), image.memory, nullptr);
    image = {};
}

void EntityRenderer::upload(Image& image, uint32_t layer, uint32_t x, uint32_t y,
                            uint32_t width, uint32_t height, const uint8_t* rgba,
                            bool first_use) {
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(width) * height * 4;
    Buffer staging = make_buffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
    std::memcpy(staging.mapped, rgba, bytes);

    VkCommandBufferAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocate.commandPool = pool_;
    allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate.commandBufferCount = 1;
    VkCommandBuffer command = VK_NULL_HANDLE;
    check(vkAllocateCommandBuffers(device_.handle(), &allocate, &command),
          "vkAllocateCommandBuffers entity");
    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(command, &begin_info), "vkBeginCommandBuffer entity");

    const auto barrier = [&](VkImageLayout from, VkImageLayout to, VkPipelineStageFlags2 src,
                             VkAccessFlags2 src_access, VkPipelineStageFlags2 dst,
                             VkAccessFlags2 dst_access) {
        VkImageMemoryBarrier2 b{};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        b.srcStageMask = src;
        b.srcAccessMask = src_access;
        b.dstStageMask = dst;
        b.dstAccessMask = dst_access;
        b.oldLayout = from;
        b.newLayout = to;
        b.image = image.image;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, layer, 1};
        VkDependencyInfo dependency{};
        dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependency.imageMemoryBarrierCount = 1;
        dependency.pImageMemoryBarriers = &b;
        vkCmdPipelineBarrier2(command, &dependency);
    };
    barrier(first_use ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0,
            VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, layer, 1};
    region.imageOffset = {static_cast<int32_t>(x), static_cast<int32_t>(y), 0};
    region.imageExtent = {width, height, 1};
    vkCmdCopyBufferToImage(command, staging.buffer, image.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
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
    destroy_buffer(staging);
}

void EntityRenderer::load(const entity::Assets& assets) {
    vkDeviceWaitIdle(device_.handle());

    // Models: one vertex buffer, each model a range of it.
    models_ = assets.models().all();
    model_index_.clear();
    baked_.clear();
    std::vector<entity::Vertex> vertices;
    for (size_t i = 0; i < models_.size(); ++i) {
        BakedModel baked;
        baked.first = static_cast<uint32_t>(vertices.size());
        baked.count = entity::bake(models_[i], vertices);
        baked.parts = static_cast<uint32_t>(models_[i].parts.size());
        baked_.push_back(baked);
        model_index_.emplace(models_[i].name, static_cast<int32_t>(i));
    }
    destroy_buffer(vertices_);
    if (!vertices.empty()) {
        vertices_ = make_buffer(vertices.size() * sizeof(entity::Vertex),
                                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true);
        std::memcpy(vertices_.mapped, vertices.data(), vertices.size() * sizeof(entity::Vertex));
    }

    // Textures: one atlas, the font's sheet and a white texel among them.
    font_ = assets.font();
    entity::Texture white;
    white.width = white.height = 1;
    white.rgba = {255, 255, 255, 255};
    std::vector<Packed> packed;
    for (const auto& [name, texture] : assets.textures()) {
        if (texture.width > 0 && texture.height > 0) packed.push_back({name, &texture});
    }
    packed.push_back({"$white", &white});
    if (font_.sheet.width > 0) packed.push_back({"$font", &font_.sheet});
    std::stable_sort(packed.begin(), packed.end(), [](const Packed& a, const Packed& b) {
        return a.texture->height > b.texture->height;
    });
    atlas_width_ = 2048;
    uint32_t pen_x = 0;
    uint32_t pen_y = 0;
    uint32_t shelf = 0;
    for (Packed& entry : packed) {
        const uint32_t w = entry.texture->width + 2;
        const uint32_t h = entry.texture->height + 2;
        if (pen_x + w > atlas_width_) {
            pen_x = 0;
            pen_y += shelf;
            shelf = 0;
        }
        entry.x = pen_x + 1;
        entry.y = pen_y + 1;
        pen_x += w;
        shelf = std::max(shelf, h);
    }
    atlas_height_ = 1;
    while (atlas_height_ < pen_y + shelf) atlas_height_ *= 2;
    std::vector<uint8_t> pixels(static_cast<size_t>(atlas_width_) * atlas_height_ * 4, 0);
    texture_index_.clear();
    rects_.clear();
    for (const Packed& entry : packed) {
        const entity::Texture& t = *entry.texture;
        for (int32_t y = -1; y <= static_cast<int32_t>(t.height); ++y) {
            const auto sy = static_cast<uint32_t>(std::clamp<int32_t>(y, 0, t.height - 1));
            for (int32_t x = -1; x <= static_cast<int32_t>(t.width); ++x) {
                const auto sx = static_cast<uint32_t>(std::clamp<int32_t>(x, 0, t.width - 1));
                const size_t to = ((entry.y + y) * static_cast<size_t>(atlas_width_) +
                                   (entry.x + x)) * 4;
                std::memcpy(&pixels[to], &t.rgba[(sy * static_cast<size_t>(t.width) + sx) * 4], 4);
            }
        }
        texture_index_.emplace(entry.name, static_cast<int32_t>(rects_.size()));
        rects_.push_back({static_cast<float>(entry.x) / static_cast<float>(atlas_width_),
                          static_cast<float>(entry.y) / static_cast<float>(atlas_height_),
                          static_cast<float>(t.width) / static_cast<float>(atlas_width_),
                          static_cast<float>(t.height) / static_cast<float>(atlas_height_)});
    }
    font_texture_ = texture("$font");
    white_texture_ = texture("$white");
    shadow_texture_ = texture("$shadow");

    destroy_image(atlas_);
    atlas_ = make_image(atlas_width_, atlas_height_, 1);
    upload(atlas_, 0, 0, 0, atlas_width_, atlas_height_, pixels.data(), true);
    if (!skins_ready_) {
        std::vector<uint8_t> empty(64 * 64 * 4, 0);
        for (uint32_t layer = 0; layer < kSkinLayers; ++layer) {
            upload(skins_, layer, 0, 0, 64, 64, empty.data(), true);
        }
        skins_ready_ = true;
    }
    destroy_buffer(rects_buffer_);
    rects_buffer_ = make_buffer(std::max<size_t>(rects_.size(), 1) * sizeof(rects_[0]),
                                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);
    std::memcpy(rects_buffer_.mapped, rects_.data(), rects_.size() * sizeof(rects_[0]));
    write_descriptors();
}

int32_t EntityRenderer::model(std::string_view name) const {
    const auto found = model_index_.find(name);
    return found == model_index_.end() ? -1 : found->second;
}

const entity::Model* EntityRenderer::model_data(int32_t index) const {
    if (index < 0 || static_cast<size_t>(index) >= models_.size()) return nullptr;
    return &models_[static_cast<size_t>(index)];
}

int32_t EntityRenderer::texture(std::string_view name) const {
    const auto found = texture_index_.find(name);
    return found == texture_index_.end() ? -1 : found->second;
}

int32_t EntityRenderer::upload_skin(const entity::Texture& skin) {
    if (skin.width != 64 || skin.height != 64 || !skins_ready_) return -1;
    for (uint32_t layer = 0; layer < kSkinLayers; ++layer) {
        if (skin_used_[layer]) continue;
        // The layer may still be read by a frame in flight from a skin that
        // was let go; the upload waits for the queue, so that frame is done.
        upload(skins_, layer, 0, 0, 64, 64, skin.rgba.data(), false);
        skin_used_[layer] = true;
        return static_cast<int32_t>(layer);
    }
    return -1;
}

void EntityRenderer::release_skin(int32_t layer) {
    if (layer >= 0 && static_cast<uint32_t>(layer) < kSkinLayers) {
        skin_used_[static_cast<size_t>(layer)] = false;
    }
}

void EntityRenderer::write_descriptors() {
    const VkDescriptorImageInfo atlas{sampler_, atlas_.view,
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    const VkDescriptorImageInfo skins{sampler_, skins_.view,
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    const VkDescriptorBufferInfo matrices{matrices_.buffer, 0, VK_WHOLE_SIZE};
    const VkDescriptorBufferInfo instances{instances_.buffer, 0, VK_WHOLE_SIZE};
    const VkDescriptorBufferInfo rects{rects_buffer_.buffer, 0, VK_WHOLE_SIZE};
    VkWriteDescriptorSet writes[5]{};
    for (uint32_t i = 0; i < 5; ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = set_;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = i < 2 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
                                         : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    }
    writes[0].pImageInfo = &atlas;
    writes[1].pImageInfo = &skins;
    writes[2].pBufferInfo = &matrices;
    writes[3].pBufferInfo = &instances;
    writes[4].pBufferInfo = &rects;
    vkUpdateDescriptorSets(device_.handle(), 5, writes, 0, nullptr);
}

void EntityRenderer::begin() {
    ++frame_;
    matrices_used_ = 0;
    queued_.clear();
    see_through_.clear();
    tags_.clear();
    shadows_.clear();
}

bool EntityRenderer::add(const Instance& instance, std::span<const entity::Matrix> parts) {
    if (instance.model < 0 || static_cast<size_t>(instance.model) >= baked_.size()) return false;
    if (queued_.size() >= kMaxInstances || matrices_used_ + parts.size() > kMaxMatrices) {
        return false;
    }
    auto* rows = static_cast<float*>(matrices_.mapped) +
                 (static_cast<size_t>(frame_ % kFrames) * kMaxMatrices + matrices_used_) * 12;
    for (const entity::Matrix& m : parts) {
        // Column-major 4x4 to three rows.
        const float out[12] = {m[0], m[4], m[8],  m[12], m[1], m[5],
                               m[9], m[13], m[2], m[6], m[10], m[14]};
        std::memcpy(rows, out, sizeof(out));
        rows += 12;
    }
    Queued q;
    q.key = (static_cast<uint32_t>(instance.type) << 24) | static_cast<uint32_t>(instance.model);
    q.matrix_base = matrices_used_;
    q.texture = instance.texture;
    q.light = static_cast<uint32_t>(instance.sky_light & 0xF) |
              static_cast<uint32_t>(instance.block_light & 0xF) << 4 |
              static_cast<uint32_t>(instance.white & 0xF) << 8 | (instance.red ? 1u << 12 : 0u);
    q.colour = instance.colour;
    queued_.push_back(q);
    matrices_used_ += static_cast<uint32_t>(parts.size());
    return true;
}

void EntityRenderer::quad(const FlatVertex (&c)[4], std::vector<FlatVertex>& out) {
    for (const int i : {0, 1, 2, 0, 2, 3}) out.push_back(c[i]);
}

void EntityRenderer::add_tag(std::string_view text, const entity::Matrix& place, bool see_through,
                             uint32_t text_colour, uint32_t background, uint8_t sky_light,
                             uint8_t block_light) {
    if (font_texture_ < 0 || text.empty()) return;
    const uint32_t light = static_cast<uint32_t>(sky_light & 0xF) |
                           static_cast<uint32_t>(block_light & 0xF) << 4;
    const auto corner = [&](float x, float y, float z, float u, float v, uint32_t colour) {
        FlatVertex vertex{};
        const float in[4] = {x, y, z, 1.0f};
        for (int row = 0; row < 3; ++row) {
            vertex.position[row] = place[row] * in[0] + place[4 + row] * in[1] +
                                   place[8 + row] * in[2] + place[12 + row];
        }
        vertex.uv[0] = u;
        vertex.uv[1] = v;
        vertex.colour = abgr(colour);
        vertex.light = light;
        return vertex;
    };
    std::vector<FlatVertex>& out = see_through ? see_through_ : tags_;
    const float width = font_.width(text);
    const float left = -width / 2.0f;
    if ((background >> 24) != 0) {
        const auto& r = rects_[static_cast<size_t>(white_texture_)];
        const float u = r[0] + r[2] / 2.0f;
        const float v = r[1] + r[3] / 2.0f;
        const FlatVertex c[4] = {corner(left - 1.0f, -1.0f, -0.01f, u, v, background),
                                 corner(left + width, -1.0f, -0.01f, u, v, background),
                                 corner(left + width, 9.0f, -0.01f, u, v, background),
                                 corner(left - 1.0f, 9.0f, -0.01f, u, v, background)};
        quad(c, out);
    }
    const auto& r = rects_[static_cast<size_t>(font_texture_)];
    const auto cell_w = static_cast<float>(font_.cell_width);
    const auto cell_h = static_cast<float>(font_.cell_height);
    const float sheet_w = static_cast<float>(font_.sheet.width);
    const float sheet_h = static_cast<float>(font_.sheet.height);
    float pen = left;
    for (const char ch : text) {
        const auto code = static_cast<uint8_t>(ch);
        const float u0 = r[0] + static_cast<float>(code % 16) * cell_w / sheet_w * r[2];
        const float v0 = r[1] + static_cast<float>(code / 16) * cell_h / sheet_h * r[3];
        const float u1 = u0 + cell_w / sheet_w * r[2];
        const float v1 = v0 + cell_h / sheet_h * r[3];
        const float top = static_cast<float>(7 - static_cast<int>(font_.ascent));
        const FlatVertex c[4] = {corner(pen, top, 0.0f, u0, v0, text_colour),
                                 corner(pen + cell_w, top, 0.0f, u1, v0, text_colour),
                                 corner(pen + cell_w, top + cell_h, 0.0f, u1, v1, text_colour),
                                 corner(pen, top + cell_h, 0.0f, u0, v1, text_colour)};
        quad(c, out);
        pen += font_.advance[code];
    }
}

void EntityRenderer::add_shadow(const std::array<std::array<float, 5>, 4>& corners, float alpha) {
    if (shadow_texture_ < 0) return;
    const auto& r = rects_[static_cast<size_t>(shadow_texture_)];
    const auto a = static_cast<uint32_t>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f);
    FlatVertex c[4];
    for (int i = 0; i < 4; ++i) {
        c[i].position[0] = corners[i][0];
        c[i].position[1] = corners[i][1];
        c[i].position[2] = corners[i][2];
        c[i].uv[0] = r[0] + corners[i][3] * r[2];
        c[i].uv[1] = r[1] + corners[i][4] * r[3];
        c[i].colour = (a << 24) | 0xFFFFFFu;
        c[i].light = 0x100;
    }
    quad(c, shadows_);
}

void EntityRenderer::record(VkCommandBuffer command, const float view_projection[16],
                            const double eye[3], uint32_t width, uint32_t height,
                            const float fog[3], float fog_start, float fog_end) {
    if (set_ == VK_NULL_HANDLE || atlas_.image == VK_NULL_HANDLE) return;
    const size_t ring = frame_ % kFrames;

    // Instances grouped by render type and model, so each group is one draw.
    std::sort(queued_.begin(), queued_.end(),
              [](const Queued& a, const Queued& b) { return a.key < b.key; });
    auto* records = static_cast<GpuInstance*>(instances_.mapped) + ring * kMaxInstances;
    for (size_t i = 0; i < queued_.size(); ++i) {
        records[i] = {queued_[i].matrix_base, queued_[i].texture, queued_[i].light,
                      queued_[i].colour};
    }

    Push push{};
    // Positions arrive relative to the camera; take the camera's own
    // translation back out of the matrix.
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            push.view_projection[column * 4 + row] = view_projection[column * 4 + row];
        }
    }
    for (int row = 0; row < 4; ++row) {
        push.view_projection[12 + row] =
            view_projection[0 + row] * static_cast<float>(eye[0]) +
            view_projection[4 + row] * static_cast<float>(eye[1]) +
            view_projection[8 + row] * static_cast<float>(eye[2]) + view_projection[12 + row];
    }
    push.fog[0] = fog[0];
    push.fog[1] = fog[1];
    push.fog[2] = fog[2];
    push.fog[3] = fog_end;
    push.params[0] = fog_start;
    push.bases[0] = static_cast<uint32_t>(ring * kMaxMatrices);
    push.bases[1] = static_cast<uint32_t>(ring * kMaxInstances);

    const VkViewport viewport{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height),
                              0.0f, 1.0f};
    const VkRect2D scissor{{0, 0}, {width, height}};
    vkCmdSetViewport(command, 0, 1, &viewport);
    vkCmdSetScissor(command, 0, 1, &scissor);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &set_, 0,
                            nullptr);
    vkCmdPushConstants(command, layout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(push), &push);

    if (!queued_.empty() && vertices_.buffer != VK_NULL_HANDLE) {
        const VkDeviceSize zero = 0;
        vkCmdBindVertexBuffers(command, 0, 1, &vertices_.buffer, &zero);
        uint32_t bound = ~0u;
        size_t at = 0;
        while (at < queued_.size()) {
            const uint32_t key = queued_[at].key;
            size_t end = at;
            while (end < queued_.size() && queued_[end].key == key) ++end;
            const uint32_t type = key >> 24;
            if (type != bound) {
                vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, model_pipelines_[type]);
                bound = type;
            }
            const BakedModel& baked = baked_[key & 0xFFFFFFu];
            vkCmdDraw(command, baked.count, static_cast<uint32_t>(end - at), baked.first,
                      static_cast<uint32_t>(at));
            at = end;
        }
    }

    // Shadows, then see-through tags, then normal ones, from one ring.
    auto* flat = static_cast<FlatVertex*>(flat_.mapped) + ring * kMaxFlat;
    uint32_t used = 0;
    const VkBuffer flat_buffer = flat_.buffer;
    for (size_t pass = 0; pass < 3; ++pass) {
        const std::vector<FlatVertex>& list = pass == 0 ? shadows_ : pass == 1 ? see_through_ : tags_;
        const auto count = static_cast<uint32_t>(std::min<size_t>(list.size(), kMaxFlat - used));
        if (count == 0) continue;
        std::memcpy(flat + used, list.data(), count * sizeof(FlatVertex));
        const VkPipeline pipeline = pass == 0 ? flat_pipelines_[2]
                                    : pass == 1 ? flat_pipelines_[0]
                                                : flat_pipelines_[1];
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        const VkDeviceSize offset = (ring * kMaxFlat + used) * sizeof(FlatVertex);
        vkCmdBindVertexBuffers(command, 0, 1, &flat_buffer, &offset);
        vkCmdDraw(command, count, 1, 0, 0);
        used += count;
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

    VkDescriptorSetLayoutBinding bindings[5]{};
    for (uint32_t i = 0; i < 5; ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorCount = 1;
        bindings[i].descriptorType = i < 2 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
                                           : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[i].stageFlags = i < 2 ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT;
    }
    VkDescriptorSetLayoutCreateInfo set_layout{};
    set_layout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    set_layout.bindingCount = 5;
    set_layout.pBindings = bindings;
    check(vkCreateDescriptorSetLayout(device_.handle(), &set_layout, nullptr, &set_layout_),
          "vkCreateDescriptorSetLayout entity");

    const VkDescriptorPoolSize sizes[2] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2},
                                           {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3}};
    VkDescriptorPoolCreateInfo pool_create{};
    pool_create.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_create.maxSets = 1;
    pool_create.poolSizeCount = 2;
    pool_create.pPoolSizes = sizes;
    check(vkCreateDescriptorPool(device_.handle(), &pool_create, nullptr, &descriptor_pool_),
          "vkCreateDescriptorPool entity");
    VkDescriptorSetAllocateInfo set_allocate{};
    set_allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    set_allocate.descriptorPool = descriptor_pool_;
    set_allocate.descriptorSetCount = 1;
    set_allocate.pSetLayouts = &set_layout_;
    check(vkAllocateDescriptorSets(device_.handle(), &set_allocate, &set_),
          "vkAllocateDescriptorSets entity");

    VkPushConstantRange push{};
    push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    push.size = sizeof(Push);
    VkPipelineLayoutCreateInfo layout{};
    layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout.setLayoutCount = 1;
    layout.pSetLayouts = &set_layout_;
    layout.pushConstantRangeCount = 1;
    layout.pPushConstantRanges = &push;
    check(vkCreatePipelineLayout(device_.handle(), &layout, nullptr, &layout_),
          "vkCreatePipelineLayout entity");

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
    const auto build = [&](VkShaderModule vertex, VkShaderModule fragment,
                           const VkPipelineVertexInputStateCreateInfo& input, const Variant& v,
                           VkPipeline& out) {
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertex;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragment;
        stages[1].pName = "main";
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
        VkPipelineColorBlendAttachmentState attachment{};
        attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                    VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        if (v.blend) {
            attachment.blendEnable = VK_TRUE;
            attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            attachment.colorBlendOp = VK_BLEND_OP_ADD;
            attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            attachment.alphaBlendOp = VK_BLEND_OP_ADD;
        }
        VkPipelineColorBlendStateCreateInfo blend{};
        blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        blend.attachmentCount = 1;
        blend.pAttachments = &attachment;
        VkGraphicsPipelineCreateInfo create{};
        create.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        create.pNext = &rendering;
        create.stageCount = 2;
        create.pStages = stages;
        create.pVertexInputState = &input;
        create.pInputAssemblyState = &assembly;
        create.pViewportState = &viewport;
        create.pRasterizationState = &raster;
        create.pMultisampleState = &multisample;
        create.pDepthStencilState = &depth;
        create.pColorBlendState = &blend;
        create.pDynamicState = &dynamic;
        create.layout = layout_;
        check(vkCreateGraphicsPipelines(device_.handle(), device_.pipeline_cache(), 1, &create,
                                        nullptr, &out),
              "vkCreateGraphicsPipelines entity");
    };

    const VkVertexInputBindingDescription model_binding{0, sizeof(entity::Vertex),
                                                        VK_VERTEX_INPUT_RATE_VERTEX};
    const VkVertexInputAttributeDescription model_attributes[4] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(entity::Vertex, position)},
        {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(entity::Vertex, uv)},
        {2, 0, VK_FORMAT_R8G8B8A8_SNORM, offsetof(entity::Vertex, normal)},
        {3, 0, VK_FORMAT_R32_UINT, offsetof(entity::Vertex, part)},
    };
    VkPipelineVertexInputStateCreateInfo model_input{};
    model_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    model_input.vertexBindingDescriptionCount = 1;
    model_input.pVertexBindingDescriptions = &model_binding;
    model_input.vertexAttributeDescriptionCount = 4;
    model_input.pVertexAttributeDescriptions = model_attributes;

    const VkVertexInputBindingDescription flat_binding{0, sizeof(FlatVertex),
                                                       VK_VERTEX_INPUT_RATE_VERTEX};
    const VkVertexInputAttributeDescription flat_attributes[4] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(FlatVertex, position)},
        {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(FlatVertex, uv)},
        {2, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(FlatVertex, colour)},
        {3, 0, VK_FORMAT_R32_UINT, offsetof(FlatVertex, light)},
    };
    VkPipelineVertexInputStateCreateInfo flat_input{};
    flat_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    flat_input.vertexBindingDescriptionCount = 1;
    flat_input.pVertexBindingDescriptions = &flat_binding;
    flat_input.vertexAttributeDescriptionCount = 4;
    flat_input.pVertexAttributeDescriptions = flat_attributes;

    VkShaderModule model_vertex = make_module(device_.handle(), shaders::EntityVert());
    VkShaderModule model_fragment = make_module(device_.handle(), shaders::EntityFrag());
    VkShaderModule flat_vertex = make_module(device_.handle(), shaders::EntityFlatVert());
    VkShaderModule flat_fragment = make_module(device_.handle(), shaders::EntityFlatFrag());
    try {
        // In Type's order: cutout, cutout seen from both sides, translucent.
        build(model_vertex, model_fragment, model_input, {true, false, true, true},
              model_pipelines_[0]);
        build(model_vertex, model_fragment, model_input, {false, false, true, true},
              model_pipelines_[1]);
        build(model_vertex, model_fragment, model_input, {false, true, true, true},
              model_pipelines_[2]);
        build(flat_vertex, flat_fragment, flat_input, {false, true, false, false},
              flat_pipelines_[0]);
        build(flat_vertex, flat_fragment, flat_input, {false, true, true, false},
              flat_pipelines_[1]);
        build(flat_vertex, flat_fragment, flat_input, {false, true, true, false},
              flat_pipelines_[2]);
    } catch (...) {
        vkDestroyShaderModule(device_.handle(), model_vertex, nullptr);
        vkDestroyShaderModule(device_.handle(), model_fragment, nullptr);
        vkDestroyShaderModule(device_.handle(), flat_vertex, nullptr);
        vkDestroyShaderModule(device_.handle(), flat_fragment, nullptr);
        throw;
    }
    vkDestroyShaderModule(device_.handle(), model_vertex, nullptr);
    vkDestroyShaderModule(device_.handle(), model_fragment, nullptr);
    vkDestroyShaderModule(device_.handle(), flat_vertex, nullptr);
    vkDestroyShaderModule(device_.handle(), flat_fragment, nullptr);
}

void EntityRenderer::destroy() {
    for (VkPipeline& p : model_pipelines_) {
        if (p != VK_NULL_HANDLE) vkDestroyPipeline(device_.handle(), p, nullptr);
        p = VK_NULL_HANDLE;
    }
    for (VkPipeline& p : flat_pipelines_) {
        if (p != VK_NULL_HANDLE) vkDestroyPipeline(device_.handle(), p, nullptr);
        p = VK_NULL_HANDLE;
    }
    if (layout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(device_.handle(), layout_, nullptr);
    if (descriptor_pool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_.handle(), descriptor_pool_, nullptr);
    }
    if (set_layout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_.handle(), set_layout_, nullptr);
    }
    if (sampler_ != VK_NULL_HANDLE) vkDestroySampler(device_.handle(), sampler_, nullptr);
    layout_ = VK_NULL_HANDLE;
    descriptor_pool_ = VK_NULL_HANDLE;
    set_layout_ = VK_NULL_HANDLE;
    sampler_ = VK_NULL_HANDLE;
    set_ = VK_NULL_HANDLE;
    destroy_buffer(vertices_);
    destroy_buffer(matrices_);
    destroy_buffer(instances_);
    destroy_buffer(flat_);
    destroy_buffer(rects_buffer_);
    destroy_image(atlas_);
    destroy_image(skins_);
    if (pool_ != VK_NULL_HANDLE) vkDestroyCommandPool(device_.handle(), pool_, nullptr);
    pool_ = VK_NULL_HANDLE;
}

} // namespace mcpp::gpu
