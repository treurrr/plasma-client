#include "mcpp/gpu/world_renderer.hpp"

#include "mcpp/gpu/outline.hpp"
#include "mcpp/gpu/sky.hpp"

#include "mcpp/byte_buffer.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <atomic>
#include <thread>
#include <format>

#include <mcpp/gpu/shaders/WorldFrag.hpp>
#include <mcpp/gpu/shaders/WorldVert.hpp>

namespace mcpp::gpu {
namespace {

// Section data is written straight into mapped memory from several threads;
// every section owns a disjoint range, so no locking is needed.
template <typename Body>
void parallel_for(size_t count, Body&& body) {
    const size_t hardware = std::max<size_t>(1, std::thread::hardware_concurrency());
    const size_t workers = std::min(hardware, count);
    if (workers <= 1) {
        for (size_t i = 0; i < count; ++i) body(i);
        return;
    }
    std::atomic<size_t> next{0};
    std::vector<std::thread> threads;
    threads.reserve(workers);
    for (size_t w = 0; w < workers; ++w) {
        threads.emplace_back([&] {
            for (size_t i = next.fetch_add(1, std::memory_order_relaxed); i < count;
                 i = next.fetch_add(1, std::memory_order_relaxed)) {
                body(i);
            }
        });
    }
    for (auto& thread : threads) thread.join();
}


// Deliberately not an sRGB format. Minecraft multiplies its face shading,
// ambient occlusion and biome tints into sRGB bytes, so doing the same
// arithmetic on linear values gives a washed-out picture: a tinted block loses
// about 43% of its saturation and the sides stop reading as sides. Keeping the
// atlas and the target in the same gamma space as the game makes the shader's
// constants mean what they mean in the game, and spares the present blit a
// conversion it would not undo.
constexpr VkFormat kColorFormat = VK_FORMAT_R8G8B8A8_UNORM;

constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;

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

struct Mat4 {
    float m[16];
};

// Gribb-Hartmann: each clip-space boundary is a sum or difference of two rows
// of the combined matrix. Only the four sides are tested; near and far add
// nothing here and the far plane sits beyond anything we upload.
struct Frustum {
    float planes[4][4];

    explicit Frustum(const Mat4& m) {
        const auto row = [&](int r, int c) { return m.m[c * 4 + r]; };
        for (int i = 0; i < 4; ++i) {
            const int axis = i / 2;
            const float sign = (i % 2 == 0) ? 1.0f : -1.0f;
            for (int c = 0; c < 4; ++c) {
                planes[i][c] = row(3, c) + sign * row(axis, c);
            }
            const float length = std::sqrt(planes[i][0] * planes[i][0] +
                                           planes[i][1] * planes[i][1] +
                                           planes[i][2] * planes[i][2]);
            if (length > 0.0f) {
                for (int c = 0; c < 4; ++c) planes[i][c] /= length;
            }
        }
    }

    // A box is outside only when it lies entirely behind one plane, so the
    // corner nearest the plane decides.
    bool contains(const float low[3], const float high[3]) const {
        for (const auto& plane : planes) {
            const float nearest = plane[0] * (plane[0] >= 0.0f ? high[0] : low[0]) +
                                  plane[1] * (plane[1] >= 0.0f ? high[1] : low[1]) +
                                  plane[2] * (plane[2] >= 0.0f ? high[2] : low[2]) + plane[3];
            if (nearest < 0.0f) return false;
        }
        return true;
    }
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

    if (camera.roll_degrees != 0.0f) {
        const float angle = camera.roll_degrees * 3.14159265f / 180.0f;
        const float c = std::cos(angle);
        const float s = std::sin(angle);
        float rolled_right[3];
        float rolled_up[3];
        for (int i = 0; i < 3; ++i) {
            rolled_right[i] = right[i] * c + up[i] * s;
            rolled_up[i] = up[i] * c - right[i] * s;
        }
        for (int i = 0; i < 3; ++i) {
            right[i] = rolled_right[i];
            up[i] = rolled_up[i];
        }
    }

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

// Outside the anonymous namespace so a member function can take one.
struct PushConstants {
    Mat4 view_projection;
    float origin[4];   // the section's corner in world space
    float eye[4];      // camera in world space, w is where the fog starts
    float sky[4];      // what the fog fades into, w is where it finishes
};

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

