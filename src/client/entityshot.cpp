#include "client/entityshot.hpp"

#include "mcpp/entity/assets.hpp"
#include "mcpp/entity/humanoid.hpp"
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
                     "[--close] [--crouch] [--age <ticks>] [--swing <0..1>]\n";
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
    float age = 0.0f;
    bool close = false;
    bool crouch = false;
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
        else if (argument == "--crouch") crouch = true;
        else if (argument == "--age") age = std::stof(next());
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
    renderer.load(assets);
    const int32_t model_index = renderer.model(model_name);

    const bool player = model_name == "player" || model_name == "player_slim";
    uint32_t texture = 0;
    if (!skin.empty() && std::filesystem::exists(skin)) {
        const std::string bytes = read_file(skin);
        mcpp::entity::Texture image;
        if (!mcpp::skins::decode_png({reinterpret_cast<const uint8_t*>(bytes.data()),
                                      bytes.size()},
                                     image)) {
            std::cerr << std::format("{} is not a png this client reads\n", skin);
            return 1;
        }
        if (player && !mcpp::skins::normalise(image)) {
            std::cerr << std::format("{} is {}x{}, which is no skin\n", skin, image.width,
                                     image.height);
            return 1;
        }
        const int32_t layer = renderer.upload_skin(image);
        if (layer < 0) return 1;
        texture = mcpp::gpu::EntityRenderer::kSkinBit | static_cast<uint32_t>(layer);
    } else {
        if (skin.empty()) {
            skin = model_name == "player_slim" ? "player/slim/alex" : "player/wide/steve";
        }
        const int32_t baked = renderer.texture(skin);
        if (baked < 0) {
            std::cerr << std::format("no texture {}\n", skin);
            return 1;
        }
        texture = static_cast<uint32_t>(baked);
    }

    mcpp::entity::HumanoidState state;
    state.y_rot = head;
    state.x_rot = pitch;
    state.crouching = crouch;
    state.age_in_ticks = age;
    if (walk >= 0.0f) {
        state.walk_speed = 1.0f;
        state.walk_position = walk * 2.0f * 3.14159265f / 0.6662f;
    }
    if (swing >= 0.0f) state.attack_time = swing;
    mcpp::entity::Pose pose(*model);
    const auto parts = mcpp::entity::HumanoidParts::of(*model);
    if (parts.complete()) {
        if (player) mcpp::entity::animate_player(*model, parts, state, pose);
        else mcpp::entity::animate_humanoid(*model, parts, state, pose);
    }

    const float height = 1.8f;
    const float middle = height * 0.5f;
    const float reach = height * 1.9f;
    std::vector<Shot> shots = {
        {"front", {0.0f, middle, reach}, {0.0f, middle, 0.0f}},
        {"right", {-reach, middle, 0.0f}, {0.0f, middle, 0.0f}},
        {"back", {0.0f, middle, -reach}, {0.0f, middle, 0.0f}},
        {"top", {0.0f, middle + reach, 0.02f}, {0.0f, middle, 0.0f}},
        {"angle", {reach * 0.62f, middle + reach * 0.45f, reach * 0.72f}, {0.0f, middle, 0.0f}},
    };
    if (close) {
        const float face = height * 0.82f;
        shots = {
            {"face", {0.0f, face, 1.1f}, {0.0f, face, 0.0f}},
            {"face-angle", {0.8f, face + 0.3f, 0.8f}, {0.0f, face, 0.0f}},
            {"shoulder", {-0.9f, height * 0.62f, 0.5f}, {-0.2f, height * 0.62f, 0.0f}},
            {"under", {0.3f, -0.4f, 0.9f}, {0.0f, 0.3f, 0.0f}},
            {"hip", {-0.7f, 0.75f, 0.45f}, {0.0f, 0.75f, 0.0f}},
        };
    }

    std::vector<mcpp::entity::Matrix> matrices(model->parts.size());
    for (const auto& shot : shots) {
        mcpp::gpu::Camera camera{};
        for (int axis = 0; axis < 3; ++axis) {
            camera.eye[axis] = shot.eye[axis];
            camera.target[axis] = shot.target[axis];
        }
        camera.fov_degrees = 50.0f;
        camera.near_plane = 0.05f;

        // Standing at the origin facing south, the way the game spawns a
        // model with no yaw: the camera's own position comes back out in
        // the renderer.
        renderer.begin();
        using namespace mcpp::entity;
        Matrix root = translation(-shot.eye[0], -shot.eye[1] + (crouch ? -0.125f : 0.0f),
                                  -shot.eye[2]);
        root = multiply(root, rotation_y(180.0f * 3.14159265f / 180.0f));
        root = multiply(root, scaling(-1.0f, -1.0f, 1.0f));
        if (player) root = multiply(root, scaling(0.9375f, 0.9375f, 0.9375f));
        root = multiply(root, translation(0.0f, -1.501f, 0.0f));
        pose_matrices(*model, pose, root, matrices);
        mcpp::gpu::EntityRenderer::Instance instance;
        instance.model = model_index;
        instance.texture = texture;
        instance.type = player ? mcpp::gpu::EntityRenderer::Type::Translucent
                               : mcpp::gpu::EntityRenderer::Type::CutoutNoCull;
        renderer.add(instance, matrices);

        world.set_in_world([&](VkCommandBuffer command, const float* view_projection,
                               uint32_t width, uint32_t height_px, const float* fog,
                               const mcpp::gpu::Camera& cam) {
            const double eye[3] = {cam.eye[0], cam.eye[1], cam.eye[2]};
            renderer.record(command, view_projection, eye, width, height_px, fog, 0.0f, 0.0f);
        });
        const auto pixels = world.render(camera);
        const std::string out = std::format("{}-{}.png", prefix, shot.name);
        mcpp::write_png(out, kSide, kSide, pixels);
        std::cout << std::format("- {}\n", out);
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
