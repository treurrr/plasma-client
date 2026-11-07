#include "client/handshot.hpp"

#include "client/hand.hpp"
#include "client/hand_view.hpp"

#include "mcpp/entity/assets.hpp"
#include "mcpp/gpu/device.hpp"
#include "mcpp/gpu/hand.hpp"
#include "mcpp/gpu/ui.hpp"
#include "mcpp/gpu/world_renderer.hpp"
#include "mcpp/image_png.hpp"
#include "mcpp/protocol_versions.hpp"
#include "mcpp/world/atlas.hpp"

#include <algorithm>
#include <chrono>
#include <format>
#include <iostream>
#include <string>
#include <vector>

namespace client {
namespace {

constexpr int32_t kProtocol = mcpp::protocol::kTargetProtocol;
constexpr uint32_t kWidth = 854;
constexpr uint32_t kHeight = 480;

struct Shot {
    std::string name;
    std::string item;
    float swing = 0.0f;
    float equipped = 0.0f;
    bool using_item = false;
    mcpp::client::UseAnimation animation = mcpp::client::UseAnimation::None;
    int32_t use_remaining = 0;
    int32_t use_duration = 0;
    bool slim = false;
};

} // namespace

int handshot(int argc, char** argv) {
    std::string prefix = "hand";
    for (int i = 2; i < argc; ++i) {
        const std::string argument = argv[i];
        if (!argument.starts_with("--")) prefix = argument;
    }

    mcpp::entity::Assets assets;
    if (!assets.open(mcpp::entity::Assets::default_path(kProtocol))) {
        std::cerr << "no entity data; bake it with tools/bake_entities.py\n";
        return 1;
    }
    const mcpp::entity::Model* wide = assets.models().find("player");
    const mcpp::entity::Model* slim_model = assets.models().find("player_slim");
    if (wide == nullptr || slim_model == nullptr) {
        std::cerr << "no player model in the entity data\n";
        return 1;
    }

    mcpp::gpu::Device device;
    mcpp::gpu::Device::Options options;
    options.validation = true;
    device.create(options);
    mcpp::world::Atlas atlas;
    if (!atlas.open(mcpp::world::Atlas::default_path(kProtocol))) {
        std::cerr << "no atlas; bake one with tools/bake_atlas.py\n";
        return 1;
    }
    mcpp::gpu::WorldRenderer world(device, kWidth, kHeight, atlas);
    mcpp::gpu::HandRenderer hand(device, world.colour_format(), world.depth_format(),
                                 mcpp::gpu::HandRenderer::default_path(kProtocol));
    hand.load_arm(*wide, *slim_model);
    if (!hand.has_items()) {
        std::cerr << "no held item geometry; bake it with tools/bake_held.py\n";
        return 1;
    }

    // Item ids come from the names the icon table carries, so a shot asks for
    // "diamond_sword" rather than a number that moves between versions.
    mcpp::gpu::UiRenderer items(device, world.colour_format(),
                                mcpp::gpu::UiRenderer::default_items_path(kProtocol));
    const auto id_of = [&](std::string_view name) -> int32_t {
        for (int32_t id = 0; id < 4096; ++id) {
            if (items.item_name(id) == name) return id;
        }
        return -1;
    };

    const std::vector<Shot> shots = {
        {"empty", "", 0.0f, 0.0f},
        {"sword", "diamond_sword", 0.0f, 0.0f},
        {"sword-swing", "diamond_sword", 0.45f, 0.0f},
        {"block", "crafting_table", 0.0f, 0.0f},
        {"bow-draw", "bow", 0.0f, 0.0f, true, mcpp::client::UseAnimation::Bow, 52, 72},
        {"equipping", "diamond_pickaxe", 0.0f, 0.7f},
        {"empty-slim", "", 0.0f, 0.0f, false, mcpp::client::UseAnimation::None, 0, 0, true},
    };

    for (const Shot& shot : shots) {
        const int32_t item = shot.item.empty() ? 0 : id_of(shot.item);
        if (!shot.item.empty() && item <= 0) {
            std::cerr << std::format("no item {}\n", shot.item);
            return 1;
        }
        const std::string skin_name = shot.slim ? "player/slim/alex" : "player/wide/steve";
        if (const mcpp::entity::Texture* skin = assets.texture(skin_name); skin != nullptr) {
            hand.set_skin(skin->rgba);
        }

        mcpp::client::HandPlayer player;
        player.partial_tick = 1.0f;
        player.main_item = item;
        player.attack_anim = shot.swing;
        player.using_item = shot.using_item;
        player.use_animation = shot.animation;
        player.use_remaining = shot.use_remaining;
        player.use_duration = shot.use_duration;
        player.main_is_bow = shot.item == "bow";
        mcpp::client::HandBob bob;
        // The hand has been holding this long enough to be all the way up,
        // unless the shot asks for it halfway through a swap.
        for (int tick = 0; tick < 10; ++tick) bob.tick(player);
        if (shot.equipped > 0.0f) {
            mcpp::client::HandPlayer swapping = player;
            swapping.main_item = item + 1;
            bob.tick(swapping);
            bob.tick(swapping);
        }

        queue_hand(hand, player, bob, shot.slim);
        const float light[3] = {1.0f, 1.0f, 1.0f};
        world.set_hand([&](VkCommandBuffer command, const float*, uint32_t width,
                           uint32_t height, const float*, const mcpp::gpu::Camera&) {
            hand.record(command, width, height, light);
        });

        mcpp::gpu::Camera camera{};
        camera.eye[0] = 0.0f;
        camera.eye[1] = 80.0f;
        camera.eye[2] = 0.0f;
        camera.target[0] = 0.0f;
        camera.target[1] = 80.0f;
        camera.target[2] = -1.0f;
        camera.fov_degrees = 70.0f;
        const auto start = std::chrono::steady_clock::now();
        const auto pixels = world.render(camera);
        const auto taken = std::chrono::steady_clock::now() - start;
        const std::string out = std::format("{}-{}.png", prefix, shot.name);
        mcpp::write_png(out, kWidth, kHeight, pixels);
        std::cout << std::format("- {}  {} vertices, {:.2f} ms gpu, {:.2f} ms wall\n", out,
                                 hand.vertices(), world.last_gpu_ms(),
                                 std::chrono::duration<double, std::milli>(taken).count());
    }
    return 0;
}

} // namespace client