    VkCommandBufferAllocateInfo reusable{};
    reusable.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    reusable.commandPool = pool_;
    reusable.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    reusable.commandBufferCount = 1;
    check(vkAllocateCommandBuffers(device_.handle(), &reusable, &frame_command_),
          "vkAllocateCommandBuffers");

    VkFenceCreateInfo fence_create{};
    fence_create.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    check(vkCreateFence(device_.handle(), &fence_create, nullptr, &frame_fence_), "vkCreateFence");

    VkQueryPoolCreateInfo query{};
    query.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    query.queryType = VK_QUERY_TYPE_TIMESTAMP;
    query.queryCount = 2;
    check(vkCreateQueryPool(device_.handle(), &query, nullptr, &timestamps_), "vkCreateQueryPool");

    // Reading this once rather than per section per frame: a getenv in the
    // draw loop is itself a measurable cost.
    culling_ = std::getenv("PLASMA_NOCULL") == nullptr;

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
        throw ProtocolError(std::format("atlas has {} layers, this gpu supports {}",
                                        atlas_layers_, limits.limits.maxImageArrayLayers));
    }

    VkImageCreateInfo create{};
    create.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    create.imageType = VK_IMAGE_TYPE_2D;
    create.format = kColorFormat;
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
    view.format = kColorFormat;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mips, 0, atlas_layers_};
    check(vkCreateImageView(device_.handle(), &view, nullptr, &atlas_view_),
          "vkCreateImageView atlas");

    // Nearest magnification keeps the blocky look up close; minification needs
    // mips and anisotropy or distant surfaces crawl with aliasing.
    VkSamplerCreateInfo sampler{};
    sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler.magFilter = VK_FILTER_NEAREST;
    // Nearest inside a mip level, linear only between levels, which is what
    // the game does. Filtering inside a level blends an opaque texel with its
    // transparent neighbour, and a transparent texel is black, so every cutout
    // texture - leaves, grass, every plant - comes out darkened at its edges.
    sampler.minFilter = VK_FILTER_NEAREST;
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

