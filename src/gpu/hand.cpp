#include "mcpp/gpu/hand.hpp"

#include "vk_util.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>

#include <mcpp/gpu/shaders/HandFrag.hpp>
#include <mcpp/gpu/shaders/HandVert.hpp>

namespace mcpp::gpu {
namespace {

constexpr uint32_t kSkinSide = 64;

struct Push {
    float projection[16];
    float light0[4];
    float light1[4];
    float lightmap[4];
};

// `Lighting.updateLevel`: the two directions the world shades a model by.
constexpr float kLight0[3] = {0.2f, 1.0f, -0.7f};
constexpr float kLight1[3] = {-0.2f, 1.0f, 0.7f};

// A world direction as the camera sees it. The hand is posed in camera space
// and the game's two lights are the world's, so one of the two has to turn.
void in_camera(const float in[3], const Matrix4& view, float out[4]) {
    const float length = std::sqrt(in[0] * in[0] + in[1] * in[1] + in[2] * in[2]);
    const float unit[3] = {in[0] / length, in[1] / length, in[2] / length};
    for (int row = 0; row < 3; ++row) {
        out[row] = view.m[row] * unit[0] + view.m[4 + row] * unit[1] + view.m[8 + row] * unit[2];
    }
    out[3] = 0.0f;
}

// The same direction for a queue that is already in the world's own axes, so
// nothing turns it: what the entity shader holds as a constant.
void in_world(const float in[3], float out[4]) {
    const float length = std::sqrt(in[0] * in[0] + in[1] * in[1] + in[2] * in[2]);
    for (int row = 0; row < 3; ++row) out[row] = in[row] / length;
    out[3] = 0.0f;
}

void transform_point(const entity::Matrix& m, const float in[3], float out[3]) {
    for (int row = 0; row < 3; ++row) {
        out[row] = m[row] * in[0] + m[4 + row] * in[1] + m[8 + row] * in[2] + m[12 + row];
    }
}

void transform_direction(const entity::Matrix& m, const float in[3], float out[3]) {
    for (int row = 0; row < 3; ++row) {
        out[row] = m[row] * in[0] + m[4 + row] * in[1] + m[8 + row] * in[2];
    }
}

std::vector<uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

} // namespace

std::filesystem::path HandRenderer::default_path(int32_t protocol) {
    const char* xdg = std::getenv("XDG_DATA_HOME");
    const char* home = std::getenv("HOME");
    const std::filesystem::path base =
        xdg != nullptr && *xdg != '\0'
            ? std::filesystem::path(xdg)
            : std::filesystem::path(home != nullptr ? home : ".") / ".local" / "share";
    return base / "plasma-client" / "data" / ("held-" + std::to_string(protocol) + ".plhd");
}

void HandRenderer::projection(float aspect, float out[16]) {
    const float f = 1.0f / std::tan(kFovDegrees * 3.14159265f / 360.0f);
    const float a = kNear;
    const float b = kFar;
    const float matrix[16] = {
        f / aspect, 0.0f, 0.0f,           0.0f,
        0.0f,       -f,   0.0f,           0.0f,
        0.0f,       0.0f, a / (b - a),    -1.0f,
        0.0f,       0.0f, a * b / (b - a), 0.0f,
    };
    std::memcpy(out, matrix, sizeof(matrix));
}

void HandRenderer::load(const std::filesystem::path& held) {
    const auto bytes = read_file(held);
    if (bytes.size() < 32 || std::memcmp(bytes.data(), "PLHD", 4) != 0) return;
    uint32_t header[8]{};
    std::memcpy(header, bytes.data() + 4, sizeof(header));
    if (header[0] != 3 || header[7] != kStages) return;
    tile_ = header[2];
    layers_ = header[3];
    item_count_ = header[4];
    const size_t pixel_bytes = header[5];
    const size_t index_bytes = header[6];
    const size_t start = (4 + sizeof(header) + 7) & ~size_t{7};
    if (bytes.size() < start + pixel_bytes + index_bytes) return;

    const uint8_t* pixels = bytes.data() + start;
    auto image = vk::sampled_array(device_, VK_FORMAT_R8G8B8A8_UNORM, tile_, tile_, layers_,
                                   {pixels, pixel_bytes}, 4);
    items_image_ = image.image;
    items_memory_ = image.memory;
    items_view_ = image.view;

    const uint8_t* index = pixels + pixel_bytes;
    items_.resize(item_count_);
    constexpr size_t kDisplayBytes = 9 * sizeof(float) + sizeof(uint32_t);
    // Four display transforms per item: first person then third, each hand.
    constexpr size_t kDisplays = 4;
    constexpr size_t kEntryBytes = 2 * kStages * sizeof(uint32_t) +
                                   (kStages - 1) * sizeof(float) + kDisplays * kDisplayBytes;
    for (uint32_t i = 0; i < item_count_; ++i) {
        const uint8_t* row = index + size_t{i} * kEntryBytes;
        for (uint32_t stage = 0; stage < kStages; ++stage) {
            std::memcpy(&items_[i].first[stage], row + stage * 8, sizeof(uint32_t));
            std::memcpy(&items_[i].count[stage], row + stage * 8 + 4, sizeof(uint32_t));
        }
        std::memcpy(items_[i].threshold, row + 8 * kStages, sizeof(items_[i].threshold));
        for (size_t which = 0; which < kDisplays; ++which) {
            const uint8_t* at = row + 8 * kStages + (kStages - 1) * sizeof(float)
                                + which * kDisplayBytes;
            Display& display = items_[i].display[which / 2][which % 2];
            std::memcpy(display.rotation, at, 3 * sizeof(float));
            std::memcpy(display.translation, at + 12, 3 * sizeof(float));
            std::memcpy(display.scale, at + 24, 3 * sizeof(float));
            uint32_t present = 0;
            std::memcpy(&present, at + 36, sizeof(uint32_t));
            display.present = present != 0;
        }
    }

    const uint8_t* geometry = index + index_bytes;
    const size_t quad_bytes = bytes.size() - (geometry - bytes.data());
    static_assert(sizeof(Quad) == 100, "the baked layout must match the generator");
    quads_.resize(quad_bytes / sizeof(Quad));
    std::memcpy(quads_.data(), geometry, quads_.size() * sizeof(Quad));
}

HandRenderer::HandRenderer(Device& device, VkFormat colour_format, VkFormat depth_format,
                           const std::filesystem::path& held)
    : device_(device) {
    const VkDevice handle = device_.handle();
    load(held);
    if (items_image_ == VK_NULL_HANDLE) {
        const std::vector<uint8_t> blank(size_t{16} * 16 * 4, 0);
        tile_ = 16;
        layers_ = 1;
        auto image = vk::sampled_array(device_, VK_FORMAT_R8G8B8A8_UNORM, 16, 16, 1, blank, 4);
        items_image_ = image.image;
        items_memory_ = image.memory;
        items_view_ = image.view;
    }
    const std::vector<uint8_t> blank_skin(size_t{kSkinSide} * kSkinSide * 4, 0);
    auto skin = vk::sampled_array(device_, VK_FORMAT_R8G8B8A8_UNORM, kSkinSide, kSkinSide, 1,
                                  blank_skin, 4);
    skin_image_ = skin.image;
    skin_memory_ = skin.memory;
    skin_view_ = skin.view;
    sampler_ = vk::sampler(handle, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

    VkDescriptorSetLayoutBinding bindings[2]{};
    for (uint32_t i = 0; i < 2; ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo set_layout{};
    set_layout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    set_layout.bindingCount = 2;
    set_layout.pBindings = bindings;
    vk::check(vkCreateDescriptorSetLayout(handle, &set_layout, nullptr, &set_layout_),
              "vkCreateDescriptorSetLayout hand");

    VkPushConstantRange range{};
    range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    range.size = sizeof(Push);
    VkPipelineLayoutCreateInfo layout{};
    layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout.setLayoutCount = 1;
    layout.pSetLayouts = &set_layout_;
    layout.pushConstantRangeCount = 1;
    layout.pPushConstantRanges = &range;
    vk::check(vkCreatePipelineLayout(handle, &layout, nullptr, &layout_),
              "vkCreatePipelineLayout hand");

    vk::PipelineDesc desc;
    desc.vertex = shaders::HandVert();
    desc.fragment = shaders::HandFrag();
    desc.layout = layout_;
    desc.stride = sizeof(Vertex);
    desc.attributes = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)},
        {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)},
        {2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)},
        {3, 0, VK_FORMAT_R32_UINT, offsetof(Vertex, layer)},
        {4, 0, VK_FORMAT_R32_UINT, offsetof(Vertex, colour)},
    };
    desc.depth_write = true;
    desc.blend = vk::Blend::Translucent;
    desc.colour_format = colour_format;
    desc.depth_format = depth_format;
    pipeline_ = vk::pipeline(device_, desc);

    auto ring = vk::mapped_buffer(device_, VkDeviceSize{kSlots} * kSlotVertices * sizeof(Vertex),
                                  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    buffer_ = ring.buffer;
    memory_ = ring.memory;
    mapped_ = ring.mapped;

    VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2};
    VkDescriptorPoolCreateInfo pool{};
    pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool.maxSets = 1;
    pool.poolSizeCount = 1;
    pool.pPoolSizes = &size;
    vk::check(vkCreateDescriptorPool(handle, &pool, nullptr, &pool_),
              "vkCreateDescriptorPool hand");
    VkDescriptorSetAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocate.descriptorPool = pool_;
    allocate.descriptorSetCount = 1;
    allocate.pSetLayouts = &set_layout_;
    vk::check(vkAllocateDescriptorSets(handle, &allocate, &set_), "vkAllocateDescriptorSets hand");
    const VkDescriptorImageInfo infos[2] = {
        {sampler_, items_view_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {sampler_, skin_view_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
    };
    VkWriteDescriptorSet writes[2]{};
    for (uint32_t i = 0; i < 2; ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = set_;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].pImageInfo = &infos[i];
    }
    vkUpdateDescriptorSets(handle, 2, writes, 0, nullptr);
}

