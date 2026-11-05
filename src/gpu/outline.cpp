#include "mcpp/gpu/outline.hpp"

#include "vk_util.hpp"

#include <cstring>

#include <mcpp/gpu/shaders/OutlineFrag.hpp>
#include <mcpp/gpu/shaders/OutlineVert.hpp>

namespace mcpp::gpu {
namespace {

// Room for every outline shape the game has, many times over.
constexpr uint32_t kMaxEdges = 1u << 16;
constexpr uint32_t kFloatsPerEdge = 6;

struct OutlinePush {
    float view_projection[16];
    float eye[4];      // xyz, then the line width in pixels
    float corner[4];   // the block's corner from the eye
    float colour[4];
    float screen[4];
};
static_assert(sizeof(OutlinePush) <= 128, "push constants must fit the guaranteed minimum");

VkShaderModule make_module(VkDevice device, std::span<const uint32_t> words) {
    VkShaderModuleCreateInfo create{};
    create.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    create.codeSize = words.size() * sizeof(uint32_t);
    create.pCode = words.data();
    VkShaderModule module = VK_NULL_HANDLE;
    vk::check(vkCreateShaderModule(device, &create, nullptr, &module), "vkCreateShaderModule outline");
    return module;
}

} // namespace

Outline::Outline(Device& device, VkFormat colour_format) : device_(device) {
    vk::Buffer buffer = vk::mapped_buffer(device_, VkDeviceSize{kMaxEdges} * kFloatsPerEdge * sizeof(float),
                                          VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    buffer_ = buffer.buffer;
    memory_ = buffer.memory;
    mapped_ = static_cast<float*>(buffer.mapped);

    VkPushConstantRange push{};
    push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    push.size = sizeof(OutlinePush);

    VkPipelineLayoutCreateInfo pipeline_layout{};
    pipeline_layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout.pushConstantRangeCount = 1;
    pipeline_layout.pPushConstantRanges = &push;
    vk::check(vkCreatePipelineLayout(device_.handle(), &pipeline_layout, nullptr, &layout_),
              "vkCreatePipelineLayout outline");

    VkShaderModule vertex = make_module(device_.handle(), shaders::OutlineVert());
    VkShaderModule fragment = make_module(device_.handle(), shaders::OutlineFrag());

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertex;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragment;
    stages[1].pName = "main";

    // One instance per edge: its two ends.
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = kFloatsPerEdge * sizeof(float);
    binding.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
    VkVertexInputAttributeDescription attributes[2]{};
    attributes[0].location = 0;
    attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[1].location = 1;
    attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[1].offset = 3 * sizeof(float);
    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input.vertexBindingDescriptionCount = 1;
    vertex_input.pVertexBindingDescriptions = &binding;
    vertex_input.vertexAttributeDescriptionCount = 2;
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
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // The game's line pipeline tests and writes depth; the lines win against
    // the faces they lie on because the shader pulls them toward the eye.
    VkPipelineDepthStencilStateCreateInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth.depthTestEnable = VK_TRUE;
    depth.depthWriteEnable = VK_TRUE;
    depth.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
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

    VkFormat format = colour_format;
    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &format;
    rendering.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;

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
    if (result != VK_SUCCESS) {
        destroy();
        vk::check(result, "vkCreateGraphicsPipelines outline");
    }
}

Outline::~Outline() { destroy(); }

void Outline::destroy() {
    if (pipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(device_.handle(), pipeline_, nullptr);
    if (layout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(device_.handle(), layout_, nullptr);
    if (buffer_ != VK_NULL_HANDLE) vkDestroyBuffer(device_.handle(), buffer_, nullptr);
    if (memory_ != VK_NULL_HANDLE) vkFreeMemory(device_.handle(), memory_, nullptr);
    pipeline_ = VK_NULL_HANDLE;
    layout_ = VK_NULL_HANDLE;
    buffer_ = VK_NULL_HANDLE;
    memory_ = VK_NULL_HANDLE;
}

const Outline::Range* Outline::find(uint64_t key) const {
    const auto found = ranges_.find(key);
    return found == ranges_.end() ? nullptr : &found->second;
}

Outline::Range Outline::add(uint64_t key, std::span<const float> edges) {
    if (const Range* known = find(key)) return *known;
    const auto count = static_cast<uint32_t>(edges.size() / kFloatsPerEdge);
    if (used_ + count > kMaxEdges) return {};
    std::memcpy(mapped_ + static_cast<size_t>(used_) * kFloatsPerEdge, edges.data(),
                static_cast<size_t>(count) * kFloatsPerEdge * sizeof(float));
    const Range range{used_, count};
    used_ += count;
    ranges_.emplace(key, range);
    return range;
}

void Outline::aim(Range range, const double corner_from_eye[3], const float eye[3], uint32_t width,
                  uint32_t height) {
    range_ = range;
    for (int i = 0; i < 3; ++i) {
        corner_[i] = static_cast<float>(corner_from_eye[i]);
        eye_[i] = eye[i];
    }
    screen_[0] = static_cast<float>(width);
    screen_[1] = static_cast<float>(height);
}

// `LevelRenderer.renderBlockOutline`, `Window.getAppropriateLineWidth`: black at 40%, 2.5 pixels wide on a 1920-wide window and wider ones.
void Outline::record(VkCommandBuffer command, const float view_projection[16],
                     const int32_t[3]) const {
    if (range_.count == 0) return;
    OutlinePush push{};
    std::memcpy(push.view_projection, view_projection, sizeof(push.view_projection));
    for (int i = 0; i < 3; ++i) {
        push.eye[i] = eye_[i];
        push.corner[i] = corner_[i];
    }
    push.eye[3] = std::max(2.5f, screen_[0] / 1920.0f * 2.5f);
    push.colour[3] = static_cast<float>(102) / 255.0f;
    push.screen[0] = screen_[0];
    push.screen[1] = screen_[1];

    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdPushConstants(command, layout_,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(push), &push);
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(command, 0, 1, &buffer_, &offset);
    vkCmdDraw(command, 6, range_.count, 0, range_.first);
}

} // namespace mcpp::gpu