    // One colour per biome per tint, filled from the registry the server sends
    // and the game's own colour maps. A vertex carries the biome index, so a
    // swamp and a plains can share a buffer and a draw.
    VkBufferCreateInfo biome_create{};
    biome_create.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    biome_create.size = kBiomeSlots * 4 * sizeof(uint32_t);
    biome_create.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    check(vkCreateBuffer(device_.handle(), &biome_create, nullptr, &biome_buffer_),
          "vkCreateBuffer biomes");
    VkMemoryRequirements biome_requirements{};
    vkGetBufferMemoryRequirements(device_.handle(), biome_buffer_, &biome_requirements);
    VkMemoryAllocateInfo biome_allocate{};
    biome_allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    biome_allocate.allocationSize = biome_requirements.size;
    biome_allocate.memoryTypeIndex =
        memory_type(device_.physical(), biome_requirements.memoryTypeBits,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    check(vkAllocateMemory(device_.handle(), &biome_allocate, nullptr, &biome_memory_),
          "vkAllocateMemory biomes");
    check(vkBindBufferMemory(device_.handle(), biome_buffer_, biome_memory_, 0),
          "vkBindBufferMemory biomes");
    void* biome_mapped = nullptr;
    check(vkMapMemory(device_.handle(), biome_memory_, 0, VK_WHOLE_SIZE, 0, &biome_mapped),
          "vkMapMemory biomes");
    biome_colours_ = static_cast<uint32_t*>(biome_mapped);
    // Plains until the server says otherwise, which is what a world with no
    // biome registry - a showcase, a test - should look like.
    for (uint32_t i = 0; i < kBiomeSlots; ++i) {
        biome_colours_[i * 4 + 0] = 0x91BD59u;
        biome_colours_[i * 4 + 1] = 0x77AB2Fu;
        biome_colours_[i * 4 + 2] = 0x3F76E4u;
        biome_colours_[i * 4 + 3] = 0x8F7A36u;
    }

    VkDescriptorSetLayoutBinding bindings[2]{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo layout{};
    layout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout.bindingCount = 2;
    layout.pBindings = bindings;
    check(vkCreateDescriptorSetLayout(device_.handle(), &layout, nullptr, &set_layout_),
          "vkCreateDescriptorSetLayout");

    VkDescriptorPoolSize sizes[2]{};
    sizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sizes[0].descriptorCount = 1;
    sizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    sizes[1].descriptorCount = 1;
    VkDescriptorPoolCreateInfo pool_create{};
    pool_create.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_create.maxSets = 1;
    pool_create.poolSizeCount = 2;
    pool_create.pPoolSizes = sizes;
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
    VkDescriptorBufferInfo buffer_info{};
    buffer_info.buffer = biome_buffer_;
    buffer_info.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet writes[2]{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = descriptor_set_;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].pImageInfo = &image_info;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = descriptor_set_;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[1].pBufferInfo = &buffer_info;
    vkUpdateDescriptorSets(device_.handle(), 2, writes, 0, nullptr);
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
    push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
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

    // Position and the packed flags share one 16-bit vector; uv arrives already
    // normalised to 0..1 because the format says UNORM.
    VkVertexInputAttributeDescription attributes[3]{};
    attributes[0] = {0, 0, VK_FORMAT_R16G16B16A16_UINT, offsetof(world::Vertex, position)};
    attributes[1] = {1, 0, VK_FORMAT_R16G16_UNORM, offsetof(world::Vertex, uv)};
    attributes[2] = {2, 0, VK_FORMAT_R16G16_UINT, offsetof(world::Vertex, texture)};

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
    raster.cullMode = VK_CULL_MODE_BACK_BIT;
    raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth.depthTestEnable = VK_TRUE;
    depth.depthWriteEnable = VK_TRUE;
    // Reversed depth, and equal passes: grass blocks draw their tinted overlay
    // as a second element on exactly the same plane as the first, and with a
    // strict test that overlay never wins.
    depth.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
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

    VkResult result = vkCreateGraphicsPipelines(device_.handle(), device_.pipeline_cache(), 1,
                                                &create, nullptr, &pipeline_);
    if (result == VK_SUCCESS) {
        // Water and glass: blended, and writing no depth, or the surface would
        // hide whatever is behind it in the same pass.
        blend_attachment.blendEnable = VK_TRUE;
        blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
        blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
        depth.depthWriteEnable = VK_FALSE;
        result = vkCreateGraphicsPipelines(device_.handle(), device_.pipeline_cache(), 1, &create,
                                           nullptr, &blended_pipeline_);
    }
    vkDestroyShaderModule(device_.handle(), vertex, nullptr);
    vkDestroyShaderModule(device_.handle(), fragment, nullptr);
    check(result, "vkCreateGraphicsPipelines");
}

void WorldRenderer::upload(const std::vector<world::SectionMesh>& sections) {
    const auto start = std::chrono::steady_clock::now();
    destroy_mesh();
    sections_.clear();

    // Work out every section's place in the buffer first, then let the threads
    // write straight into mapped memory. Building a merged copy on the way was
    // costing more than the transfer itself.
    sections_.reserve(sections.size());
    std::vector<size_t> source;
    source.reserve(sections.size());
    size_t vertex_total = 0;
    size_t index_total = 0;
    for (size_t i = 0; i < sections.size(); ++i) {
        const auto& section = sections[i];
        if (section.indices.empty()) continue;

        Section entry{};
        for (int axis = 0; axis < 3; ++axis) {
            entry.origin[axis] = static_cast<float>(section.origin[axis]);
            entry.bounds_min[axis] = entry.origin[axis] + section.bounds_min[axis];
            entry.bounds_max[axis] = entry.origin[axis] + section.bounds_max[axis];
        }
        entry.first_index = static_cast<uint32_t>(index_total);
        entry.index_count = static_cast<uint32_t>(section.indices.size());
        entry.opaque_count = static_cast<uint32_t>(section.opaque_indices);
        entry.vertex_offset = static_cast<int32_t>(vertex_total);
        sections_.push_back(entry);
        source.push_back(i);

        vertex_total += section.vertices.size();
        index_total += section.indices.size();
    }

    vertex_bytes_ = vertex_total * sizeof(world::Vertex);
    index_bytes_ = index_total * sizeof(uint32_t);
    index_count_ = static_cast<uint32_t>(index_total);
    if (index_count_ > 0) {
        upload_sections(sections, source);
    }

    upload_ms_ = std::chrono::duration<double, std::milli>(
                     std::chrono::steady_clock::now() - start).count();
}

void WorldRenderer::reserve(size_t vertex_capacity, size_t index_capacity) {
    destroy_mesh();
    sections_.clear();
    vertex_bytes_ = 0;
    index_bytes_ = 0;
    index_count_ = 0;
    vertex_capacity_ = vertex_capacity;
    index_capacity_ = index_capacity;

    auto vertex = make_buffer(device_, vertex_capacity_,
                              VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    auto index = make_buffer(device_, index_capacity_,
                             VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vertex_buffer_ = vertex.buffer;
    vertex_memory_ = vertex.memory;
    index_buffer_ = index.buffer;
    index_memory_ = index.memory;
}

void WorldRenderer::forget(int32_t chunk_x, int32_t chunk_z) {
    const auto x = static_cast<float>(chunk_x * 16);
    const auto z = static_cast<float>(chunk_z * 16);
    for (auto& entry : sections_) {
        if (entry.origin[0] == x && entry.origin[2] == z) {
            entry.index_count = 0;
            entry.opaque_count = 0;
        }
    }
}

bool WorldRenderer::replace(const std::vector<world::SectionMesh>& sections) {
    // Whatever stood at these origins stops being drawn first, so a section
    // that meshes to nothing - the last block in it broken - disappears.
    for (const auto& section : sections) {
        for (auto& entry : sections_) {
            if (static_cast<int32_t>(entry.origin[0]) == section.origin[0] &&
                static_cast<int32_t>(entry.origin[1]) == section.origin[1] &&
                static_cast<int32_t>(entry.origin[2]) == section.origin[2]) {
                entry.index_count = 0;
                entry.opaque_count = 0;
            }
        }
    }
    return append(sections);
}

bool WorldRenderer::append(const std::vector<world::SectionMesh>& sections) {
    if (vertex_capacity_ == 0) return false;

    const size_t first_section = sections_.size();
    const size_t vertex_start = vertex_bytes_;
    const size_t index_start = index_bytes_;

    std::vector<size_t> source;
    source.reserve(sections.size());
    size_t vertex_added = 0;
    size_t index_added = 0;
    for (size_t i = 0; i < sections.size(); ++i) {
        const auto& section = sections[i];
        if (section.indices.empty()) continue;

        const size_t vertex_offset = (vertex_start + vertex_added) / sizeof(world::Vertex);
        const size_t index_offset = (index_start + index_added) / sizeof(uint32_t);

        Section entry{};
        for (int axis = 0; axis < 3; ++axis) {
            entry.origin[axis] = static_cast<float>(section.origin[axis]);
            entry.bounds_min[axis] = entry.origin[axis] + section.bounds_min[axis];
            entry.bounds_max[axis] = entry.origin[axis] + section.bounds_max[axis];
        }
        entry.first_index = static_cast<uint32_t>(index_offset);
        entry.index_count = static_cast<uint32_t>(section.indices.size());
        entry.opaque_count = static_cast<uint32_t>(section.opaque_indices);
        entry.vertex_offset = static_cast<int32_t>(vertex_offset);
        sections_.push_back(entry);
        source.push_back(i);

        vertex_added += section.vertices.size() * sizeof(world::Vertex);
        index_added += section.indices.size() * sizeof(uint32_t);
    }

    if (source.empty()) return true;
    if (vertex_start + vertex_added > vertex_capacity_ ||
        index_start + index_added > index_capacity_) {
        sections_.resize(first_section);   // out of room; the caller decides what to drop
        out_of_vertex_room_ = vertex_start + vertex_added > vertex_capacity_;
        return false;
    }

    const size_t needed = vertex_added + index_added;
    if (needed > append_staging_bytes_) {
        if (append_staging_ != VK_NULL_HANDLE) {
            vkUnmapMemory(device_.handle(), append_staging_memory_);
            vkDestroyBuffer(device_.handle(), append_staging_, nullptr);
            vkFreeMemory(device_.handle(), append_staging_memory_, nullptr);
        }
        append_staging_bytes_ = std::max<size_t>(needed, 8u << 20);
        auto staging = make_buffer(device_, append_staging_bytes_,
                                   VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        append_staging_ = staging.buffer;
        append_staging_memory_ = staging.memory;
        check(vkMapMemory(device_.handle(), append_staging_memory_, 0, VK_WHOLE_SIZE, 0,
                          &append_mapped_),
              "vkMapMemory");
    }

    auto* base = static_cast<uint8_t*>(append_mapped_);
    parallel_for(source.size(), [&](size_t slot) {
        const auto& section = sections[source[slot]];
        const auto& entry = sections_[first_section + slot];
        const size_t vertex_offset =
            static_cast<size_t>(entry.vertex_offset) * sizeof(world::Vertex) - vertex_start;
        const size_t index_offset =
            static_cast<size_t>(entry.first_index) * sizeof(uint32_t) - index_start;
        std::memcpy(base + vertex_offset, section.vertices.data(),
                    section.vertices.size() * sizeof(world::Vertex));
        std::memcpy(base + vertex_added + index_offset, section.indices.data(),
                    section.indices.size() * sizeof(uint32_t));
    });

    check(vkResetCommandBuffer(frame_command_, 0), "vkResetCommandBuffer");
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(frame_command_, &begin), "vkBeginCommandBuffer");
    VkBufferCopy vertex_copy{0, vertex_start, vertex_added};
    vkCmdCopyBuffer(frame_command_, append_staging_, vertex_buffer_, 1, &vertex_copy);
    VkBufferCopy index_copy{vertex_added, index_start, index_added};
    vkCmdCopyBuffer(frame_command_, append_staging_, index_buffer_, 1, &index_copy);
    check(vkEndCommandBuffer(frame_command_), "vkEndCommandBuffer");

    VkCommandBufferSubmitInfo command_submit{};
    command_submit.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    command_submit.commandBuffer = frame_command_;
    VkSubmitInfo2 submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &command_submit;
    check(vkResetFences(device_.handle(), 1, &frame_fence_), "vkResetFences");
    check(vkQueueSubmit2(device_.graphics_queue(), 1, &submit, frame_fence_), "vkQueueSubmit2");
    check(vkWaitForFences(device_.handle(), 1, &frame_fence_, VK_TRUE, UINT64_MAX),
          "vkWaitForFences");

    vertex_bytes_ += vertex_added;
    index_bytes_ += index_added;
    index_count_ = static_cast<uint32_t>(index_bytes_ / sizeof(uint32_t));
    return true;
}

void WorldRenderer::upload(const world::Mesh& mesh) {
    sections_.clear();
    const auto start = std::chrono::steady_clock::now();
    destroy_mesh();
    upload_merged(mesh);
    upload_ms_ = std::chrono::duration<double, std::milli>(
                     std::chrono::steady_clock::now() - start).count();
}

void WorldRenderer::upload_sections(const std::vector<world::SectionMesh>& sections,
                                    const std::vector<size_t>& order) {
    const auto t0 = std::chrono::steady_clock::now();
    auto staging = make_buffer(device_, vertex_bytes_ + index_bytes_,
                               VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    const auto t1 = std::chrono::steady_clock::now();

    void* mapped = nullptr;
    check(vkMapMemory(device_.handle(), staging.memory, 0, VK_WHOLE_SIZE, 0, &mapped),
          "vkMapMemory");
    auto* base = static_cast<uint8_t*>(mapped);

    parallel_for(order.size(), [&](size_t slot) {
        const auto& section = sections[order[slot]];
        const auto& entry = sections_[slot];
        std::memcpy(base + static_cast<size_t>(entry.vertex_offset) * sizeof(world::Vertex),
                    section.vertices.data(), section.vertices.size() * sizeof(world::Vertex));
        std::memcpy(base + vertex_bytes_ + static_cast<size_t>(entry.first_index) * sizeof(uint32_t),
                    section.indices.data(), section.indices.size() * sizeof(uint32_t));
    });

    vkUnmapMemory(device_.handle(), staging.memory);
    const auto t2 = std::chrono::steady_clock::now();

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
    const auto t3 = std::chrono::steady_clock::now();

    vkFreeCommandBuffers(device_.handle(), pool_, 1, &command);
    vkDestroyBuffer(device_.handle(), staging.buffer, nullptr);
    vkFreeMemory(device_.handle(), staging.memory, nullptr);

    const auto ms = [](auto a, auto b) {
        return std::chrono::duration<double, std::milli>(b - a).count();
    };
    upload_alloc_ms_ = ms(t0, t1);
    upload_copy_ms_ = ms(t1, t2);
    upload_gpu_ms_ = ms(t2, t3);
}

void WorldRenderer::upload_merged(const world::Mesh& mesh) {
    vertex_bytes_ = mesh.vertices.size() * sizeof(world::Vertex);
    index_bytes_ = mesh.indices.size() * sizeof(uint32_t);
    index_count_ = static_cast<uint32_t>(mesh.indices.size());
    opaque_index_count_ = static_cast<uint32_t>(mesh.opaque_indices);
    if (index_count_ == 0) return;

    const auto t0 = std::chrono::steady_clock::now();
    auto staging = make_buffer(device_, vertex_bytes_ + index_bytes_,
                               VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    const auto t1 = std::chrono::steady_clock::now();
    void* mapped = nullptr;
    check(vkMapMemory(device_.handle(), staging.memory, 0, VK_WHOLE_SIZE, 0, &mapped),
          "vkMapMemory");
    std::memcpy(mapped, mesh.vertices.data(), vertex_bytes_);
    std::memcpy(static_cast<uint8_t*>(mapped) + vertex_bytes_, mesh.indices.data(), index_bytes_);
    vkUnmapMemory(device_.handle(), staging.memory);
    const auto t2 = std::chrono::steady_clock::now();

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

    const auto t3 = std::chrono::steady_clock::now();

    vkFreeCommandBuffers(device_.handle(), pool_, 1, &command);
    vkDestroyBuffer(device_.handle(), staging.buffer, nullptr);
    vkFreeMemory(device_.handle(), staging.memory, nullptr);

    const auto ms = [](auto a, auto b) {
        return std::chrono::duration<double, std::milli>(b - a).count();
    };
    upload_alloc_ms_ = ms(t0, t1);
    upload_copy_ms_ = ms(t1, t2);
    upload_gpu_ms_ = ms(t2, t3);
}

// Two passes over the same buffers: everything solid first, then whatever has
// to be blended, drawn from the back forwards so the water in front does not
// erase the water behind it.
void WorldRenderer::draw_geometry(VkCommandBuffer command, PushConstants& push) {
    const auto send = [&](const float origin[3]) {
        push.origin[0] = origin[0];
        push.origin[1] = origin[1];
        push.origin[2] = origin[2];
        vkCmdPushConstants(command, layout_,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(push), &push);
    };

    drawn_sections_ = 0;
    drawn_triangles_ = 0;

    if (sections_.empty()) {
        const float none[3] = {0.0f, 0.0f, 0.0f};
        send(none);
        if (opaque_index_count_ > 0) {
            vkCmdDrawIndexed(command, opaque_index_count_, 1, 0, 0, 0);
        }
        if (index_count_ > opaque_index_count_) {
            vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, blended_pipeline_);
            vkCmdDrawIndexed(command, index_count_ - opaque_index_count_, 1,
                             opaque_index_count_, 0, 0);
            vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
        }
        drawn_triangles_ = index_count_ / 3;
        return;
    }

    const Frustum frustum(push.view_projection);
    visible_.clear();
    for (const auto& section : sections_) {
        if (culling_ && !frustum.contains(section.bounds_min, section.bounds_max)) continue;
        visible_.push_back(&section);
        if (section.opaque_count == 0) continue;
        send(section.origin);
        vkCmdDrawIndexed(command, section.opaque_count, 1, section.first_index,
                         section.vertex_offset, 0);
        ++drawn_sections_;
        drawn_triangles_ += section.opaque_count / 3;
    }

    bool any_blended = false;
    for (const auto* section : visible_) {
        if (section->index_count > section->opaque_count) {
            any_blended = true;
            break;
        }
    }
    if (!any_blended) return;

    const float eye[3] = {push.eye[0], push.eye[1], push.eye[2]};
    std::ranges::sort(visible_, [&](const Section* a, const Section* b) {
        const auto distance = [&](const Section* s) {
            const float dx = (s->bounds_min[0] + s->bounds_max[0]) * 0.5f - eye[0];
            const float dy = (s->bounds_min[1] + s->bounds_max[1]) * 0.5f - eye[1];
            const float dz = (s->bounds_min[2] + s->bounds_max[2]) * 0.5f - eye[2];
            return dx * dx + dy * dy + dz * dz;
        };
        return distance(a) > distance(b);
    });

    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, blended_pipeline_);
    for (const auto* section : visible_) {
        const uint32_t count = section->index_count - section->opaque_count;
        if (count == 0) continue;
        send(section->origin);
        vkCmdDrawIndexed(command, count, 1, section->first_index + section->opaque_count,
                         section->vertex_offset, 0);
        drawn_triangles_ += count / 3;
    }
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
}

void WorldRenderer::record_frame(VkCommandBuffer command, const Camera& camera, bool readback) {
    record_frame(command, camera, readback, false);
}

void WorldRenderer::record_frame(VkCommandBuffer command, const Camera& camera, bool readback,
                                 bool leave_transfer_src) {
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
    color.clearValue.color = {{fog_[0], fog_[1], fog_[2], 1.0f}};

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
    if (sky_ != nullptr) sky_->record(command, camera, width_, height_, zenith_, fog_);
    if (index_count_ > 0) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
        const VkViewport viewport{0.0f, 0.0f, static_cast<float>(width_),
                                  static_cast<float>(height_), 0.0f, 1.0f};
        const VkRect2D scissor{{0, 0}, {width_, height_}};
        vkCmdSetViewport(command, 0, 1, &viewport);
        vkCmdSetScissor(command, 0, 1, &scissor);

        PushConstants push{};
        push.eye[0] = camera.eye[0];
        push.eye[1] = camera.eye[1];
        push.eye[2] = camera.eye[2];
        push.eye[3] = camera.fog_start;
        push.sky[0] = fog_[0];
        push.sky[1] = fog_[1];
        push.sky[2] = fog_[2];
        push.sky[3] = camera.fog_end;
        push.view_projection = view_projection(camera, static_cast<float>(width_) /
                                                           static_cast<float>(height_));
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1,
                                &descriptor_set_, 0, nullptr);

        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(command, 0, 1, &vertex_buffer_, &offset);
        vkCmdBindIndexBuffer(command, index_buffer_, 0, VK_INDEX_TYPE_UINT32);

        draw_geometry(command, push);
        if (outline_ != nullptr && outlined_present_) {
            outline_->record(command, push.view_projection.m, outlined_);
        }
    }
    vkCmdEndRendering(command);

    if (readback || leave_transfer_src) {
        transition(command, color_, VK_IMAGE_ASPECT_COLOR_BIT,
                   VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                   VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT,
                   VK_ACCESS_2_TRANSFER_READ_BIT);

        if (readback) {
            VkBufferImageCopy region{};
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.imageExtent = {width_, height_, 1};
            vkCmdCopyImageToBuffer(command, color_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback_,
                                   1, &region);
        }
    }

    vkCmdWriteTimestamp2(command, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, timestamps_, 1);
}

void WorldRenderer::record_present(VkCommandBuffer command, const Camera& camera) {
    record_frame(command, camera, false, true);
}

void WorldRenderer::read_timestamps() {
    uint64_t marks[2] = {0, 0};
    if (vkGetQueryPoolResults(device_.handle(), timestamps_, 0, 2, sizeof(marks), marks,
                              sizeof(uint64_t),
                              VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT) == VK_SUCCESS) {
        last_gpu_ms_ = static_cast<double>(marks[1] - marks[0]) * timestamp_period_ / 1e6;
    }
}

double WorldRenderer::submit(VkCommandBuffer command) {
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
    vkDestroyFence(device_.handle(), fence, nullptr);

    uint64_t marks[2] = {0, 0};
    if (vkGetQueryPoolResults(device_.handle(), timestamps_, 0, 2, sizeof(marks), marks,
                              sizeof(uint64_t),
                              VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT) == VK_SUCCESS) {
        last_gpu_ms_ = static_cast<double>(marks[1] - marks[0]) * timestamp_period_ / 1e6;
    }
    return last_gpu_ms_;
}

double WorldRenderer::draw(const Camera& camera) {
    check(vkResetCommandBuffer(frame_command_, 0), "vkResetCommandBuffer");

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(frame_command_, &begin), "vkBeginCommandBuffer");
    record_frame(frame_command_, camera, false);
    check(vkEndCommandBuffer(frame_command_), "vkEndCommandBuffer");

    VkCommandBufferSubmitInfo command_submit{};
    command_submit.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    command_submit.commandBuffer = frame_command_;
    VkSubmitInfo2 submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &command_submit;

    check(vkResetFences(device_.handle(), 1, &frame_fence_), "vkResetFences");
    check(vkQueueSubmit2(device_.graphics_queue(), 1, &submit, frame_fence_), "vkQueueSubmit2");
    check(vkWaitForFences(device_.handle(), 1, &frame_fence_, VK_TRUE, UINT64_MAX),
          "vkWaitForFences");
    read_timestamps();
    return last_gpu_ms_;
}

void WorldRenderer::set_sky_colours(uint32_t zenith, uint32_t fog) {
    const auto unpack = [](uint32_t packed, float out[3]) {
        out[0] = static_cast<float>((packed >> 16) & 0xFF) / 255.0f;
        out[1] = static_cast<float>((packed >> 8) & 0xFF) / 255.0f;
        out[2] = static_cast<float>(packed & 0xFF) / 255.0f;
    };
    unpack(zenith, zenith_);
    unpack(fog, fog_);
}

void WorldRenderer::set_biome_colours(std::span<const uint32_t> colours) {
    if (biome_colours_ == nullptr) return;
    const size_t slots = std::min<size_t>(colours.size() / 4, kBiomeSlots);
    for (size_t i = 0; i < slots; ++i) {
        for (size_t channel = 0; channel < 4; ++channel) {
            biome_colours_[i * 4 + channel] = colours[i * 4 + channel];
        }
    }
}

VkFormat WorldRenderer::colour_format() const { return kColorFormat; }

std::vector<uint8_t> WorldRenderer::render(const Camera& camera, const Overlay& overlay) {
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
    color.clearValue.color = {{fog_[0], fog_[1], fog_[2], 1.0f}};

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
    if (sky_ != nullptr) sky_->record(command, camera, width_, height_, zenith_, fog_);
    if (index_count_ > 0) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
        const VkViewport viewport{0.0f, 0.0f, static_cast<float>(width_),
                                  static_cast<float>(height_), 0.0f, 1.0f};
        const VkRect2D scissor{{0, 0}, {width_, height_}};
        vkCmdSetViewport(command, 0, 1, &viewport);
        vkCmdSetScissor(command, 0, 1, &scissor);

        PushConstants push{};
        push.eye[0] = camera.eye[0];
        push.eye[1] = camera.eye[1];
        push.eye[2] = camera.eye[2];
        push.eye[3] = camera.fog_start;
        push.sky[0] = fog_[0];
        push.sky[1] = fog_[1];
        push.sky[2] = fog_[2];
        push.sky[3] = camera.fog_end;
        push.view_projection = view_projection(camera, static_cast<float>(width_) /
                                                           static_cast<float>(height_));
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1,
                                &descriptor_set_, 0, nullptr);

        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(command, 0, 1, &vertex_buffer_, &offset);
        vkCmdBindIndexBuffer(command, index_buffer_, 0, VK_INDEX_TYPE_UINT32);

