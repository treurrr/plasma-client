#include "mcpp/gpu/blur.hpp"
#include "mcpp/gpu/world_renderer.hpp"

#include "vk_util.hpp"

#include <mcpp/gpu/shaders/BlurFrag.hpp>
#include <mcpp/gpu/shaders/BlurVert.hpp>

namespace mcpp::gpu {
namespace {

struct BlurPush {
    float texel[2];
    float direction[2];
    float radius;
};

void barrier(VkCommandBuffer command, VkImage image, VkImageLayout from, VkImageLayout to,
             VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
             VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access) {
    VkImageMemoryBarrier2 b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    b.srcStageMask = src_stage;
    b.srcAccessMask = src_access;
    b.dstStageMask = dst_stage;
    b.dstAccessMask = dst_access;
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &b;
    vkCmdPipelineBarrier2(command, &dependency);
}

constexpr VkPipelineStageFlags2 kDraw = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
constexpr VkPipelineStageFlags2 kRead = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
constexpr VkAccessFlags2 kWrite = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
constexpr VkAccessFlags2 kSample = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;

} // namespace

BoxBlur::BoxBlur(Device& device, VkFormat format) : device_(device), format_(format) {
    const VkDevice handle = device_.handle();
    // The game's post chain reads its inputs with linear filtering, clamped.
    sampler_ = vk::sampler(handle, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

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
              "vkCreateDescriptorSetLayout blur");

    VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2};
    VkDescriptorPoolCreateInfo pool{};
    pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool.maxSets = 2;
    pool.poolSizeCount = 1;
    pool.pPoolSizes = &size;
    vk::check(vkCreateDescriptorPool(handle, &pool, nullptr, &pool_), "vkCreateDescriptorPool blur");
    const VkDescriptorSetLayout layouts[2] = {set_layout_, set_layout_};
    VkDescriptorSetAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocate.descriptorPool = pool_;
    allocate.descriptorSetCount = 2;
    allocate.pSetLayouts = layouts;
    VkDescriptorSet sets[2]{};
    vk::check(vkAllocateDescriptorSets(handle, &allocate, sets), "vkAllocateDescriptorSets blur");
    from_target_ = sets[0];
    from_scratch_ = sets[1];

    VkPushConstantRange push{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(BlurPush)};
    VkPipelineLayoutCreateInfo layout{};
    layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout.setLayoutCount = 1;
    layout.pSetLayouts = &set_layout_;
    layout.pushConstantRangeCount = 1;
    layout.pPushConstantRanges = &push;
    vk::check(vkCreatePipelineLayout(handle, &layout, nullptr, &layout_),
              "vkCreatePipelineLayout blur");

    vk::PipelineDesc desc;
    desc.vertex = shaders::BlurVert();
    desc.fragment = shaders::BlurFrag();
    desc.layout = layout_;
    desc.depth_test = false;
    desc.colour_format = format_;
    desc.depth_format = VK_FORMAT_UNDEFINED;
    pipeline_ = vk::pipeline(device_, desc);
}

BoxBlur::~BoxBlur() {
    if (device_.handle() == VK_NULL_HANDLE) return;
    vkDeviceWaitIdle(device_.handle());
    destroy_scratch();
    vkDestroyPipeline(device_.handle(), pipeline_, nullptr);
    vkDestroyPipelineLayout(device_.handle(), layout_, nullptr);
    vkDestroyDescriptorPool(device_.handle(), pool_, nullptr);
    vkDestroyDescriptorSetLayout(device_.handle(), set_layout_, nullptr);
    vkDestroySampler(device_.handle(), sampler_, nullptr);
}

void BoxBlur::destroy_scratch() {
    vkDestroyImageView(device_.handle(), scratch_view_, nullptr);
    vkDestroyImage(device_.handle(), scratch_, nullptr);
    vkFreeMemory(device_.handle(), scratch_memory_, nullptr);
    scratch_view_ = VK_NULL_HANDLE;
    scratch_ = VK_NULL_HANDLE;
    scratch_memory_ = VK_NULL_HANDLE;
}