HandRenderer::~HandRenderer() { destroy(); }

void HandRenderer::destroy() {
    const VkDevice handle = device_.handle();
    if (pipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(handle, pipeline_, nullptr);
    if (layout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(handle, layout_, nullptr);
    if (pool_ != VK_NULL_HANDLE) vkDestroyDescriptorPool(handle, pool_, nullptr);
    if (set_layout_ != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(handle, set_layout_, nullptr);
    if (sampler_ != VK_NULL_HANDLE) vkDestroySampler(handle, sampler_, nullptr);
    vk::Image items{items_image_, items_memory_, items_view_};
    items.destroy(handle);
    vk::Image skin{skin_image_, skin_memory_, skin_view_};
    skin.destroy(handle);
    vk::Buffer buffer{buffer_, memory_, mapped_, 0};
    buffer.destroy(handle);
}

uint32_t HandRenderer::item_quads(int32_t item) const {
    if (item < 0 || static_cast<uint32_t>(item) >= item_count_) return 0;
    return items_[static_cast<size_t>(item)].count[0];
}

// `ItemStackRenderState.visitExtents`: the corners of an item's own model,
// before any display transform, which is what a dropped item's lift and its
// stack offsets are worked out from. Empty when the item has no geometry.
std::optional<std::array<float, 6>> HandRenderer::item_bounds(int32_t item) const {
    if (item < 0 || static_cast<uint32_t>(item) >= item_count_) return std::nullopt;
    const Entry& entry = items_[static_cast<size_t>(item)];
    if (entry.count[0] == 0) return std::nullopt;
    std::array<float, 6> box{std::numeric_limits<float>::max(),
                             std::numeric_limits<float>::max(),
                             std::numeric_limits<float>::max(),
                             -std::numeric_limits<float>::max(),
                             -std::numeric_limits<float>::max(),
                             -std::numeric_limits<float>::max()};
    for (uint32_t i = 0; i < entry.count[0]; ++i) {
        const Quad& quad = quads_[entry.first[0] + i];
        for (const auto& corner : quad.corners) {
            for (int axis = 0; axis < 3; ++axis) {
                box[static_cast<size_t>(axis)] =
                    std::min(box[static_cast<size_t>(axis)], corner.position[axis]);
                box[static_cast<size_t>(axis) + 3] =
                    std::max(box[static_cast<size_t>(axis) + 3], corner.position[axis]);
            }
        }
    }
    return box;
}

HandRenderer::Display HandRenderer::item_display(int32_t item, bool left_hand,
                                                 bool third_person) const {
    if (item < 0 || static_cast<uint32_t>(item) >= item_count_) return {};
    const Entry& entry = items_[static_cast<size_t>(item)];
    return entry.display[third_person ? 1 : 0][left_hand ? 1 : 0];
}

void HandRenderer::set_skin(std::span<const uint8_t> rgba) {
    if (rgba.size() != size_t{kSkinSide} * kSkinSide * 4) return;
    const VkDevice handle = device_.handle();
    vkDeviceWaitIdle(handle);
    vk::Image old{skin_image_, skin_memory_, skin_view_};
    old.destroy(handle);
    auto image = vk::sampled_array(device_, VK_FORMAT_R8G8B8A8_UNORM, kSkinSide, kSkinSide, 1,
                                   rgba, 4);
    skin_image_ = image.image;
    skin_memory_ = image.memory;
    skin_view_ = image.view;
    const VkDescriptorImageInfo info{sampler_, skin_view_,
                                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set_;
    write.dstBinding = 1;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &info;
    vkUpdateDescriptorSets(handle, 1, &write, 0, nullptr);
    skin_ready_ = true;
}

void HandRenderer::bake_arm(const entity::Model& model, ArmModel& out) {
    out.model = &model;
    out.vertices.clear();
    entity::bake(model, out.vertices);
    out.arm[0] = model.find("right_arm");
    out.arm[1] = model.find("left_arm");
    out.sleeve[0] = model.find("right_arm/right_sleeve");
    out.sleeve[1] = model.find("left_arm/left_sleeve");
}

void HandRenderer::load_arm(const entity::Model& wide, const entity::Model& slim) {
    bake_arm(wide, wide_);
    bake_arm(slim, slim_);
}

void HandRenderer::begin() {
    vertices_.clear();
    world_.clear();
    runs_.clear();
    slot_ = (slot_ + 1) % kSlots;
}

void HandRenderer::add_triangle(const entity::Vertex& vertex, const entity::Matrix& pose) {
    Vertex out{};
    transform_point(pose, vertex.position, out.position);
    const float normal[3] = {static_cast<float>(vertex.normal[0]) / 127.0f,
                             static_cast<float>(vertex.normal[1]) / 127.0f,
                             static_cast<float>(vertex.normal[2]) / 127.0f};
    transform_direction(pose, normal, out.normal);
    out.uv[0] = vertex.uv[0];
    out.uv[1] = vertex.uv[1];
    out.layer = kSkinBit;
    out.colour = 0xFFFFFFFFu;
    vertices_.push_back(out);
}

void HandRenderer::add_quads(std::vector<Vertex>& out, uint32_t limit, int32_t item,
                             const entity::Matrix& pose, float pull) {
    if (item < 0 || static_cast<uint32_t>(item) >= item_count_) return;
    const Entry& entry = items_[static_cast<size_t>(item)];
    // `RangeSelectItemModel`: how far a bow or a crossbow is drawn picks the
    // model, at the thresholds the item's own definition names. Nothing else
    // swaps a model while it is in the hand.
    uint32_t stage = 0;
    if (pull > 0.0f) {
        for (uint32_t i = 0; i + 1 < kStages; ++i) {
            if (pull >= entry.threshold[i]) stage = i + 1;
        }
    }
    for (uint32_t i = 0; i < entry.count[stage]; ++i) {
        const Quad& quad = quads_[entry.first[stage] + i];
        Vertex corners[4]{};
        for (int c = 0; c < 4; ++c) {
            transform_point(pose, quad.corners[c].position, corners[c].position);
            transform_direction(pose, quad.normal, corners[c].normal);
            corners[c].uv[0] = quad.corners[c].uv[0];
            corners[c].uv[1] = quad.corners[c].uv[1];
            corners[c].layer = quad.layer;
            corners[c].colour = quad.colour;
        }
        for (const int c : {0, 1, 2, 0, 2, 3}) {
            if (out.size() >= limit) return;
            out.push_back(corners[c]);
        }
    }
}

void HandRenderer::add_item(int32_t item, const entity::Matrix& pose, float pull) {
    add_quads(vertices_, kMaxVertices, item, pose, pull);
}

void HandRenderer::add_world_item(int32_t item, const entity::Matrix& pose, const float light[3],
                                  float pull) {
    const size_t before = world_.size();
    add_quads(world_, kMaxWorldVertices, item, pose, pull);
    if (world_.size() == before) return;
    const bool same = !runs_.empty() && runs_.back().light[0] == light[0]
                      && runs_.back().light[1] == light[1] && runs_.back().light[2] == light[2];
    if (!same) {
        runs_.push_back({static_cast<uint32_t>(before), 0, {light[0], light[1], light[2]}});
    }
    runs_.back().count += static_cast<uint32_t>(world_.size() - before);
}

// `AvatarRenderer.renderHand`: the arm alone, reset to the model's own pose
// with the small turn the game gives it, and the sleeve over it.
void HandRenderer::add_arm(const entity::Matrix& pose, bool left_arm, bool slim, bool sleeve) {
    const ArmModel& source = slim ? slim_ : wide_;
    if (source.model == nullptr) return;
    const int side = left_arm ? 1 : 0;
    if (source.arm[side] < 0) return;
    // The model keeps its own chain - every part hangs off `root` - so only
    // the arm's own turn is set and only its boxes are drawn.
    entity::Pose parts(*source.model);
    parts[source.arm[side]].z_rot = left_arm ? -0.1f : 0.1f;
    std::vector<entity::Matrix> matrices(source.model->parts.size());
    entity::pose_matrices(*source.model, parts, pose, matrices);
    const int32_t wanted = source.sleeve[side];
    for (const entity::Vertex& vertex : source.vertices) {
        const auto part = static_cast<int32_t>(vertex.part);
        if (part != source.arm[side] && (part != wanted || !sleeve)) continue;
        if (vertices_.size() >= kMaxVertices) return;
        add_triangle(vertex, matrices[vertex.part]);
    }
}

void HandRenderer::record(VkCommandBuffer command, uint32_t width, uint32_t height,
                          const float light[3], const Camera& camera) {
    if (vertices_.empty()) return;
    const size_t offset = size_t{slot_} * kSlotVertices * sizeof(Vertex);
    std::memcpy(static_cast<uint8_t*>(mapped_) + offset, vertices_.data(),
                vertices_.size() * sizeof(Vertex));

    Push push{};
    projection(static_cast<float>(width) / static_cast<float>(height), push.projection);
    const Matrix4 view = camera_rotation(camera);
    in_camera(kLight0, view, push.light0);
    in_camera(kLight1, view, push.light1);
    for (int i = 0; i < 3; ++i) push.lightmap[i] = light[i];
    push.lightmap[3] = 1.0f;

    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &set_, 0,
                            nullptr);
    vkCmdPushConstants(command, layout_,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(Push), &push);
    const VkViewport viewport{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height),
                              0.0f, 1.0f};
    const VkRect2D scissor{{0, 0}, {width, height}};
    vkCmdSetViewport(command, 0, 1, &viewport);
    vkCmdSetScissor(command, 0, 1, &scissor);
    const VkDeviceSize vertex_offset = offset;
    vkCmdBindVertexBuffers(command, 0, 1, &buffer_, &vertex_offset);
    vkCmdDraw(command, static_cast<uint32_t>(vertices_.size()), 1, 0, 0);
}

void HandRenderer::record_world(VkCommandBuffer command, const float view_projection[16],
                                const double eye[3], uint32_t width, uint32_t height) {
    if (world_.empty()) return;
    const size_t offset = (size_t{slot_} * kSlotVertices + kMaxVertices) * sizeof(Vertex);
    std::memcpy(static_cast<uint8_t*>(mapped_) + offset, world_.data(),
                world_.size() * sizeof(Vertex));

    Push push{};
    // Every item is posed relative to the camera, so the camera's own
    // translation goes back into the matrix, as the entity renderer does it.
    std::memcpy(push.projection, view_projection, sizeof(push.projection));
    for (int row = 0; row < 4; ++row) {
        push.projection[12 + row] = view_projection[row] * static_cast<float>(eye[0])
                                    + view_projection[4 + row] * static_cast<float>(eye[1])
                                    + view_projection[8 + row] * static_cast<float>(eye[2])
                                    + view_projection[12 + row];
    }
    in_world(kLight0, push.light0);
    in_world(kLight1, push.light1);

    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &set_, 0,
                            nullptr);
    const VkViewport viewport{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height),
                              0.0f, 1.0f};
    const VkRect2D scissor{{0, 0}, {width, height}};
    vkCmdSetViewport(command, 0, 1, &viewport);
    vkCmdSetScissor(command, 0, 1, &scissor);
    const VkDeviceSize vertex_offset = offset;
    vkCmdBindVertexBuffers(command, 0, 1, &buffer_, &vertex_offset);
    for (const WorldRun& run : runs_) {
        for (int i = 0; i < 3; ++i) push.lightmap[i] = run.light[i];
        push.lightmap[3] = 1.0f;
        vkCmdPushConstants(command, layout_,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(Push), &push);
        vkCmdDraw(command, run.count, 1, run.first, 0);
    }
}

} // namespace mcpp::gpu