        draw_geometry(command, push);
        if (outline_ != nullptr && outlined_present_) {
            outline_->record(command, push.view_projection.m, outlined_);
        }
    }
    vkCmdEndRendering(command);

    if (overlay) {
        VkMemoryBarrier2 wait{};
        wait.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
        wait.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        wait.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        wait.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        wait.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
                             VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        VkDependencyInfo dependency{};
        dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependency.memoryBarrierCount = 1;
        dependency.pMemoryBarriers = &wait;
        vkCmdPipelineBarrier2(command, &dependency);

        VkRenderingAttachmentInfo over{};
        over.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        over.imageView = color_view_;
        over.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        over.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        over.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

        VkRenderingInfo pass{};
        pass.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        pass.renderArea = {{0, 0}, {width_, height_}};
        pass.layerCount = 1;
        pass.colorAttachmentCount = 1;
        pass.pColorAttachments = &over;
        vkCmdBeginRendering(command, &pass);
        overlay(command, width_, height_);
        vkCmdEndRendering(command);
    }

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
    if (biome_colours_ != nullptr) vkUnmapMemory(device_.handle(), biome_memory_);
    vkDestroyBuffer(device_.handle(), biome_buffer_, nullptr);
    vkFreeMemory(device_.handle(), biome_memory_, nullptr);
    vkDestroyPipeline(device_.handle(), pipeline_, nullptr);
    vkDestroyPipeline(device_.handle(), blended_pipeline_, nullptr);
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
    vkDestroyFence(device_.handle(), frame_fence_, nullptr);
    vkDestroyCommandPool(device_.handle(), pool_, nullptr);
}

} // namespace mcpp::gpu
