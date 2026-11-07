#include "client/hand_view.hpp"

#include "client/game.hpp"

#include "mcpp/entity/assets.hpp"
#include "mcpp/gpu/hand.hpp"
#include "mcpp/protocol_versions.hpp"
#include "mcpp/session.hpp"
#include "mcpp/world/atmosphere.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>

namespace client {
namespace {

constexpr int32_t kProtocol = mcpp::protocol::kTargetProtocol;

} // namespace

struct HandArea {
    std::optional<mcpp::gpu::HandRenderer> renderer;
    mcpp::entity::Assets assets;
    mcpp::client::HandBob bob;
    mcpp::client::SwingClock swing;
    int32_t bow = -1;
    int32_t crossbow = -1;
    int32_t sky_light = 15;
    int32_t block_light = 0;
    bool slim = false;
};

void hand_start(Game& game) {
    if (!game.renderer) return;
    auto area = std::make_shared<HandArea>();
    if (!area->assets.open(mcpp::entity::Assets::default_path(kProtocol))) return;
    const mcpp::entity::Model* wide = area->assets.models().find("player");
    const mcpp::entity::Model* slim = area->assets.models().find("player_slim");
    if (wide == nullptr || slim == nullptr) return;
    area->renderer.emplace(game.device, game.renderer->colour_format(),
                           game.renderer->depth_format(),
                           mcpp::gpu::HandRenderer::default_path(kProtocol));
    area->renderer->load_arm(*wide, *slim);
    if (const mcpp::entity::Texture* skin = area->assets.texture("player/wide/steve");
        skin != nullptr) {
        area->renderer->set_skin(skin->rgba);
    }
    area->bow = game.screens.items.find("bow");
    area->crossbow = game.screens.items.find("crossbow");
    game.hand = area;
    game.renderer->set_hand([&game](VkCommandBuffer command, const float*, uint32_t width,
                                    uint32_t height, const float*,
                                    const mcpp::gpu::Camera& view) {
        if (!game.hand || !game.hand->renderer) return;
        const auto& atmosphere = game.renderer->atmosphere();
        float light[3] = {1.0f, 1.0f, 1.0f};
        mcpp::world::lightmap_texel(atmosphere, game.hand->block_light, game.hand->sky_light,
                                    light);
        game.hand->renderer->record(command, width, height, light, view);
    });
}

void hand_tick(Game& game) {
    if (!game.hand || !game.session) return;
    const auto& inventory = game.session->inventory();
    mcpp::client::HandPlayer player;
    player.main_item = inventory.hotbar(static_cast<size_t>(std::clamp(inventory.held, 0, 8))).id;
    player.off_item = inventory.slots[mcpp::Inventory::kOffhand].id;
    player.hands_busy = game.player.using_item;
    game.hand->bob.tick(player);
    game.hand->swing.tick();
    if (game.player.attacking) game.hand->swing.swing();
}

void hand_draw(Game& game) {
    if (!game.hand || !game.hand->renderer || !game.session) return;
    auto& area = *game.hand;
    const auto& inventory = game.session->inventory();
    const auto& body = game.player.body;
    const auto pt = static_cast<float>(game.player.partial_tick);

    mcpp::client::HandPlayer player;
    player.partial_tick = pt;
    player.main_item = inventory.hotbar(static_cast<size_t>(std::clamp(inventory.held, 0, 8))).id;
    player.off_item = inventory.slots[mcpp::Inventory::kOffhand].id;
    player.attack_anim = area.swing.amount(pt);
    player.swinging_hand = mcpp::client::HandSide::Main;
    player.view_bobbing = game.player.bob_view;
    // `GameRenderer.bobView` reads the walk distance backwards, as the camera
    // already does in player_draw.
    player.walk_dist_o = -body.walk_dist_o;
    player.walk_dist = -body.walk_dist;
    player.bob_o = body.bob_o;
    player.bob = body.bob;
    // The head's own drift is not tracked yet, so the hand hangs square on
    // the view rather than lagging a tenth behind it.
    player.view_x_rot = 0.0f;
    player.view_y_rot = 0.0f;
    player.main_is_bow = player.main_item == area.bow;
    player.off_is_bow = player.off_item == area.bow;
    player.main_is_crossbow = player.main_item == area.crossbow;
    player.off_is_crossbow = player.off_item == area.crossbow;

    // The light where the player stands, which is what the hand is lit by.
    {
        auto& world = game.world;
        std::lock_guard lock(world.world_guard);
        const auto at = [&](int axis) {
            return static_cast<int32_t>(std::floor(game.player.eye[axis]));
        };
        const auto found = world.world.find({at(0) >> 4, at(2) >> 4});
        if (found != world.world.end()) {
            const int32_t index = found->second.index_of(at(1));
            area.sky_light = found->second.sky_light_at(at(0) & 15, index, at(2) & 15);
            area.block_light = found->second.block_light_at(at(0) & 15, index, at(2) & 15);
        }
    }

    queue_hand(*area.renderer, player, area.bob, area.slim);
}

void hand_stop(Game& game) {
    if (game.renderer) game.renderer->set_hand({});
    game.hand.reset();
}

} // namespace client
