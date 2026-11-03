#include "client/entityshot.hpp"

#include "client/common.hpp"

#include "mcpp/entity/assets.hpp"
#include "mcpp/entity/humanoid.hpp"
#include "mcpp/entity/scene.hpp"
#include "mcpp/session.hpp"
#include "mcpp/gpu/device.hpp"
#include "mcpp/gpu/entity_renderer.hpp"
#include "mcpp/gpu/world_renderer.hpp"
#include "mcpp/image_png.hpp"
#include "mcpp/protocol_versions.hpp"
#include "mcpp/skins.hpp"
#include "mcpp/world/atlas.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <optional>
#include <thread>
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
                     "[--models <file>] [--property <file>] [--walk <phase>] [--yaw <head>] [--pitch <deg>] "
                     "[--close] [--crouch] [--age <ticks>] [--swing <0..1>]\n";
        return 2;
    }
    const std::string model_name = argv[2];
    std::string prefix = "entity-" + model_name;
    std::string skin;
    std::string models_file;
    std::string property;
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
        else if (argument == "--property") property = next();
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
    if (!property.empty()) {
        // The player list's textures property, fetched the way the client
        // fetches it in play: decoded, downloaded from Mojang's host, cached.
        const auto textures = mcpp::skins::decode_property(read_file(property));
        if (!textures || textures->skin_url.empty()) {
            std::cerr << "no skin in that property\n";
            return 1;
        }
        std::cout << std::format("- {} skin at {}\n", textures->slim ? "slim" : "wide",
                                 textures->skin_url);
        mcpp::skins::Cache cache;
        const mcpp::entity::Texture* fetched = nullptr;
        for (int i = 0; i < 300 && fetched == nullptr && !cache.failed(textures->skin_url); ++i) {
            fetched = cache.get(textures->skin_url);
            if (fetched == nullptr) std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        if (fetched == nullptr) {
            std::cerr << "the skin did not arrive\n";
            return 1;
        }
        const int32_t layer = renderer.upload_skin(*fetched);
        if (layer < 0) return 1;
        texture = mcpp::gpu::EntityRenderer::kSkinBit | static_cast<uint32_t>(layer);
    } else if (!skin.empty() && std::filesystem::exists(skin)) {
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

using Clock = std::chrono::steady_clock;

mcpp::SessionOptions options_for(const std::string& target, std::string username) {
    const auto address = parse_address(target);
    mcpp::SessionOptions options;
    options.host = address.host;
    options.port = address.port;
    options.profile = signed_in_profile(username);
    options.username = username;
    return options;
}

// Answers whatever has arrived without waiting for more.
void drain(mcpp::Session& session) {
    while (session.has_data()) {
        if (!session.pump()) return;
    }
}

// A second player that does what a person would on camera: stands and looks
// round, walks out and back, sneaks, punches while looking up and down, and
// walks while punching. Twenty steps a second, each sent the way the game
// sends its own movement.
int entitybot(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "usage: entitybot <host[:port]> <name>\n";
        return 2;
    }
    auto options = options_for(argv[2], argv[3]);
    double start[3] = {0.0, 0.0, 0.0};
    float start_yaw = 0.0f;
    bool placed = false;
    mcpp::SessionEvents events;
    events.on_status = [](std::string_view line) { std::cout << "- " << line << '\n'; };
    events.player.on_position = [&](double x, double y, double z, float yaw, float) {
        start[0] = x;
        start[1] = y;
        start[2] = z;
        start_yaw = yaw;
        placed = true;
    };
    mcpp::Session session(options, events);
    session.connect();
    const auto joined = Clock::now();
    while (!placed && Clock::now() - joined < std::chrono::seconds(10)) {
        drain(session);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    // Wait to be put where the test is, then a moment for the camera to see
    // us arrive.
    std::cout << "- waiting for a teleport away from spawn\n";
    const double spawn_x = start[0];
    while (std::abs(start[0] - spawn_x) < 100.0 &&
           Clock::now() - joined < std::chrono::seconds(60)) {
        drain(session);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    for (int i = 0; i < 40; ++i) {
        drain(session);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    std::cout << std::format("- start at {:.2f} {:.2f} {:.2f}\n", start[0], start[1], start[2]);

    constexpr double kWalk = 0.21585;   // blocks a tick at walking pace on flat ground
    constexpr double kSneak = 0.0648;
    double x = start[0];
    double z = start[2];
    bool sneaking = false;
    auto next = Clock::now();
    constexpr int kTicks = 360;
    for (int t = 0; t < kTicks; ++t) {
        float yaw = start_yaw;
        float pitch = 0.0f;
        bool sneak = false;
        bool swing = false;
        double dx = 0.0;
        double dz = 0.0;
        if (t < 60) {
            // Look round: the head first, the body catching up.
            yaw = start_yaw + 70.0f * std::sin(static_cast<float>(t) / 60.0f * 6.2831853f);
            pitch = -20.0f * std::sin(static_cast<float>(t) / 30.0f * 6.2831853f);
        } else if (t < 120) {
            yaw = -90.0f;   // east
            dx = kWalk;
        } else if (t < 180) {
            yaw = 90.0f;    // west, back where it came from
            dx = -kWalk;
        } else if (t < 240) {
            sneak = true;
            yaw = 0.0f;     // south, sneaking
            dz = t < 220 ? kSneak : 0.0;
        } else if (t < 300) {
            yaw = 180.0f;
            pitch = 40.0f * std::sin(static_cast<float>(t - 240) / 60.0f * 6.2831853f);
            swing = (t - 240) % 8 == 0;
        } else {
            yaw = -45.0f;
            dx = kWalk * 0.7071;
            dz = kWalk * 0.7071;
            swing = (t - 300) % 10 == 0;
        }
        x += dx;
        z += dz;
        if (sneak != sneaking) {
            session.send_input_state(sneak ? 0x20 : 0x00);
            sneaking = sneak;
        }
        session.send_position(x, start[1], z, yaw, pitch, true);
        if (swing) session.send_swing();
        drain(session);
        next += std::chrono::milliseconds(50);
        std::this_thread::sleep_until(next);
    }
    if (sneaking) session.send_input_state(0);
    for (int i = 0; i < 20; ++i) {
        drain(session);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    session.close();
    std::cout << "- script done\n";
    return 0;
}

// Joins, stands still and writes every packet with the moment it arrived:
// the recording itself in the session's format, and beside it one
// millisecond count per packet.
int entityrecord(int argc, char** argv) {
    if (argc < 6) {
        std::cerr << "usage: entityrecord <host[:port]> <name> <out.plrc> <seconds>\n";
        return 2;
    }
    auto options = options_for(argv[2], argv[3]);
    const std::filesystem::path file = argv[4];
    const double seconds = std::stod(argv[5]);
    options.record = file;
    mcpp::SessionEvents events;
    events.on_status = [](std::string_view line) { std::cout << "- " << line << '\n'; };
    mcpp::Session session(options, events);
    session.connect();
    std::ofstream times(file.string() + ".times");
    const auto started = Clock::now();
    size_t packets = 0;
    while (Clock::now() - started < std::chrono::duration<double>(seconds)) {
        if (!session.has_data()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        const auto at = Clock::now();
        if (!session.pump()) break;
        times << std::chrono::duration_cast<std::chrono::milliseconds>(at - started).count()
              << '\n';
        ++packets;
    }
    session.close();
    std::cout << std::format("- {} packets in {:.0f} s to {}\n", packets, seconds, file.string());
    return packets == 0 ? 1 : 0;
}

// Plays a recording back through the scene the client draws with, tick by
// tick on the recorded clock, and takes a picture at each tick asked for,
// from the recording player's eyes or from a camera given.
int entityreplay(int argc, char** argv) {
    if (argc < 5) {
        std::cerr << "usage: entityreplay <file.plrc> <out-prefix> <tick>[,<tick>...] "
                     "[--camera x,y,z,tx,ty,tz] [--width w] [--height h]\n";
        return 2;
    }
    const std::filesystem::path file = argv[2];
    const std::string prefix = argv[3];
    std::vector<int> wanted;
    {
        std::stringstream list(argv[4]);
        std::string item;
        while (std::getline(list, item, ',')) wanted.push_back(std::stoi(item));
    }
    std::array<float, 6> fixed{};
    bool has_camera = false;
    uint32_t width = 960;
    uint32_t height = 540;
    for (int i = 5; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--camera" && i + 1 < argc) {
            std::stringstream values(argv[++i]);
            std::string item;
            for (float& f : fixed) {
                std::getline(values, item, ',');
                f = std::stof(item);
            }
            has_camera = true;
        } else if (argument == "--width" && i + 1 < argc) {
            width = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (argument == "--height" && i + 1 < argc) {
            height = static_cast<uint32_t>(std::stoul(argv[++i]));
        }
    }
    std::vector<int64_t> times;
    {
        std::ifstream input(file.string() + ".times");
        int64_t t = 0;
        while (input >> t) times.push_back(t);
    }

    mcpp::entity::Assets assets;
    mcpp::world::BlockTable table;
    mcpp::world::Atlas atlas;
    if (!assets.open(mcpp::entity::Assets::default_path(kProtocol)) ||
        !table.open(mcpp::world::BlockTable::default_path(kProtocol)) ||
        !atlas.open(mcpp::world::Atlas::default_path(kProtocol))) {
        std::cerr << "baked data missing\n";
        return 1;
    }
    mcpp::skins::Cache skins;
    mcpp::entity::Scene scene(
        mcpp::entity::load_entity_types(mcpp::entity::default_registries_path(kProtocol)), &skins);
    std::vector<mcpp::world::Chunk> chunks;
    double eye[3] = {0.0, 0.0, 0.0};
    float look[2] = {0.0f, 0.0f};

    mcpp::SessionOptions options;
    options.replay = file;
    options.username = "PlasmaD2";
    mcpp::SessionEvents events;
    events.world.on_chunk = [&](const mcpp::world::Chunk& chunk) { chunks.push_back(chunk); };
    events.player.on_position = [&](double x, double y, double z, float yaw, float pitch) {
        eye[0] = x;
        eye[1] = y + 1.62;
        eye[2] = z;
        look[0] = yaw;
        look[1] = pitch;
    };
    events.entities.on_add = [&](const mcpp::Entity& e) { scene.add(e); };
    events.entities.on_moved = [&](const mcpp::EntityMove& m) { scene.moved(m); };
    events.entities.on_head = [&](int32_t id, float yaw) { scene.head(id, yaw); };
    events.entities.on_remove = [&](int32_t id) { scene.remove(id); };
    events.entities.on_animate = [&](int32_t id, uint8_t a) { scene.animate(id, a); };
    events.entities.on_hurt = [&](int32_t id, float) { scene.hurt(id); };
    events.entities.on_damage = [&](int32_t id) { scene.damaged(id); };
    events.entities.on_event = [&](int32_t id, uint8_t s) { scene.event(id, s); };
    events.entities.on_player_info = [&](const mcpp::PlayerInfo& info) { scene.player_info(info); };
    events.entities.on_player_remove = [&](std::string_view uuid) { scene.player_gone(uuid); };
    mcpp::Session session(options, events);
    session.connect();

    mcpp::gpu::Device device;
    mcpp::gpu::Device::Options gpu_options;
    device.create(gpu_options);
    std::optional<mcpp::gpu::WorldRenderer> world;
    std::optional<mcpp::gpu::EntityRenderer> renderer;
    size_t meshed = 0;

    const auto light = [&](int32_t x, int32_t y, int32_t z) -> std::array<uint8_t, 2> {
        for (const auto& chunk : chunks) {
            if (chunk.x != (x >> 4) || chunk.z != (z >> 4)) continue;
            const int32_t index = chunk.index_of(y);
            return {chunk.sky_light_at(x & 15, index, z & 15),
                    chunk.block_light_at(x & 15, index, z & 15)};
        }
        return {15, 0};
    };

    // A tick's packets are applied before it ticks, the way the game does.
    size_t packet = 0;
    int64_t clock = times.empty() ? 0 : times.front();
    const int last = wanted.empty() ? 0 : *std::max_element(wanted.begin(), wanted.end());
    for (int tick = 0; tick <= last; ++tick) {
        while (packet < times.size() && times[packet] < clock + 50) {
            if (!session.pump()) break;
            ++packet;
        }
        scene.tick();
        clock += 50;
        if (std::find(wanted.begin(), wanted.end(), tick) == wanted.end()) continue;
        if (!world) {
            world.emplace(device, width, height, atlas);
            renderer.emplace(device, world->colour_format());
            renderer->load(assets);
            scene.bind(*renderer);
        }
        if (chunks.size() != meshed) {
            world->upload(mesh_world(chunks, table, nullptr));
            meshed = chunks.size();
        }
        mcpp::gpu::Camera camera{};
        if (has_camera) {
            for (int axis = 0; axis < 3; ++axis) {
                camera.eye[axis] = fixed[axis];
                camera.target[axis] = fixed[3 + axis];
            }
        } else {
            const float yaw = look[0] * 3.14159265f / 180.0f;
            const float pitch = look[1] * 3.14159265f / 180.0f;
            const float f[3] = {-std::sin(yaw) * std::cos(pitch), -std::sin(pitch),
                                std::cos(yaw) * std::cos(pitch)};
            for (int axis = 0; axis < 3; ++axis) {
                camera.eye[axis] = static_cast<float>(eye[axis]);
                camera.target[axis] = camera.eye[axis] + f[axis];
            }
        }
        camera.fov_degrees = 70.0f;
        camera.near_plane = 0.05f;
        mcpp::entity::View view;
        for (int axis = 0; axis < 3; ++axis) {
            view.eye[axis] = camera.eye[axis];
            view.forward[axis] = camera.target[axis] - camera.eye[axis];
        }
        view.partial = 1.0f;
        // Skins come in on the cache's worker; a first pass asks for them.
        for (int pass = 0; pass < 2; ++pass) {
            renderer->begin();
            scene.draw(*renderer, view, light);
            if (pass == 0) std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        world->set_in_world([&](VkCommandBuffer command, const float* view_projection,
                                uint32_t w, uint32_t h, const float* fog,
                                const mcpp::gpu::Camera& cam) {
            const double at[3] = {cam.eye[0], cam.eye[1], cam.eye[2]};
            renderer->record(command, view_projection, at, w, h, fog, 0.0f, 0.0f);
        });
        const auto pixels = world->render(camera);
        const std::string out = std::format("{}-{:04}.png", prefix, tick);
        mcpp::write_png(out, width, height, pixels);
        std::cout << std::format("- {}: {} drawn of {}, camera {:.2f} {:.2f} {:.2f}\n", out,
                                 scene.stats().drawn, scene.stats().tracked, camera.eye[0],
                                 camera.eye[1], camera.eye[2]);
    }
    return 0;
}

} // namespace

int entity_command(int argc, char** argv) {
    if (argc < 2) return -1;
    const std::string command = argv[1];
    if (command == "entityshot") return entityshot(argc, argv);
    if (command == "entitybot") return entitybot(argc, argv);
    if (command == "entityrecord") return entityrecord(argc, argv);
    if (command == "entityreplay") return entityreplay(argc, argv);
    return -1;
}

} // namespace client