void BoxBlur::ensure_scratch(uint32_t width, uint32_t height) {
    if (scratch_ != VK_NULL_HANDLE && width == scratch_width_ && height == scratch_height_) return;
    if (scratch_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_.handle());
        destroy_scratch();
    }
    VkImageCreateInfo create{};
    create.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    create.imageType = VK_IMAGE_TYPE_2D;
    create.format = format_;
    create.extent = {width, height, 1};
    create.mipLevels = 1;
    create.arrayLayers = 1;
    create.samples = VK_SAMPLE_COUNT_1_BIT;
    create.tiling = VK_IMAGE_TILING_OPTIMAL;
    create.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    create.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    vk::check(vkCreateImage(device_.handle(), &create, nullptr, &scratch_), "vkCreateImage blur");
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device_.handle(), scratch_, &requirements);
    VkMemoryAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = vk::memory_type(device_.physical(), requirements.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vk::check(vkAllocateMemory(device_.handle(), &allocate, nullptr, &scratch_memory_),
              "vkAllocateMemory blur");
    vk::check(vkBindImageMemory(device_.handle(), scratch_, scratch_memory_, 0),
              "vkBindImageMemory blur");
    VkImageViewCreateInfo view{};
    view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image = scratch_;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = format_;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vk::check(vkCreateImageView(device_.handle(), &view, nullptr, &scratch_view_),
              "vkCreateImageView blur");
    scratch_width_ = width;
    scratch_height_ = height;
    point_at(from_scratch_, scratch_view_);
}

void BoxBlur::point_at(VkDescriptorSet set, VkImageView view) {
    VkDescriptorImageInfo image{sampler_, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &image;
    vkUpdateDescriptorSets(device_.handle(), 1, &write, 0, nullptr);
}

void BoxBlur::pass(VkCommandBuffer command, VkImageView target, VkDescriptorSet source,
                   uint32_t width, uint32_t height, float dx, float dy, int radius) {
    VkRenderingAttachmentInfo colour{};
    colour.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colour.imageView = target;
    colour.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colour.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colour.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering.renderArea = {{0, 0}, {width, height}};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &colour;
    vkCmdBeginRendering(command, &rendering);
    const VkViewport viewport{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height),
                              0.0f, 1.0f};
    const VkRect2D scissor{{0, 0}, {width, height}};
    vkCmdSetViewport(command, 0, 1, &viewport);
    vkCmdSetScissor(command, 0, 1, &scissor);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &source, 0,
                            nullptr);
    const BlurPush push{{1.0f / static_cast<float>(width), 1.0f / static_cast<float>(height)},
                        {dx, dy},
                        static_cast<float>(radius)};
    vkCmdPushConstants(command, layout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
    vkCmdDraw(command, 3, 1, 0, 0);
    vkCmdEndRendering(command);
}

// `GameRenderer.processBlurEffect`: the blur chain, three times across then down at the menu blur radius.
void BoxBlur::record(VkCommandBuffer command, VkImage image, VkImageView view, uint32_t width,
                     uint32_t height, int radius) {
    if (radius < 1 || width == 0 || height == 0) return;
    ensure_scratch(width, height);
    if (view != target_view_) {
        point_at(from_target_, view);
        target_view_ = view;
    }
    for (int round = 0; round < 3; ++round) {
        barrier(command, image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, kDraw, kWrite, kRead, kSample);
        barrier(command, scratch_, VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, kRead, 0, kDraw, kWrite);
        pass(command, scratch_view_, from_target_, width, height, 1.0f, 0.0f, radius);
        barrier(command, scratch_, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, kDraw, kWrite, kRead, kSample);
        barrier(command, image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, kRead, 0, kDraw, kWrite);
        pass(command, view, from_scratch_, width, height, 0.0f, 1.0f, radius);
    }
}

void WorldRenderer::set_backdrop(Overlay under, int radius) {
    backdrop_ = std::move(under);
    blur_radius_ = radius;
}

void WorldRenderer::record_backdrop(VkCommandBuffer command) {
    if (!backdrop_) return;
    VkMemoryBarrier2 wait{};
    wait.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    wait.srcStageMask = kDraw;
    wait.srcAccessMask = kWrite;
    wait.dstStageMask = kDraw;
    wait.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | kWrite;
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
    backdrop_(command, width_, height_);
    vkCmdEndRendering(command);

    if (blur_radius_ < 1) return;
    if (!blur_) blur_ = std::make_unique<BoxBlur>(device_, colour_format());
    blur_->record(command, color_, color_view_, width_, height_, blur_radius_);
}

} // namespace mcpp::gpu
