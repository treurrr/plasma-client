#include "client/entityshot.hpp"

#include "client/common.hpp"

#include "mcpp/entity/assets.hpp"
#include "mcpp/entity/held_items.hpp"
#include "mcpp/entity/equipment.hpp"
#include "mcpp/entity/humanoid.hpp"
#include "mcpp/entity/mobs.hpp"
#include "mcpp/entity/scene.hpp"
#include "mcpp/session.hpp"
#include "mcpp/gpu/device.hpp"
#include "mcpp/gpu/entity_renderer.hpp"
#include "mcpp/gpu/hand.hpp"
#include "mcpp/gpu/world_renderer.hpp"
#include "mcpp/image_png.hpp"
#include "mcpp/protocol_versions.hpp"
#include "mcpp/skins.hpp"
#include "mcpp/ui/items.hpp"
#include "mcpp/world/atlas.hpp"
#include "mcpp/world/atmosphere.hpp"
#include "mcpp/world/block_table.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <memory>
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
        std::cerr << "usage: entityshot <model|minecraft:type> [out-prefix] "
                     "[--skin <png>|<texture>] [--models <file>] [--property <file>] "
                     "[--walk <phase>] [--yaw <head>] [--pitch <deg>] [--close] [--crouch] "
                     "[--age <ticks>] [--swing <0..1>] [--baby] [--armour <asset>] "
                     "[--hold <item>]\n";
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
    bool baby = false;
    bool gear = false;
    std::string armour;
    std::string held;
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
        else if (argument == "--baby") baby = true;
        else if (argument == "--gear") gear = true;
        else if (argument == "--hold") held = next();
        else if (argument == "--armour") armour = next();
        else prefix = argument;
    }

    mcpp::entity::Assets assets;
    if (!assets.open(mcpp::entity::Assets::default_path(kProtocol))) {
        std::cerr << "no entity data; bake it with tools/bake_entities.py\n";
        return 1;
    }
    if (!models_file.empty()) assets.use_models(read_file(models_file));
    // A registry name ("minecraft:zombie") draws the whole type: its own layer,
    // the layers the game stacks on it, and its armour. A bare layer name
    // ("zombie#helmet") draws that one layer alone.
    const mcpp::entity::MobType* row = mcpp::entity::mob_type(model_name);
    std::string layer_name = model_name;
    if (row != nullptr) {
        layer_name = baby && !row->baby_layer.empty() ? std::string(row->baby_layer)
                                                     : std::string(row->layer);
        if (skin.empty()) skin = std::string(row->texture);
    }
    const mcpp::entity::Model* model = assets.models().find(layer_name);
    if (model == nullptr) {
        std::cerr << std::format("no model {} in the data\n", layer_name);
        return 1;
    }
    int32_t held_item = -1;
    mcpp::entity::EquipmentAssets equipment;
    const bool has_equipment =
        equipment.open(mcpp::entity::EquipmentAssets::default_path(kProtocol));

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
    std::optional<mcpp::gpu::HandRenderer> hand;
    if (!held.empty()) {
        hand.emplace(device, world.colour_format(), world.depth_format(),
                     mcpp::gpu::HandRenderer::default_path(kProtocol));
        if (!hand->has_items()) {
            std::cerr << "no held item data; bake it with tools/bake_held.py\n";
            return 1;
        }
        std::ifstream items(mcpp::entity::default_registries_path(kProtocol));
        std::string line;
        int32_t at = 0;
        while (std::getline(items, line)) {
            constexpr std::string_view kPrefix = "minecraft:item\t";
            if (!line.starts_with(kPrefix)) continue;
            if (line.substr(kPrefix.size()) == "minecraft:" + held) {
                held_item = at;
                break;
            }
            ++at;
        }
        if (held_item < 0) {
            std::cerr << std::format("no item {}\n", held);
            return 1;
        }
    }
    const int32_t model_index = renderer.model(layer_name);

    const bool player = layer_name == "player" || layer_name == "player_slim";
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
            skin = layer_name == "player_slim" ? "player/slim/alex" : "player/wide/steve";
        }
        const int32_t baked = renderer.texture(skin);
        if (baked < 0) {
            std::cerr << std::format("no texture {}\n", skin);
            return 1;
        }
        texture = static_cast<uint32_t>(baked);
    }

    mcpp::entity::MobState state;
    state.y_rot = head;
    state.x_rot = pitch;
    state.crouching = crouch;
    state.age_in_ticks = age;
    state.baby = baby;
    state.age_scale = baby ? 0.5f : 1.0f;
    // Enough of a sheep, a wolf and a cat to show their wool and their collars.
    state.collar_color = 14;
    state.wool_color = 4;
    state.saddled = gear;
    state.powered = gear;
    if (walk >= 0.0f) {
        state.walk_speed = 1.0f;
        state.walk_position = walk * 2.0f * 3.14159265f / 0.6662f;
    }
    if (swing >= 0.0f) state.attack_time = swing;
    if (!armour.empty()) state.right_arm_pose = mcpp::entity::ArmPose::Item;
    mcpp::entity::Pose pose(*model);
    auto animator = mcpp::entity::make_mob_model(layer_name, *model);
    if (animator == nullptr) {
        std::cerr << std::format("no model class animates {}\n", layer_name);
        return 1;
    }
    animator->animate(state, pose);

    // Every layer this shot draws over the body, in the order the game's own
    // renderer adds them: the base, then each overlay, then the armour.
    struct Over {
        const mcpp::entity::Model* model = nullptr;
        std::unique_ptr<mcpp::entity::MobModel> animator;
        int32_t index = -1;
        uint32_t texture = 0;
        uint32_t colour = 0xFFFFFFFFu;
        mcpp::gpu::EntityRenderer::Type type = mcpp::gpu::EntityRenderer::Type::CutoutNoCull;
        bool eyes = false;
    };
    std::vector<Over> overs;
    const auto add_over = [&](std::string_view name, std::string_view texture_name,
                              uint32_t colour, mcpp::gpu::EntityRenderer::Type type, bool eyes) {
        const mcpp::entity::Model* data = assets.models().find(name);
        const int32_t index = renderer.model(name);
        const int32_t baked = renderer.texture(texture_name);
        if (data == nullptr || index < 0 || baked < 0) {
            std::cerr << std::format("- skipped layer {} ({})\n", name, texture_name);
            return;
        }
        Over over;
        over.model = data;
        over.animator = mcpp::entity::make_mob_model(name, *data);
        over.index = index;
        over.texture = static_cast<uint32_t>(baked);
        over.colour = colour;
        over.type = type;
        over.eyes = eyes;
        if (over.animator == nullptr) {
            std::cerr << std::format("- no model class animates {}\n", name);
            return;
        }
        overs.push_back(std::move(over));
    };
    if (row != nullptr) {
        for (const mcpp::entity::MobOverlay& one : row->overlays) {
            if (one.when != nullptr && !one.when(state)) continue;
            std::string name(one.layer.empty() ? row->layer : one.layer);
            if (baby) {
                if (!one.baby_layer.empty()) name = std::string(one.baby_layer);
                else if (one.layer.empty() && !row->baby_layer.empty()) {
                    name = std::string(row->baby_layer);
                }
            }
            const uint32_t colour = one.colour != nullptr ? one.colour(state) : 0xFFFFFFFFu;
            if (colour == 0) continue;
            add_over(name, one.texture, colour, one.render, one.kind == mcpp::entity::Overlay::Eyes);
        }
    }
    {
        const bool wears_armour = (row != nullptr && row->humanoid_armour) || player;
        // `HumanoidArmorLayer`: one asset's worth of armour, in every slot that
        // asset has a layer for, which is what a full set looks like.
        if (wears_armour && !armour.empty() && has_equipment) {
            struct Piece {
                std::string_view suffix;
                mcpp::entity::LayerType type;
            };
            const Piece pieces[] = {
                {"#chestplate", mcpp::entity::LayerType::Humanoid},
                {"#leggings", mcpp::entity::LayerType::HumanoidLeggings},
                {"#boots", mcpp::entity::LayerType::Humanoid},
                {"#helmet", mcpp::entity::LayerType::Humanoid},
            };
            for (const Piece& piece : pieces) {
                const std::string name = layer_name + std::string(piece.suffix);
                for (const auto& one : equipment.layers(armour, piece.type)) {
                    const uint32_t colour = mcpp::entity::equipment_layer_colour(one, 0);
                    if (colour == 0) continue;
                    add_over(name, one.texture, colour,
                             mcpp::gpu::EntityRenderer::Type::CutoutNoCull, false);
                }
            }
        }
    }

    float height = 1.8f;
    if (row != nullptr) {
        std::ifstream sizes(mcpp::entity::default_registries_path(kProtocol).parent_path() /
                            std::format("entity_dimensions-{}.txt", kProtocol));
        std::string line;
        while (std::getline(sizes, line)) {
            if (!line.starts_with(model_name) || line.size() <= model_name.size() ||
                line[model_name.size()] != ' ') {
                continue;
            }
            std::istringstream row_in(line.substr(model_name.size()));
            float width = 0.0f;
            float tall = 0.0f;
            if (row_in >> width >> tall) height = std::max(tall, width);
            break;
        }
        if (baby) height *= 0.5f;
    }
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
        Placement place;
        place.matrix = root;
        place.shape = Placement::kAffine;
        animator->scale(state, place);
        root = place.matrix;
        root = multiply(root, translation(0.0f, -1.501f, 0.0f));
        pose_matrices(*model, pose, root, matrices);
        mcpp::gpu::EntityRenderer::Instance instance;
        instance.model = model_index;
        instance.texture = texture;
        instance.type = row != nullptr          ? row->render
                        : player                ? mcpp::gpu::EntityRenderer::Type::Translucent
                                                : mcpp::gpu::EntityRenderer::Type::CutoutNoCull;
        instance.colour = animator->tint(state);
        renderer.add(instance, matrices);
        for (const Over& over : overs) {
            Pose layer_pose(*over.model);
            over.animator->animate(state, layer_pose);
            std::vector<Matrix> layer_matrices(over.model->parts.size());
            pose_matrices(*over.model, layer_pose, root, layer_matrices);
            mcpp::gpu::EntityRenderer::Instance one = instance;
            one.model = over.index;
            one.texture = over.texture;
            one.type = over.type;
            one.colour = over.colour;
            if (over.eyes) {
                one.sky_light = 15;
                one.block_light = 15;
            }
            renderer.add(one, layer_matrices);
        }

        if (hand && held_item >= 0) {
            hand->begin();
            const auto chain = animator->hand_chain(false);
            if (!chain.empty()) {
                const int32_t last = chain.back();
                const HandModel kind = layer_name == "player_slim" ? HandModel::SlimAvatar
                                                                   : HandModel::Plain;
                const float nudge = hand_arm_offset(kind, false);
                pose[last].x += nudge;
                pose_matrices(*model, pose, root, matrices);
                pose[last].x -= nudge;
                const Matrix arm = hand_after_chain(matrices[static_cast<size_t>(last)], kind,
                                                    false, pose[last].x_rot);
                const auto display = hand->item_display(held_item, false, true);
                constexpr float kFull[3] = {1.0f, 1.0f, 1.0f};
                hand->add_world_item(held_item, held_item_placement(arm, false, display), kFull);
                pose_matrices(*model, pose, root, matrices);
            }
        }
        world.set_in_world([&](VkCommandBuffer command, const float* view_projection,
                               uint32_t width, uint32_t height_px, const float* fog,
                               const mcpp::gpu::Camera& cam) {
            const double eye[3] = {cam.eye[0], cam.eye[1], cam.eye[2]};
            renderer.record(command, view_projection, eye, width, height_px, fog, 0.0f, 0.0f);
            if (hand && held_item >= 0) {
                hand->record_world(command, view_projection, eye, width, height_px);
            }
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

// Every mob the table knows, summoned on the vanilla server and drawn: the
// client is the one that has to recognise the type, read what the server says
// about it, find its layers and queue them. Every entity it summons is killed
// again before it returns, whether it passed or not.
int entitymobs(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "usage: entitymobs <host[:port]> <name> [console.in] [out-dir]\n";
        return 2;
    }
    const std::filesystem::path console =
        argc > 4 ? argv[4] : std::filesystem::path(std::getenv("HOME")) / "plasma-testserver" /
                                 "console.in";
    const std::filesystem::path out_dir = argc > 5 ? argv[5] : "";
    // Far from spawn and from every other agent's spot. The server picks the
    // height: dropped into the air, its own flight check would throw us off.
    constexpr double kX = 7413.0;
    constexpr double kZ = -7413.0;
    const std::string tag = "plasmaH";

    const auto say = [&console](const std::string& line) {
        std::ofstream(console, std::ios::app) << line << '\n';
    };
    // Everything this test made, gone again, however it ends.
    struct Sweep {
        std::function<void()> run;
        ~Sweep() { run(); }
    } sweep{[&] { say(std::format("kill @e[tag={}]", tag)); }};

    mcpp::entity::Assets assets;
    mcpp::world::Atlas atlas;
    if (!assets.open(mcpp::entity::Assets::default_path(kProtocol)) ||
        !atlas.open(mcpp::world::Atlas::default_path(kProtocol))) {
        std::cerr << "baked data missing\n";
        return 1;
    }
    mcpp::entity::EquipmentAssets equipment;
    equipment.open(mcpp::entity::EquipmentAssets::default_path(kProtocol));

    mcpp::gpu::Device device;
    mcpp::gpu::Device::Options gpu_options;
    device.create(gpu_options);
    constexpr uint32_t kSide = 512;
    mcpp::gpu::WorldRenderer world(device, kSide, kSide, atlas);
    mcpp::gpu::EntityRenderer renderer(device, world.colour_format());
    renderer.load(assets);

    auto types = mcpp::entity::load_entity_types(mcpp::entity::default_registries_path(kProtocol));
    mcpp::entity::Scene scene(types, nullptr);
    scene.bind(renderer, &equipment);

    auto options = options_for(argv[2], argv[3]);
    bool placed = false;
    double me_at[3] = {0.0, 0.0, 0.0};
    std::vector<int32_t> alive;
    int32_t summoning = -1;
    mcpp::SessionEvents events;
    events.on_status = [](std::string_view line) { std::cout << "- " << line << '\n'; };
    events.player.on_position = [&](double x, double y, double z, float, float) {
        me_at[0] = x;
        me_at[1] = y;
        me_at[2] = z;
        placed = true;
    };
    events.entities.on_add = [&](const mcpp::Entity& entity) {
        scene.add(entity);
        if (entity.type == summoning) alive.push_back(entity.id);
    };
    events.entities.on_moved = [&](const mcpp::EntityMove& move) { scene.moved(move); };
    events.entities.on_head = [&](int32_t id, float yaw) { scene.head(id, yaw); };
    events.entities.on_remove = [&](int32_t id) {
        scene.remove(id);
        std::erase(alive, id);
    };
    events.entities.on_equipment = [&](int32_t id, const mcpp::Equipment& worn) {
        scene.equipment(id, worn);
    };
    mcpp::Session session(options, events);
    events.data.on_entity_data = [&](int32_t id) {
        const auto& all = session.entities();
        if (const auto found = all.find(id); found != all.end()) {
            scene.metadata(id, found->second.metadata);
        }
    };
    session.connect();
    const auto joined = Clock::now();
    while (!placed && Clock::now() - joined < std::chrono::seconds(20)) {
        drain(session);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (!placed) {
        std::cerr << "never joined\n";
        return 1;
    }
    const std::string me = argv[3];
    // `spreadplayers` with no spread puts us on solid ground at that spot,
    // which `tp` to a guessed height would not.
    say(std::format("spreadplayers {} {} 0 1 false {}", kX, kZ, me));
    for (int i = 0; i < 200 && std::abs(me_at[0] - kX) > 32.0; ++i) {
        drain(session);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    for (int i = 0; i < 60; ++i) {
        drain(session);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (std::abs(me_at[0] - kX) > 32.0) {
        std::cerr << std::format("never reached the spot: at {:.1f} {:.1f} {:.1f}\n", me_at[0],
                                 me_at[1], me_at[2]);
        return 1;
    }
    const double at_x = me_at[0];
    const double at_y = me_at[1];
    const double at_z = me_at[2];
    std::cout << std::format("- testing at {:.1f} {:.1f} {:.1f}\n", at_x, at_y, at_z);

    mcpp::entity::View view;
    view.eye[0] = at_x;
    view.eye[1] = at_y + 1.6;
    view.eye[2] = at_z + 4.0;
    view.forward[0] = 0.0f;
    view.forward[1] = -0.1f;
    view.forward[2] = -1.0f;
    view.partial = 0.5f;
    const auto light = [](int32_t, int32_t, int32_t) -> std::array<uint8_t, 2> {
        return {15, 0};
    };

    int passed = 0;
    int failed = 0;
    for (const mcpp::entity::MobType& row : mcpp::entity::mob_types()) {
        const std::string name(row.type);
        const std::string short_name = name.substr(name.find(':') + 1);
        summoning = -1;
        for (size_t at = 0; at < types.size(); ++at) {
            if (types[at] == row.type) summoning = static_cast<int32_t>(at);
        }
        say(std::format("summon {} {} {} {} {{Tags:[\"{}\"],NoAI:1b,Silent:1b}}", name, at_x,
                        at_y, at_z, tag));
        // Let it arrive, let its metadata and its equipment follow.
        const size_t before = alive.size();
        for (int i = 0; i < 40 && alive.size() == before; ++i) {
            drain(session);
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
        for (int i = 0; i < 10; ++i) {
            drain(session);
            scene.tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
        renderer.begin();
        scene.draw(renderer, view, light);
        const auto& stats = scene.stats();
        size_t mine = 0;
        for (const int32_t id : alive) {
            const mcpp::entity::MobState* state = scene.state(id);
            if (state != nullptr && state->entity_id == id) ++mine;
        }
        const bool ok = mine >= 1 && stats.drawn >= 1 && stats.layers >= 1 && stats.parts >= 1;
        if (ok) ++passed;
        else ++failed;
        std::cout << std::format("{} {:<28} summoned {} tracked {} drawn {} layers {}\n",
                                 ok ? "ok  " : "FAIL", short_name, mine, stats.tracked,
                                 stats.drawn, stats.layers);
        if (!out_dir.empty()) {
            world.set_in_world([&](VkCommandBuffer command, const float* view_projection,
                                   uint32_t w, uint32_t h, const float* fog,
                                   const mcpp::gpu::Camera& cam) {
                const double at[3] = {cam.eye[0], cam.eye[1], cam.eye[2]};
                renderer.record(command, view_projection, at, w, h, fog, 0.0f, 0.0f);
            });
            mcpp::gpu::Camera camera{};
            for (int axis = 0; axis < 3; ++axis) {
                camera.eye[axis] = static_cast<float>(view.eye[axis]);
                camera.target[axis] = static_cast<float>(view.eye[axis]) + view.forward[axis] * 4.0f;
            }
            camera.fov_degrees = 70.0f;
            camera.near_plane = 0.05f;
            mcpp::write_png((out_dir / std::format("summoned-{}.png", short_name)).string(), kSide,
                            kSide, world.render(camera));
        }
        // This one goes before the next arrives, so one summon is one entity.
        say(std::format("kill @e[tag={}]", tag));
        for (int i = 0; i < 20 && !alive.empty(); ++i) {
            drain(session);
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
        summoning = -1;
    }
    say(std::format("kill @e[tag={}]", tag));
    for (int i = 0; i < 40; ++i) {
        drain(session);
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    std::cout << std::format("- {} drawn, {} not, {} left behind\n", passed, failed,
                             alive.size());
    return failed == 0 && alive.empty() ? 0 : 1;
}

// The NBT one of the object entities needs to be worth looking at: a dropped
// item needs an item, a falling block a block, a hologram some text.
std::string object_nbt(std::string_view type, std::string_view tag) {
    const auto with = [&](std::string_view rest) {
        return std::format("{{Tags:[\"{}\"],Silent:1b{}{}}}", tag, rest.empty() ? "" : ",", rest);
    };
    if (type == "minecraft:item") {
        return with("Item:{id:\"minecraft:diamond\",count:17},Age:0s,PickupDelay:32767s");
    }
    if (type == "minecraft:falling_block") return with("BlockState:{Name:\"minecraft:sand\"}");
    if (type == "minecraft:tnt") return with("fuse:60s,block_state:{Name:\"minecraft:tnt\"}");
    if (type == "minecraft:experience_orb") return with("Value:42s,Age:0s");
    if (type == "minecraft:text_display") {
        return with("text:\"Plasma lobby\",billboard:\"center\"");
    }
    if (type == "minecraft:item_display") {
        return with("item:{id:\"minecraft:diamond_sword\",count:1},item_display:\"ground\"");
    }
    if (type == "minecraft:block_display") return with("block_state:{Name:\"minecraft:stone\"}");
    if (type.ends_with("_minecart") || type == "minecraft:minecart") return with("");
    if (type == "minecraft:snowball" || type == "minecraft:egg" ||
        type == "minecraft:ender_pearl") {
        return with("");
    }
    return with("");
}

// Every object entity the client draws, one at a time, on the server: summoned
// at this agent's own spot, drawn, looked at from three sides and killed again.
int entityobjects(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "usage: entityobjects <host[:port]> <name> [console.in] [out-dir]\n";
        return 2;
    }
    const std::filesystem::path console =
        argc > 4 ? argv[4] : std::filesystem::path(std::getenv("HOME")) / "plasma-testserver" /
                                 "console.in";
    const std::filesystem::path out_dir = argc > 5 ? argv[5] : "";
    // This agent's own spot, far from spawn and from every other agent's.
    constexpr double kX = -8329.0;
    constexpr double kZ = 8329.0;
    const std::string tag = "plasmaS6";

    const auto say = [&console](const std::string& line) {
        std::ofstream(console, std::ios::app) << line << '\n';
    };
    struct Sweep {
        std::function<void()> run;
        ~Sweep() { run(); }
    } sweep{[&] { say(std::format("kill @e[tag={}]", tag)); }};

    mcpp::entity::Assets assets;
    mcpp::world::Atlas atlas;
    if (!assets.open(mcpp::entity::Assets::default_path(kProtocol)) ||
        !atlas.open(mcpp::world::Atlas::default_path(kProtocol))) {
        std::cerr << "baked data missing\n";
        return 1;
    }
    mcpp::ui::ItemTable items;
    items.load(mcpp::ui::ItemTable::default_path(kProtocol));
    mcpp::world::BlockTable blocks;
    const bool have_blocks = blocks.open(mcpp::world::BlockTable::default_path(kProtocol));
    if (!have_blocks) std::cout << "- no block table; a falling block draws nothing\n";

    mcpp::gpu::Device device;
    mcpp::gpu::Device::Options gpu_options;
    device.create(gpu_options);
    constexpr uint32_t kSide = 512;
    mcpp::gpu::WorldRenderer world(device, kSide, kSide, atlas);
    mcpp::gpu::EntityRenderer renderer(device, world.colour_format());
    renderer.load(assets);
    mcpp::gpu::HandRenderer hand(device, world.colour_format(), world.depth_format(),
                                 mcpp::gpu::HandRenderer::default_path(kProtocol));
    mcpp::world::Atmosphere atmosphere;

    auto types = mcpp::entity::load_entity_types(mcpp::entity::default_registries_path(kProtocol));
    mcpp::entity::Scene scene(types, nullptr);
    scene.bind(renderer);
    scene.hold_items_with(&hand, &atmosphere);
    scene.draw_objects_with(&items, [&blocks, have_blocks](int32_t state) -> std::string_view {
        return have_blocks ? blocks.block_name(state) : std::string_view{};
    });

    // The test server is offline; joining as the name this test was given
    // keeps it off whatever account happens to be signed in on this machine.
    const auto address = parse_address(argv[2]);
    mcpp::SessionOptions options;
    options.host = address.host;
    options.port = address.port;
    options.username = argv[3];
    bool placed = false;
    double me_at[3] = {0.0, 0.0, 0.0};
    std::vector<int32_t> alive;
    int32_t summoning = -1;
    mcpp::SessionEvents events;
    events.on_status = [](std::string_view line) { std::cout << "- " << line << '\n'; };
    events.player.on_position = [&](double x, double y, double z, float, float) {
        me_at[0] = x;
        me_at[1] = y;
        me_at[2] = z;
        placed = true;
    };
    events.entities.on_add = [&](const mcpp::Entity& entity) {
        scene.add(entity);
        if (entity.type == summoning) alive.push_back(entity.id);
    };
    events.entities.on_moved = [&](const mcpp::EntityMove& move) { scene.moved(move); };
    events.entities.on_remove = [&](int32_t id) {
        scene.remove(id);
        std::erase(alive, id);
    };
    // `Session` takes its events by value, so every handler has to be in
    // place before it is built; one assigned afterwards is assigned to a copy
    // nothing reads. The session itself is only known after that, hence the
    // pointer the handler reads through.
    mcpp::Session* connected = nullptr;
    events.data.on_entity_data = [&](int32_t id) {
        if (connected == nullptr) return;
        const auto& all = connected->entities();
        if (const auto found = all.find(id); found != all.end()) {
            scene.metadata(id, found->second.metadata);
        }
    };
    mcpp::Session session(options, events);
    connected = &session;
    session.connect();
    const auto joined = Clock::now();
    while (!placed && Clock::now() - joined < std::chrono::seconds(20)) {
        drain(session);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (!placed) {
        std::cerr << "never joined\n";
        return 1;
    }
    const std::string me = argv[3];
    say(std::format("spreadplayers {} {} 0 1 false {}", kX, kZ, me));
    for (int i = 0; i < 200 && std::abs(me_at[0] - kX) > 32.0; ++i) {
        drain(session);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    for (int i = 0; i < 60; ++i) {
        drain(session);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (std::abs(me_at[0] - kX) > 32.0) {
        std::cerr << std::format("never reached the spot: at {:.1f} {:.1f} {:.1f}\n", me_at[0],
                                 me_at[1], me_at[2]);
        return 1;
    }
    const double at_x = me_at[0];
    const double at_y = me_at[1];
    const double at_z = me_at[2];
    std::cout << std::format("- testing at {:.1f} {:.1f} {:.1f}\n", at_x, at_y, at_z);

    const auto light = [](int32_t, int32_t, int32_t) -> std::array<uint8_t, 2> {
        return {15, 0};
    };
    // The front, its own right and straight down: the three sides that show a
    // placement's mistakes.
    // Looking straight down has no up of its own, so the top view is steep
    // rather than vertical.
    const std::array<std::array<float, 6>, 3> sides = {{
        {0.0f, 1.0f, 3.0f, 0.0f, -0.2f, -1.0f},
        {3.0f, 1.0f, 0.0f, -1.0f, -0.2f, 0.0f},
        {0.0f, 3.4f, 1.3f, 0.0f, -1.0f, -0.45f},
    }};

    int passed = 0;
    int failed = 0;
    int gone = 0;
    for (const mcpp::entity::ObjectType& row : mcpp::entity::object_types()) {
        if (row.type.empty()) continue;
        const std::string name(row.type);
        const std::string short_name = name.substr(name.find(':') + 1);
        summoning = -1;
        for (size_t at = 0; at < types.size(); ++at) {
            if (types[at] == row.type) summoning = static_cast<int32_t>(at);
        }
        if (summoning < 0) {
            std::cout << std::format("skip {:<28} the server has no such type\n", short_name);
            continue;
        }
        // The last one's removal has to have landed before the baseline is
        // taken, or its leaving cancels this one's arriving.
        for (int i = 0; i < 12; ++i) {
            drain(session);
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
        // What one frame costs with nothing of ours in it: the spot has its
        // own mobs and items, and only the rise over them is this entity.
        mcpp::entity::View baseline;
        baseline.eye[0] = at_x;
        baseline.eye[1] = at_y + 1.0;
        baseline.eye[2] = at_z + 3.0;
        baseline.forward[2] = -1.0f;
        baseline.partial = 0.5f;
        renderer.begin();
        hand.begin();
        scene.draw(renderer, baseline, light);
        const size_t drawn_before = scene.stats().drawn;
        say(std::format("summon {} {} {} {} {}", name, at_x, at_y + 1.0, at_z,
                        object_nbt(row.type, tag)));
        const size_t before = alive.size();
        for (int i = 0; i < 40 && alive.size() == before; ++i) {
            drain(session);
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
        for (int i = 0; i < 10; ++i) {
            drain(session);
            scene.tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
        size_t drawn = 0;
        size_t items_queued = 0;
        for (size_t side = 0; side < sides.size(); ++side) {
            mcpp::entity::View view;
            view.eye[0] = at_x + sides[side][0];
            view.eye[1] = at_y + sides[side][1];
            view.eye[2] = at_z + sides[side][2];
            for (int axis = 0; axis < 3; ++axis) {
                view.forward[axis] = sides[side][3 + static_cast<size_t>(axis)];
            }
            view.partial = 0.5f;
            renderer.begin();
            hand.begin();
            scene.draw(renderer, view, light);
            drawn = std::max(drawn, scene.stats().drawn);
            items_queued = std::max(items_queued, hand.world_vertices());
            if (out_dir.empty()) continue;
            world.set_in_world([&](VkCommandBuffer command, const float* view_projection,
                                   uint32_t w, uint32_t h, const float* fog,
                                   const mcpp::gpu::Camera& cam) {
                const double at[3] = {cam.eye[0], cam.eye[1], cam.eye[2]};
                renderer.record(command, view_projection, at, w, h, fog, 0.0f, 0.0f);
                hand.record_world(command, view_projection, at, w, h);
            });
            mcpp::gpu::Camera camera{};
            for (int axis = 0; axis < 3; ++axis) {
                camera.eye[axis] = static_cast<float>(view.eye[axis]);
                camera.target[axis] =
                    static_cast<float>(view.eye[axis]) + view.forward[axis] * 4.0f;
            }
            camera.fov_degrees = 70.0f;
            camera.near_plane = 0.05f;
            static constexpr std::array<std::string_view, 3> kSideNames = {"front", "side",
                                                                          "above"};
            mcpp::write_png(
                (out_dir / std::format("object-{}-{}.png", short_name, kSideNames[side])).string(),
                kSide, kSide, world.render(camera));
        }
        // What the server told us about it, so a type that draws nothing can
        // be told from one the server took away again.
        size_t fields = 0;
        bool present = false;
        for (const int32_t id : alive) {
            const auto& all = session.entities();
            if (const auto found = all.find(id); found != all.end()) {
                present = true;
                fields = std::max(fields, found->second.metadata.size());
            }
        }
        const bool ok = drawn > drawn_before || items_queued > 0;
        if (ok) ++passed;
        else if (!present) ++gone;
        else ++failed;
        std::cout << std::format("{} {:<28} drawn {} of {} item vertices {} data {}\n",
                                 ok ? "ok  " : (present ? "FAIL" : "gone"), short_name,
                                 drawn - drawn_before, drawn, items_queued, fields);
        say(std::format("kill @e[tag={}]", tag));
        for (int i = 0; i < 20 && !alive.empty(); ++i) {
            drain(session);
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
        summoning = -1;
    }
    say(std::format("kill @e[tag={}]", tag));
    for (int i = 0; i < 40; ++i) {
        drain(session);
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    std::cout << std::format("- {} drawn, {} not, {} gone before the frame, {} left behind\n",
                             passed, failed, gone, alive.size());
    return failed == 0 && alive.empty() ? 0 : 1;
}

// A crowd walking in a grid, for what entities cost: the tick,
// the posing and the gpu, each against the same frame with no one in it.
int entitybench(int argc, char** argv) {
    const int count = argc > 2 ? std::stoi(argv[2]) : 200;
    mcpp::entity::Assets assets;
    mcpp::world::Atlas atlas;
    if (!assets.open(mcpp::entity::Assets::default_path(kProtocol)) ||
        !atlas.open(mcpp::world::Atlas::default_path(kProtocol))) {
        std::cerr << "baked data missing\n";
        return 1;
    }
    mcpp::gpu::Device device;
    mcpp::gpu::Device::Options gpu_options;
    device.create(gpu_options);
    mcpp::gpu::WorldRenderer world(device, 1280, 720, atlas);
    mcpp::gpu::EntityRenderer renderer(device, world.colour_format());
    renderer.load(assets);
    auto types = mcpp::entity::load_entity_types(mcpp::entity::default_registries_path(kProtocol));
    int32_t player = -1;
    for (size_t i = 0; i < types.size(); ++i) {
        if (types[i] == "minecraft:player") player = static_cast<int32_t>(i);
    }
    mcpp::entity::Scene scene(types, nullptr);
    scene.bind(renderer);
    // Every type the table knows, in turn, with a player every tenth, so the
    // cost is a crowd's rather than one model's drawn two hundred times.
    const auto rows = mcpp::entity::mob_types();
    std::vector<int32_t> mixed;
    for (const mcpp::entity::MobType& row : rows) {
        for (size_t at = 0; at < types.size(); ++at) {
            if (types[at] == row.type) mixed.push_back(static_cast<int32_t>(at));
        }
    }
    if (mixed.empty() && player >= 0) mixed.push_back(player);
    const int side = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(count))));
    for (int i = 0; i < count; ++i) {
        mcpp::Entity e;
        e.id = i + 1;
        e.type = i % 10 == 0 || mixed.empty()
                     ? player
                     : mixed[static_cast<size_t>(i) % mixed.size()];
        e.uuid[0] = 0x1234567800000000ull + static_cast<uint64_t>(i);
        e.uuid[1] = 0x9abcdef000000000ull + static_cast<uint64_t>(i) * 7919;
        e.x = (i % side) * 1.5;
        e.z = 6.0 + (i / side) * 1.5;
        if (e.type == player) {
            mcpp::PlayerInfo info;
            info.uuid = mcpp::uuid_string(e.uuid);
            info.name = std::format("Plasma{}", i);
            scene.player_info(info);
        }
        scene.add(e);
    }
    mcpp::gpu::Camera camera{};
    camera.eye[0] = static_cast<float>(side) * 0.75f;
    camera.eye[1] = 6.0f;
    camera.eye[2] = 0.0f;
    camera.target[0] = camera.eye[0];
    camera.target[1] = 0.0f;
    camera.target[2] = 6.0f + static_cast<float>(side) * 0.75f;
    mcpp::entity::View view;
    for (int axis = 0; axis < 3; ++axis) {
        view.eye[axis] = camera.eye[axis];
        view.forward[axis] = camera.target[axis] - camera.eye[axis];
    }
    const auto light = [](int32_t, int32_t, int32_t) -> std::array<uint8_t, 2> { return {15, 0}; };
    bool draw_entities = true;
    world.set_in_world([&](VkCommandBuffer command, const float* view_projection, uint32_t w,
                           uint32_t h, const float* fog, const mcpp::gpu::Camera& cam) {
        if (!draw_entities) return;
        const double at[3] = {cam.eye[0], cam.eye[1], cam.eye[2]};
        renderer.record(command, view_projection, at, w, h, fog, 0.0f, 0.0f);
    });
    double tick_ms = 0.0;
    double pose_ms = 0.0;
    double gpu_with = 0.0;
    double gpu_without = 0.0;
    constexpr int kFrames = 120;
    for (int frame = 0; frame < kFrames; ++frame) {
        const auto t0 = Clock::now();
        for (int i = 0; i < count; ++i) {
            mcpp::EntityMove m;
            m.id = i + 1;
            m.x = (i % side) * 1.5 + 0.2 * frame;
            m.z = 6.0 + (i / side) * 1.5;
            m.yaw = -90.0f;
            m.has_position = m.has_rotation = true;
            scene.moved(m);
        }
        scene.tick();
        const auto t1 = Clock::now();
        view.partial = 0.5f;
        renderer.begin();
        scene.draw(renderer, view, light);
        const auto t2 = Clock::now();
        draw_entities = true;
        world.render(camera);
        const double with = world.last_gpu_ms();
        draw_entities = false;
        world.render(camera);
        const double without = world.last_gpu_ms();
        if (frame >= 20) {
            tick_ms += std::chrono::duration<double, std::milli>(t1 - t0).count();
            pose_ms += std::chrono::duration<double, std::milli>(t2 - t1).count();
            gpu_with += with;
            gpu_without += without;
        }
    }
    const double n = kFrames - 20;
    std::cout << std::format(
        "- {} entities of {} types, {} drawn, {} part matrices a frame\n"
        "- cpu: tick {:.3f} ms (packets and one tick), pose and queue {:.3f} ms a frame\n"
        "- gpu: {:.3f} ms with them, {:.3f} ms without, {:.3f} ms for the crowd\n",
        count, mixed.size() + 1, scene.stats().drawn, scene.stats().parts, tick_ms / n,
        pose_ms / n, gpu_with / n, gpu_without / n, (gpu_with - gpu_without) / n);
    if (argc > 3) {
        draw_entities = true;
        renderer.begin();
        scene.draw(renderer, view, light);
        mcpp::write_png(argv[3], 1280, 720, world.render(camera));
    }
    return 0;
}

// What a lobby costs: a crowd of dropped items and a wall of holograms, which
// is what a hub is actually full of. No server; the metadata is written by
// hand the way the server writes it.
int entitylobby(int argc, char** argv) {
    const int items_count = argc > 2 ? std::stoi(argv[2]) : 200;
    const int holograms = argc > 3 ? std::stoi(argv[3]) : 50;
    mcpp::entity::Assets assets;
    mcpp::world::Atlas atlas;
    if (!assets.open(mcpp::entity::Assets::default_path(kProtocol)) ||
        !atlas.open(mcpp::world::Atlas::default_path(kProtocol))) {
        std::cerr << "baked data missing\n";
        return 1;
    }
    mcpp::ui::ItemTable items;
    items.load(mcpp::ui::ItemTable::default_path(kProtocol));
    mcpp::gpu::Device device;
    mcpp::gpu::Device::Options gpu_options;
    device.create(gpu_options);
    mcpp::gpu::WorldRenderer world(device, 1280, 720, atlas);
    mcpp::gpu::EntityRenderer renderer(device, world.colour_format());
    renderer.load(assets);
    mcpp::gpu::HandRenderer hand(device, world.colour_format(), world.depth_format(),
                                 mcpp::gpu::HandRenderer::default_path(kProtocol));
    mcpp::world::Atmosphere atmosphere;

    auto types = mcpp::entity::load_entity_types(mcpp::entity::default_registries_path(kProtocol));
    int32_t item_type = -1;
    int32_t text_type = -1;
    for (size_t i = 0; i < types.size(); ++i) {
        if (types[i] == "minecraft:item") item_type = static_cast<int32_t>(i);
        if (types[i] == "minecraft:text_display") text_type = static_cast<int32_t>(i);
    }
    if (item_type < 0 || text_type < 0) {
        std::cerr << "the registries have no item or text_display type\n";
        return 1;
    }
    mcpp::entity::Scene scene(types, nullptr);
    scene.bind(renderer);
    scene.hold_items_with(&hand, &atmosphere);
    scene.draw_objects_with(&items, nullptr);

    const int32_t diamond = items.find("diamond");
    const int side = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(items_count))));
    int32_t next_id = 1;
    for (int i = 0; i < items_count; ++i) {
        mcpp::Entity e;
        e.id = next_id++;
        e.type = item_type;
        e.x = (i % side) * 1.2;
        e.y = 0.0;
        e.z = 6.0 + (i / side) * 1.2;
        scene.add(e);
        mcpp::proto::Metadata data;
        mcpp::ItemStack stack;
        stack.id = diamond >= 0 ? diamond : 1;
        stack.count = 1 + i % 64;
        data.set(8, mcpp::proto::MetaType::ItemStack,
                 mcpp::proto::Value(std::make_shared<const mcpp::ItemStack>(stack)));
        scene.metadata(e.id, data);
    }
    for (int i = 0; i < holograms; ++i) {
        mcpp::Entity e;
        e.id = next_id++;
        e.type = text_type;
        e.x = (i % 10) * 2.0;
        e.y = 2.5;
        e.z = 6.0 + (i / 10) * 2.0;
        scene.add(e);
        mcpp::proto::Metadata data;
        const mcpp::nbt::Value text(std::format("Plasma lobby {}", i));
        data.set(23, mcpp::proto::MetaType::Component, mcpp::proto::Value(text));
        data.set(15, mcpp::proto::MetaType::Byte, mcpp::proto::Value(int64_t{3}));
        scene.metadata(e.id, data);
    }

    mcpp::gpu::Camera camera{};
    camera.eye[0] = static_cast<float>(side) * 0.6f;
    camera.eye[1] = 4.0f;
    camera.eye[2] = 0.0f;
    camera.target[0] = camera.eye[0];
    camera.target[1] = 0.5f;
    camera.target[2] = 6.0f + static_cast<float>(side) * 0.6f;
    mcpp::entity::View view;
    for (int axis = 0; axis < 3; ++axis) {
        view.eye[axis] = camera.eye[axis];
        view.forward[axis] = camera.target[axis] - camera.eye[axis];
    }
    const auto light = [](int32_t, int32_t, int32_t) -> std::array<uint8_t, 2> { return {15, 0}; };
    bool draw_entities = true;
    world.set_in_world([&](VkCommandBuffer command, const float* view_projection, uint32_t w,
                           uint32_t h, const float* fog, const mcpp::gpu::Camera& cam) {
        if (!draw_entities) return;
        const double at[3] = {cam.eye[0], cam.eye[1], cam.eye[2]};
        renderer.record(command, view_projection, at, w, h, fog, 0.0f, 0.0f);
        hand.record_world(command, view_projection, at, w, h);
    });
    double pose_ms = 0.0;
    double gpu_with = 0.0;
    double gpu_without = 0.0;
    size_t item_vertices = 0;
    constexpr int kFrames = 120;
    for (int frame = 0; frame < kFrames; ++frame) {
        scene.tick();
        view.partial = 0.5f;
        const auto t0 = Clock::now();
        renderer.begin();
        hand.begin();
        scene.draw(renderer, view, light);
        const auto t1 = Clock::now();
        item_vertices = hand.world_vertices();
        draw_entities = true;
        world.render(camera);
        const double with = world.last_gpu_ms();
        draw_entities = false;
        world.render(camera);
        const double without = world.last_gpu_ms();
        if (frame >= 20) {
            pose_ms += std::chrono::duration<double, std::milli>(t1 - t0).count();
            gpu_with += with;
            gpu_without += without;
        }
    }
    const double n = kFrames - 20;
    std::cout << std::format(
        "- {} dropped items and {} holograms, {} drawn, {} item vertices a frame\n"
        "- cpu: build and queue {:.3f} ms a frame\n"
        "- gpu: {:.3f} ms with them, {:.3f} ms without, {:.3f} ms for the lobby\n",
        items_count, holograms, scene.stats().drawn, item_vertices, pose_ms / n, gpu_with / n,
        gpu_without / n, (gpu_with - gpu_without) / n);
    if (argc > 4) {
        draw_entities = true;
        renderer.begin();
        hand.begin();
        scene.draw(renderer, view, light);
        mcpp::write_png(argv[4], 1280, 720, world.render(camera));
    }
    return 0;
}

} // namespace

int entity_command(int argc, char** argv) {
    if (argc < 2) return -1;
    const std::string command = argv[1];
    if (command == "entitylobby") return entitylobby(argc, argv);
    if (command == "entityshot") return entityshot(argc, argv);
    if (command == "entitybot") return entitybot(argc, argv);
    if (command == "entitymobs") return entitymobs(argc, argv);
    if (command == "entityobjects") return entityobjects(argc, argv);
    if (command == "entityrecord") return entityrecord(argc, argv);
    if (command == "entityreplay") return entityreplay(argc, argv);
    if (command == "entitybench") return entitybench(argc, argv);
    return -1;
}

} // namespace client
