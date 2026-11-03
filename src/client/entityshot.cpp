#include "client/entityshot.hpp"

#include "mcpp/entity/assets.hpp"
#include "mcpp/entity/motion.hpp"
#include "mcpp/entity/scene.hpp"
#include "mcpp/gpu/device.hpp"
#include "mcpp/gpu/entity_renderer.hpp"
#include "mcpp/gpu/world_renderer.hpp"
#include "mcpp/image_png.hpp"
#include "mcpp/protocol_versions.hpp"
#include "mcpp/skins.hpp"
#include "mcpp/world/atlas.hpp"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace client {
namespace {

constexpr int32_t kProtocol = mcpp::protocol::kTargetProtocol;

struct Shot {
    std::string name;
    float eye[3];
    float target[3];
};

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::stringstream text;
    text << input.rdbuf();
    return text.str();
}

// One entity on its own, from the four sides that show a model's mistakes: the
// front, its own right, straight down and three quarters round. No server,
// no world, nothing between the camera and the model.
int entityshot(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: entityshot <model> [out-prefix] [--skin <png>|<texture>] "
                     "[--models <file>] [--walk <phase>] [--yaw <head>] [--pitch <deg>] "
                     "[--close] [--swing <0..1>]\n";
        return 2;
    }
    const std::string model_name = argv[2];
    std::string prefix = "entity-" + model_name;
    std::string skin;
    std::string models_file;
    float walk = -1.0f;
    float head = 0.0f;
    float pitch = 0.0f;
    float swing = -1.0f;
    bool close = false;
    for (int i = 3; i < argc; ++i) {
        const std::string argument = argv[i];
        const auto next = [&]() -> std::string {
            if (i + 1 >= argc) throw std::runtime_error(argument + " needs a value");
            return argv[++i];
        };
        if (argument == "--skin") skin = next();
        else if (argument == "--models") models_file = next();
        else if (argument == "--walk") walk = std::stof(next());
        else if (argument == "--yaw") head = std::stof(next());
        else if (argument == "--pitch") pitch = std::stof(next());
        else if (argument == "--swing") swing = std::stof(next());
        else if (argument == "--close") close = true;
        else prefix = argument;
    }

    mcpp::entity::Assets assets;
    if (!assets.open(mcpp::entity::Assets::default_path(kProtocol))) {
        std::cerr << "no entity data; bake it with tools/bake_entities.py\n";
        return 1;
    }
    if (!models_file.empty()) assets.use_models(read_file(models_file));
    const mcpp::entity::Model* model = assets.models().find(model_name);
    if (model == nullptr) {
        std::cerr << std::format("no model {} in the data\n", model_name);
        return 1;
    }

    mcpp::entity::Texture texture;
    std::string texture_name = model->texture;
    if (!skin.empty() && std::filesystem::exists(skin)) {
        const std::string bytes = read_file(skin);
        if (!mcpp::skins::decode_png({reinterpret_cast<const uint8_t*>(bytes.data()),
                                      bytes.size()},
                                     texture)) {
            std::cerr << std::format("{} is not a png this client reads\n", skin);
            return 1;
        }
        if (model_name.starts_with("player") && !mcpp::skins::normalise(texture)) {
            std::cerr << std::format("{} is {}x{}, which is no skin\n", skin, texture.width,
                                     texture.height);
            return 1;
        }
        texture_name = "file:" + skin;
    } else {
        if (!skin.empty()) texture_name = skin;
        const auto* baked = assets.texture(texture_name);
        if (baked == nullptr) {
            std::cerr << std::format("no texture {}\n", texture_name);
            return 1;
        }
        texture = *baked;
    }

    mcpp::gpu::Device device;
    mcpp::gpu::Device::Options gpu_options;
    gpu_options.validation = true;
    device.create(gpu_options);
    mcpp::world::Atlas atlas;
    if (!atlas.open(mcpp::world::Atlas::default_path(kProtocol))) {
        std::cerr << "no atlas; bake one with tools/bake_atlas.py\n";
        return 1;
    }
    constexpr uint32_t kSide = 720;
    mcpp::gpu::WorldRenderer world(device, kSide, kSide, atlas);
    mcpp::gpu::EntityRenderer renderer(device, world.colour_format());
    renderer.upload_texture(texture_name, texture);
    renderer.set_font(assets.font());

    mcpp::entity::Motion motion;
    mcpp::Entity entity;
    entity.head_yaw = head;
    entity.pitch = pitch;
    motion.spawn(entity);
    motion.body_yaw = motion.old_body_yaw = 0.0f;
    if (walk >= 0.0f) {
        motion.walk_speed = motion.old_walk_speed = 1.0f;
        motion.walk_position = walk * 2.0f * 3.14159265f / 0.6662f;
    }
    if (swing >= 0.0f) {
        motion.swinging = true;
        motion.attack = motion.old_attack = swing;
    }

    const float height = model->height;
    const float middle = height * 0.5f;
    const float reach = std::max(height, model->width) * 1.9f;
    std::vector<Shot> shots = {
        {"front", {0.0f, middle, reach}, {0.0f, middle, 0.0f}},
        {"right", {-reach, middle, 0.0f}, {0.0f, middle, 0.0f}},
        {"back", {0.0f, middle, -reach}, {0.0f, middle, 0.0f}},
        {"top", {0.0f, middle + reach, 0.02f}, {0.0f, middle, 0.0f}},
        {"angle", {reach * 0.62f, middle + reach * 0.45f, reach * 0.72f}, {0.0f, middle, 0.0f}},
        // Nearly orthographic from (1, 1, 1), the way the wiki's renders are taken.
        {"iso", {30.0f, middle + 30.0f, 30.0f}, {0.0f, middle, 0.0f}},
    };
    if (close) {
        const float face = height * 0.82f;
        shots = {
            {"face", {0.0f, face, 1.1f}, {0.0f, face, 0.0f}},
            {"face-angle", {0.8f, face + 0.3f, 0.8f}, {0.0f, face, 0.0f}},
            {"shoulder", {-0.9f, height * 0.62f, 0.5f}, {-0.2f, height * 0.62f, 0.0f}},
            {"under", {0.3f, -0.4f, 0.9f}, {0.0f, 0.3f, 0.0f}},
        };
    }

    const double origin[3] = {0.0, 0.0, 0.0};
    for (const auto& shot : shots) {
        renderer.begin();
        mcpp::entity::Pose pose(*model);
        mcpp::entity::pose_humanoid(*model, motion, 1.0f, pose);
        std::vector<mcpp::entity::Vertex> vertices;
        mcpp::entity::build_mesh(*model, pose,
                                 mcpp::entity::model_transform(*model, motion, 1.0f, origin) ,
                                 vertices);
        mcpp::gpu::EntityRenderer::Draw draw;
        draw.texture = texture_name;
        draw.layer = model_name.starts_with("player")
                         ? mcpp::gpu::EntityRenderer::Layer::CutoutNoCull
                         : mcpp::gpu::EntityRenderer::Layer::Cutout;
        renderer.add(vertices, draw);

        mcpp::gpu::Camera camera{};
        for (int axis = 0; axis < 3; ++axis) {
            camera.eye[axis] = shot.eye[axis];
            camera.target[axis] = shot.target[axis];
        }
        camera.fov_degrees = shot.name == "iso" ? 2.8f : 50.0f;
        camera.near_plane = 0.05f;
        world.set_in_world([&](VkCommandBuffer command, const float* view_projection,
                               uint32_t width, uint32_t height_px, const float* fog,
                               const mcpp::gpu::Camera&) {
            renderer.record(command, view_projection, width, height_px, fog, 0.0f, 0.0f);
        });
        const auto pixels = world.render(camera);
        const std::string out = std::format("{}-{}.png", prefix, shot.name);
        mcpp::write_png(out, kSide, kSide, pixels);
        std::cout << std::format("- {}: {} vertices\n", out, vertices.size());
    }
    return 0;
}

} // namespace

int entity_command(int argc, char** argv) {
    if (argc < 2) return -1;
    const std::string command = argv[1];
    if (command == "entityshot") return entityshot(argc, argv);
    return -1;
}

} // namespace client
