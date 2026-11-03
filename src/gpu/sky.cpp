#include "mcpp/gpu/sky.hpp"

#include "mcpp/byte_buffer.hpp"
#include "mcpp/gpu/world_renderer.hpp"
#include "mcpp/jmath.hpp"
#include "vk_util.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <format>
#include <fstream>
#include <numbers>

#include <mcpp/gpu/shaders/CloudsFrag.hpp>
#include <mcpp/gpu/shaders/CloudsVert.hpp>
#include <mcpp/gpu/shaders/SkyFrag.hpp>
#include <mcpp/gpu/shaders/SkyVert.hpp>

namespace mcpp::gpu {
namespace {

constexpr uint32_t kCloudSide = 256;
constexpr uint32_t kCelestialSide = 64;
constexpr uint32_t kCelestialLayers = 10;
constexpr uint32_t kEndSkySide = 128;
constexpr uint32_t kEndFlashLayer = 9;
constexpr float kPi = std::numbers::pi_v<float>;

struct SkyVertex {
    float position[3];
    float uv[2];
    uint32_t colour;   // 0xAABBGGRR, read as RGBA8
};

struct SkyPush {
    float transform[16];
    float colour[4];
    float fog_colour[4];
    float params[4];   // mode, layer, sky fog end
};
static_assert(sizeof(SkyPush) <= 128, "push constants must fit the guaranteed minimum");

struct CloudPush {
    float transform[16];
    float colour[4];
    float offset[4];       // xyz where cell 0 starts relative to the eye, w the fog's end
    float cell[4];
};

enum Mode : int { kSkyDisc, kVertexColour, kFlat, kCelestial, kEndSky };

uint32_t rgba(uint32_t argb) {
    return (argb & 0xFF00FF00u) | ((argb >> 16) & 0xFFu) | ((argb & 0xFFu) << 16);
}

void colour_of(uint32_t argb, float out[4]) {
    out[0] = world::argb::red_float(argb);
    out[1] = world::argb::green_float(argb);
    out[2] = world::argb::blue_float(argb);
    out[3] = world::argb::alpha_float(argb);
}

Matrix4 identity() {
    return Matrix4{{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
}
Matrix4 rotate_x(float radians) {
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    return Matrix4{{1, 0, 0, 0, 0, c, s, 0, 0, -s, c, 0, 0, 0, 0, 1}};
}
Matrix4 rotate_y(float radians) {
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    return Matrix4{{c, 0, -s, 0, 0, 1, 0, 0, s, 0, c, 0, 0, 0, 0, 1}};
}
Matrix4 rotate_z(float radians) {
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    return Matrix4{{c, s, 0, 0, -s, c, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
}
Matrix4 translate(float x, float y, float z) {
    Matrix4 out = identity();
    out.m[12] = x;
    out.m[13] = y;
    out.m[14] = z;
    return out;
}
Matrix4 scale(float x, float y, float z) {
    Matrix4 out = identity();
    out.m[0] = x;
    out.m[5] = y;
    out.m[10] = z;
    return out;
}
float radians(float degrees) { return degrees * kPi / 180.0f; }

void add_quad(std::vector<SkyVertex>& out, const SkyVertex corners[4]) {
    for (const int i : {0, 1, 2, 2, 3, 0}) out.push_back(corners[i]);
}

} // namespace

struct Sky::Resources {
    vk::Image celestials;
    vk::Image end_sky;
    VkSampler clamp = VK_NULL_HANDLE;
    VkSampler repeat = VK_NULL_HANDLE;
    vk::Buffer vertices;
    vk::Buffer faces;
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout cloud_set_layout = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkDescriptorSet cloud_set = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipelineLayout cloud_layout = VK_NULL_HANDLE;
    VkPipeline disc = VK_NULL_HANDLE;
    VkPipeline translucent = VK_NULL_HANDLE;
    VkPipeline overlay = VK_NULL_HANDLE;
    VkPipeline fancy_clouds = VK_NULL_HANDLE;
    VkPipeline flat_clouds = VK_NULL_HANDLE;
};

std::filesystem::path Sky::default_path(int32_t protocol) {
    std::filesystem::path base;
    if (const char* data_home = std::getenv("XDG_DATA_HOME")) {
        base = data_home;
    } else if (const char* home = std::getenv("HOME")) {
        base = std::filesystem::path(home) / ".local" / "share";
    }
    return base / "plasma-client" / "data" / std::format("sky-{}.plsk", protocol);
}

std::vector<std::array<float, 3>> Sky::star_corners() {
    jmath::LegacyRandom random(10842);
    std::vector<std::array<float, 3>> out;
    for (int i = 0; i < 1500; ++i) {
        const float x = random.next_float() * 2.0f - 1.0f;
        const float y = random.next_float() * 2.0f - 1.0f;
        const float z = random.next_float() * 2.0f - 1.0f;
        const float size = 0.15f + random.next_float() * 0.1f;
        const float length_squared = x * x + y * y + z * z;
        if (length_squared <= 0.010000001f || length_squared >= 1.0f) continue;
        const float to_100 = 100.0f / std::sqrt(length_squared);
        const float centre[3] = {x * to_100, y * to_100, z * to_100};
        const auto turn = static_cast<float>(random.next_double() * static_cast<double>(kPi) * 2.0);

        // Facing the eye, the star's own up the world's, then spun about the
        // line of sight.
        const float dir_length = 1.0f / std::sqrt(centre[0] * centre[0] + centre[1] * centre[1] +
                                                  centre[2] * centre[2]);
        const float dir[3] = {-centre[0] * dir_length, -centre[1] * dir_length, -centre[2] * dir_length};
        float left[3] = {1.0f * dir[2] - 0.0f * dir[1], 0.0f * dir[0] - 0.0f * dir[2],
                         0.0f * dir[1] - 1.0f * dir[0]};
        const float left_length =
            1.0f / std::sqrt(left[0] * left[0] + left[1] * left[1] + left[2] * left[2]);
        for (float& c : left) c *= left_length;
        const float up[3] = {dir[1] * left[2] - dir[2] * left[1], dir[2] * left[0] - dir[0] * left[2],
                             dir[0] * left[1] - dir[1] * left[0]};
        const float s = std::sin(-turn);
        const float c = std::cos(-turn);
        const float corners[4][2] = {{size, -size}, {size, size}, {-size, size}, {-size, -size}};
        for (const auto& corner : corners) {
            const float a = c * corner[0] - s * corner[1];
            const float b = s * corner[0] + c * corner[1];
            out.push_back({left[0] * a + up[0] * b + centre[0], left[1] * a + up[1] * b + centre[1],
                           left[2] * a + up[2] * b + centre[2]});
        }
    }
    return out;
}

Sky::Sky(Device& device, VkFormat colour_format, VkFormat depth_format,
         const std::filesystem::path& textures)
    : device_(device) {
    std::ifstream input(textures, std::ios::binary);
    if (!input) throw ProtocolError(std::format("cannot open {}", textures.string()));
    char magic[4] = {};
    uint32_t version = 0;
    input.read(magic, 4);
    input.read(reinterpret_cast<char*>(&version), 4);
    if (std::memcmp(magic, "PLSK", 4) != 0 || version != 2) {
        throw ProtocolError("not a version 2 sky file; bake it again with tools/bake_sky.py");
    }
    std::vector<uint8_t> clouds(kCloudSide * kCloudSide);
    std::vector<uint8_t> celestials(kCelestialSide * kCelestialSide * 4 * kCelestialLayers);
    std::vector<uint8_t> end_sky(kEndSkySide * kEndSkySide * 4);
    input.read(reinterpret_cast<char*>(clouds.data()), static_cast<std::streamsize>(clouds.size()));
    input.read(reinterpret_cast<char*>(celestials.data()),
               static_cast<std::streamsize>(celestials.size()));
    input.read(reinterpret_cast<char*>(end_sky.data()), static_cast<std::streamsize>(end_sky.size()));
    if (!input) throw ProtocolError("truncated sky file");

    // Each filled cell remembers which of its sides open onto an empty one,
    // because only those get a wall.
    const auto empty = [&](int32_t x, int32_t z) {
        x = (x % 256 + 256) % 256;
        z = (z % 256 + 256) % 256;
        return clouds[static_cast<size_t>(z) * kCloudSide + static_cast<size_t>(x)] < 10;
    };
    cloud_cells_.assign(kCloudSide * kCloudSide, 0);
    for (int32_t z = 0; z < 256; ++z) {
        for (int32_t x = 0; x < 256; ++x) {
            if (empty(x, z)) continue;
            cloud_cells_[static_cast<size_t>(z) * kCloudSide + static_cast<size_t>(x)] = static_cast<uint8_t>(
                0x10 | (empty(x, z - 1) ? 8 : 0) | (empty(x + 1, z) ? 4 : 0) |
                (empty(x, z + 1) ? 2 : 0) | (empty(x - 1, z) ? 1 : 0));
        }
    }

    gpu_ = new Resources;
    try {
        gpu_->celestials = vk::sampled_array(device_, VK_FORMAT_R8G8B8A8_UNORM, kCelestialSide,
                                             kCelestialSide, kCelestialLayers, celestials, 4);
        gpu_->end_sky = vk::sampled_array(device_, VK_FORMAT_R8G8B8A8_UNORM, kEndSkySide,
                                          kEndSkySide, 1, end_sky, 4);
        gpu_->clamp = vk::sampler(device_.handle(), VK_FILTER_NEAREST,
                                  VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
        gpu_->repeat = vk::sampler(device_.handle(), VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_REPEAT);
        build_geometry();
        create_pipelines(colour_format, depth_format);
    } catch (...) {
        destroy();
        throw;
    }
}

Sky::~Sky() { destroy(); }

void Sky::destroy() {
    if (gpu_ == nullptr) return;
    const VkDevice device = device_.handle();
    for (VkPipeline pipeline : {gpu_->disc, gpu_->translucent, gpu_->overlay, gpu_->fancy_clouds,
                                gpu_->flat_clouds}) {
        if (pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, pipeline, nullptr);
    }
    if (gpu_->layout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, gpu_->layout, nullptr);
    if (gpu_->cloud_layout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, gpu_->cloud_layout, nullptr);
    if (gpu_->pool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, gpu_->pool, nullptr);
    if (gpu_->set_layout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, gpu_->set_layout, nullptr);
    if (gpu_->cloud_set_layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, gpu_->cloud_set_layout, nullptr);
    }
    if (gpu_->clamp != VK_NULL_HANDLE) vkDestroySampler(device, gpu_->clamp, nullptr);
    if (gpu_->repeat != VK_NULL_HANDLE) vkDestroySampler(device, gpu_->repeat, nullptr);
    gpu_->vertices.destroy(device);
    gpu_->faces.destroy(device);
    gpu_->celestials.destroy(device);
    gpu_->end_sky.destroy(device);
    delete gpu_;
    gpu_ = nullptr;
}

void Sky::build_geometry() {
    std::vector<SkyVertex> v;
    const auto range = [&](size_t first) {
        return Range{static_cast<uint32_t>(first), static_cast<uint32_t>(v.size() - first)};
    };
    const uint32_t white = 0xFFFFFFFFu;

    // A fan of eight around the centre, 512 out, at y = 16 above and -16 below.
    const auto disc = [&](float y) {
        const size_t first = v.size();
        const float radius = (y > 0.0f ? 1.0f : -1.0f) * 512.0f;
        std::vector<SkyVertex> ring;
        for (int angle = -180; angle <= 180; angle += 45) {
            const float a = static_cast<float>(angle) * static_cast<float>(std::numbers::pi / 180.0);
            ring.push_back({{radius * jmath::cos(a), y, 512.0f * jmath::sin(a)}, {0, 0}, white});
        }
        const SkyVertex centre{{0.0f, y, 0.0f}, {0, 0}, white};
        for (size_t i = 0; i + 1 < ring.size(); ++i) {
            v.push_back(centre);
            v.push_back(ring[i]);
            v.push_back(ring[i + 1]);
        }
        return range(first);
    };
    disc_ = disc(16.0f);
    dark_disc_ = disc(-16.0f);

    {
        const size_t first = v.size();
        const SkyVertex centre{{0.0f, 100.0f, 0.0f}, {0, 0}, white};
        std::vector<SkyVertex> ring;
        for (int i = 0; i <= 16; ++i) {
            const float a = static_cast<float>(i) * (kPi * 2.0f) / 16.0f;
            const float s = jmath::sin(a);
            const float c = jmath::cos(a);
            ring.push_back({{s * 120.0f, c * 120.0f, -c * 40.0f}, {0, 0}, 0x00FFFFFFu});
        }
        for (size_t i = 0; i + 1 < ring.size(); ++i) {
            v.push_back(centre);
            v.push_back(ring[i]);
            v.push_back(ring[i + 1]);
        }
        fan_ = range(first);
    }

    {
        const size_t first = v.size();
        const SkyVertex corners[4] = {{{-1, 0, -1}, {0, 0}, white}, {{1, 0, -1}, {1, 0}, white},
                                      {{1, 0, 1}, {1, 1}, white}, {{-1, 0, 1}, {0, 1}, white}};
        add_quad(v, corners);
        sun_ = range(first);
    }
    {
        // Every phase faces the other way round from the sun.
        const size_t first = v.size();
        const SkyVertex corners[4] = {{{-1, 0, -1}, {1, 1}, white}, {{1, 0, -1}, {0, 1}, white},
                                      {{1, 0, 1}, {0, 0}, white}, {{-1, 0, 1}, {1, 0}, white}};
        add_quad(v, corners);
        moons_ = range(first);
    }

    {
        const size_t first = v.size();
        const auto corners = star_corners();
        for (size_t i = 0; i + 3 < corners.size(); i += 4) {
            SkyVertex quad[4];
            for (int k = 0; k < 4; ++k) {
                quad[k] = {{corners[i + k][0], corners[i + k][1], corners[i + k][2]}, {0, 0}, white};
            }
            add_quad(v, quad);
        }
        stars_ = range(first);
    }

    {
        const size_t first = v.size();
        const uint32_t grey = rgba(0xFF282828u);
        for (int face = 0; face < 6; ++face) {
            Matrix4 turn = identity();
            switch (face) {
            case 1: turn = rotate_x(kPi / 2.0f); break;
            case 2: turn = rotate_x(-kPi / 2.0f); break;
            case 3: turn = rotate_x(kPi); break;
            case 4: turn = rotate_z(kPi / 2.0f); break;
            case 5: turn = rotate_z(-kPi / 2.0f); break;
            default: break;
            }
            const float local[4][5] = {{-100, -100, -100, 0, 0}, {-100, -100, 100, 0, 16},
                                       {100, -100, 100, 16, 16}, {100, -100, -100, 16, 0}};
            SkyVertex quad[4];
            for (int k = 0; k < 4; ++k) {
                float p[3];
                for (int row = 0; row < 3; ++row) {
                    p[row] = turn.m[row] * local[k][0] + turn.m[4 + row] * local[k][1] +
                             turn.m[8 + row] * local[k][2];
                }
                quad[k] = {{p[0], p[1], p[2]}, {local[k][3], local[k][4]}, grey};
            }
            add_quad(v, quad);
        }
        end_box_ = range(first);
    }
    end_flash_ = sun_;

    gpu_->vertices = vk::mapped_buffer(device_, v.size() * sizeof(SkyVertex),
                                       VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    std::memcpy(gpu_->vertices.mapped, v.data(), v.size() * sizeof(SkyVertex));
}

void Sky::create_pipelines(VkFormat colour_format, VkFormat depth_format) {
    const VkDevice device = device_.handle();
    VkDescriptorSetLayoutBinding bindings[2]{};
    for (uint32_t i = 0; i < 2; ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo layout_create{};
    layout_create.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_create.bindingCount = 2;
    layout_create.pBindings = bindings;
    vk::check(vkCreateDescriptorSetLayout(device, &layout_create, nullptr, &gpu_->set_layout),
              "vkCreateDescriptorSetLayout sky");

    VkDescriptorSetLayoutBinding faces{};
    faces.binding = 0;
    faces.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    faces.descriptorCount = 1;
    faces.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    layout_create.bindingCount = 1;
    layout_create.pBindings = &faces;
    vk::check(vkCreateDescriptorSetLayout(device, &layout_create, nullptr, &gpu_->cloud_set_layout),
              "vkCreateDescriptorSetLayout clouds");

    const VkDescriptorPoolSize sizes[2] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2},
                                           {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}};
    VkDescriptorPoolCreateInfo pool_create{};
    pool_create.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_create.maxSets = 2;
    pool_create.poolSizeCount = 2;
    pool_create.pPoolSizes = sizes;
    vk::check(vkCreateDescriptorPool(device, &pool_create, nullptr, &gpu_->pool),
              "vkCreateDescriptorPool sky");
    const VkDescriptorSetLayout set_layouts[2] = {gpu_->set_layout, gpu_->cloud_set_layout};
    VkDescriptorSet sets[2]{};
    VkDescriptorSetAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocate.descriptorPool = gpu_->pool;
    allocate.descriptorSetCount = 2;
    allocate.pSetLayouts = set_layouts;
    vk::check(vkAllocateDescriptorSets(device, &allocate, sets), "vkAllocateDescriptorSets sky");
    gpu_->set = sets[0];
    gpu_->cloud_set = sets[1];

    const VkDescriptorImageInfo images[2] = {
        {gpu_->clamp, gpu_->celestials.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {gpu_->repeat, gpu_->end_sky.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
    VkWriteDescriptorSet writes[2]{};
    for (uint32_t i = 0; i < 2; ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = gpu_->set;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].pImageInfo = &images[i];
    }
    vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);

    VkPushConstantRange push{};
    push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    push.size = sizeof(SkyPush);
    VkPipelineLayoutCreateInfo layout{};
    layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout.setLayoutCount = 1;
    layout.pSetLayouts = &gpu_->set_layout;
    layout.pushConstantRangeCount = 1;
    layout.pPushConstantRanges = &push;
    vk::check(vkCreatePipelineLayout(device, &layout, nullptr, &gpu_->layout), "vkCreatePipelineLayout sky");
    push.size = sizeof(CloudPush);
    layout.pSetLayouts = &gpu_->cloud_set_layout;
    vk::check(vkCreatePipelineLayout(device, &layout, nullptr, &gpu_->cloud_layout),
              "vkCreatePipelineLayout clouds");

    vk::PipelineDesc desc;
    desc.vertex = shaders::SkyVert();
    desc.fragment = shaders::SkyFrag();
    desc.layout = gpu_->layout;
    desc.stride = sizeof(SkyVertex);
    desc.attributes = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(SkyVertex, position)},
                       {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(SkyVertex, uv)},
                       {2, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(SkyVertex, colour)}};
    desc.depth_test = false;
    desc.colour_format = colour_format;
    desc.depth_format = depth_format;
    gpu_->disc = vk::pipeline(device_, desc);
    desc.blend = vk::Blend::Translucent;
    gpu_->translucent = vk::pipeline(device_, desc);
    desc.blend = vk::Blend::Overlay;
    gpu_->overlay = vk::pipeline(device_, desc);

    vk::PipelineDesc clouds;
    clouds.vertex = shaders::CloudsVert();
    clouds.fragment = shaders::CloudsFrag();
    clouds.layout = gpu_->cloud_layout;
    clouds.depth_write = true;
    clouds.blend = vk::Blend::Translucent;
    clouds.colour_format = colour_format;
    clouds.depth_format = depth_format;
    gpu_->flat_clouds = vk::pipeline(device_, clouds);
    // The game winds its faces counter-clockwise, which this projection keeps.
    clouds.cull = VK_CULL_MODE_BACK_BIT;
    clouds.front = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    gpu_->fancy_clouds = vk::pipeline(device_, clouds);
}

void Sky::record(VkCommandBuffer command, const Camera& camera, uint32_t width, uint32_t height,
                 const world::Atmosphere& a) const {
    if (!a.draw_sky) return;
    const Matrix4 projection =
        camera_projection(camera, static_cast<float>(width) / static_cast<float>(height));
    const Matrix4 view = projection * camera_rotation(camera);

    const VkViewport viewport{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height),
                              0.0f, 1.0f};
    const VkRect2D scissor{{0, 0}, {width, height}};
    vkCmdSetViewport(command, 0, 1, &viewport);
    vkCmdSetScissor(command, 0, 1, &scissor);
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(command, 0, 1, &gpu_->vertices.buffer, &offset);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, gpu_->layout, 0, 1, &gpu_->set,
                            0, nullptr);

    const auto draw = [&](VkPipeline pipeline, const Matrix4& model, const float colour[4], Mode mode,
                          Range range, float layer = 0.0f) {
        SkyPush push{};
        const Matrix4 transform = view * model;
        std::memcpy(push.transform, transform.m, sizeof(push.transform));
        std::memcpy(push.colour, colour, sizeof(push.colour));
        std::memcpy(push.fog_colour, a.fog_colour, sizeof(push.fog_colour));
        push.params[0] = static_cast<float>(mode);
        push.params[1] = layer;
        push.params[2] = a.sky_fog_end;
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdPushConstants(command, gpu_->layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(push), &push);
        vkCmdDraw(command, range.count, 1, range.first, 0);
    };

    if (a.skybox == world::Skybox::End) {
        const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        draw(gpu_->translucent, identity(), white, kEndSky, end_box_);
        return;
    }

    float sky[4];
    colour_of(a.sky_colour, sky);
    draw(gpu_->disc, identity(), sky, kSkyDisc, disc_);

    float glow[4];
    colour_of(a.sunrise_colour, glow);
    if (!(glow[3] <= 0.001f)) {
        const float side = std::sin(a.sun_angle) < 0.0f ? 180.0f : 0.0f;
        const Matrix4 model =
            rotate_x(radians(90.0f)) * rotate_z(radians(side + 90.0f)) * scale(1.0f, 1.0f, glow[3]);
        draw(gpu_->translucent, model, glow, kVertexColour, fan_);
    }

    const Matrix4 turned = rotate_y(radians(-90.0f));
    const float light[4] = {1.0f, 1.0f, 1.0f, a.rain_brightness};
    draw(gpu_->overlay, turned * rotate_x(a.sun_angle) * translate(0, 100, 0) * scale(30, 1, 30), light,
         kCelestial, sun_, 0.0f);
    draw(gpu_->overlay, turned * rotate_x(a.moon_angle) * translate(0, 100, 0) * scale(20, 1, 20), light,
         kCelestial, moons_, static_cast<float>(1 + std::clamp(a.moon_phase, 0, 7)));
    if (a.star_brightness > 0.0f) {
        const float b = a.star_brightness;
        const float stars[4] = {b, b, b, b};
        draw(gpu_->overlay, turned * rotate_x(a.star_angle), stars, kFlat, stars_);
    }
    if (a.dark_disc) {
        const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        draw(gpu_->disc, translate(0.0f, 12.0f, 0.0f), black, kSkyDisc, dark_disc_);
    }
}

void Sky::record_clouds(VkCommandBuffer command, const Camera& camera, uint32_t width,
                        uint32_t height, const world::Atmosphere& a) {
    if (cloud_mode_ == Clouds::Off || world::argb::alpha(a.cloud_colour) == 0) return;
    const int32_t range_blocks = cloud_range_ * 16;
    const auto radius = static_cast<int32_t>(std::ceil(static_cast<float>(range_blocks) / 12.0f));

    const float below = a.cloud_height - camera.eye[1];
    const float top = below + 4.0f;
    const int32_t side = top < 0.0f ? 0 : below > 0.0f ? 2 : 1;   // above, inside, below

    const float drift = static_cast<float>(cloud_ticks_ % (static_cast<int64_t>(kCloudSide) * 400)) +
                        cloud_partial_;
    double x = static_cast<double>(camera.eye[0]) + static_cast<double>(drift * 0.030000001f);
    double z = static_cast<double>(camera.eye[2]) + static_cast<double>(3.96f);
    const double span = kCloudSide * 12.0;
    x -= std::floor(x / span) * span;
    z -= std::floor(z / span) * span;
    const auto cell_x = static_cast<int32_t>(std::floor(x / 12.0));
    const auto cell_z = static_cast<int32_t>(std::floor(z / 12.0));
    const auto offset_x = static_cast<float>(x - static_cast<double>(static_cast<float>(cell_x) * 12.0f));
    const auto offset_z = static_cast<float>(z - static_cast<double>(static_cast<float>(cell_z) * 12.0f));
    const bool fancy = cloud_mode_ == Clouds::Fancy;

    if (cell_x != built_cell_[0] || cell_z != built_cell_[1] || side != built_side_ ||
        cloud_mode_ != built_mode_) {
        built_cell_[0] = cell_x;
        built_cell_[1] = cell_z;
        built_side_ = side;
        built_mode_ = cloud_mode_;
        // Nearest first, in rings, so the blend inside the layer comes out
        // the same as the game's.
        std::vector<uint32_t> faces;
        const auto face = [&](int32_t dx, int32_t dz, uint32_t direction, uint32_t flags) {
            faces.push_back((static_cast<uint32_t>(dx) & 0xFFFFu) |
                            ((static_cast<uint32_t>(dz) & 0xFFFFu) << 16));
            faces.push_back(direction | flags);
        };
        const auto cell = [&](int32_t dx, int32_t dz) {
            const int32_t tx = ((cell_x + dx) % 256 + 256) % 256;
            const int32_t tz = ((cell_z + dz) % 256 + 256) % 256;
            const uint8_t data = cloud_cells_[static_cast<size_t>(tz) * kCloudSide + static_cast<size_t>(tx)];
            if (data == 0) return;
            if (!fancy) {
                face(dx, dz, 0, 32);
                return;
            }
            if (side != 2) face(dx, dz, 1, 0);
            if (side != 0) face(dx, dz, 0, 0);
            if ((data & 8) && dz > 0) face(dx, dz, 2, 0);
            if ((data & 2) && dz < 0) face(dx, dz, 3, 0);
            if ((data & 1) && dx > 0) face(dx, dz, 4, 0);
            if ((data & 4) && dx < 0) face(dx, dz, 5, 0);
            if (std::abs(dx) <= 1 && std::abs(dz) <= 1) {
                for (uint32_t direction = 0; direction < 6; ++direction) face(dx, dz, direction, 16);
            }
        };
        for (int32_t ring = 0; ring <= 2 * radius; ++ring) {
            for (int32_t dx = -ring; dx <= ring; ++dx) {
                const int32_t dz = ring - std::abs(dx);
                if (dz < 0 || dz > radius || dx * dx + dz * dz > radius * radius) continue;
                if (dz != 0) cell(dx, -dz);
                cell(dx, dz);
            }
        }
        cloud_faces_ = static_cast<uint32_t>(faces.size() / 2);
        const VkDeviceSize bytes = std::max<VkDeviceSize>(faces.size() * sizeof(uint32_t), 16);
        if (gpu_->faces.size < bytes) {
            vkDeviceWaitIdle(device_.handle());
            gpu_->faces.destroy(device_.handle());
            gpu_->faces = vk::mapped_buffer(device_, bytes * 2, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
            VkDescriptorBufferInfo info{gpu_->faces.buffer, 0, VK_WHOLE_SIZE};
            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = gpu_->cloud_set;
            write.dstBinding = 0;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            write.pBufferInfo = &info;
            vkUpdateDescriptorSets(device_.handle(), 1, &write, 0, nullptr);
        }
        if (!faces.empty()) std::memcpy(gpu_->faces.mapped, faces.data(), faces.size() * sizeof(uint32_t));
    }
    if (cloud_faces_ == 0) return;

    CloudPush push{};
    const Matrix4 transform =
        camera_projection(camera, static_cast<float>(width) / static_cast<float>(height)) *
        camera_rotation(camera);
    std::memcpy(push.transform, transform.m, sizeof(push.transform));
    colour_of(a.cloud_colour, push.colour);
    push.offset[0] = -offset_x;
    push.offset[1] = below;
    push.offset[2] = -offset_z;
    push.offset[3] = a.cloud_fog_end;
    push.cell[0] = 12.0f;
    push.cell[1] = 4.0f;
    push.cell[2] = 12.0f;
    const VkViewport viewport{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height),
                              0.0f, 1.0f};
    const VkRect2D scissor{{0, 0}, {width, height}};
    vkCmdSetViewport(command, 0, 1, &viewport);
    vkCmdSetScissor(command, 0, 1, &scissor);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      fancy ? gpu_->fancy_clouds : gpu_->flat_clouds);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, gpu_->cloud_layout, 0, 1,
                            &gpu_->cloud_set, 0, nullptr);
    vkCmdPushConstants(command, gpu_->cloud_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(push), &push);
    vkCmdDraw(command, cloud_faces_ * 6, 1, 0, 0);
}

} // namespace mcpp::gpu
