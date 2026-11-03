#include "client/game.hpp"

#include "mcpp/protocol_versions.hpp"

#include <cmath>
#include <format>
#include <iostream>

namespace client {

void entities_bind(Game& game, mcpp::SessionEvents& events) {
    auto& area = game.entities;
    if (!area.assets.open(mcpp::entity::Assets::default_path(mcpp::protocol::kTargetProtocol))) {
        std::cout << "- no entity data; bake it with tools/bake_entities.py. Nothing alive "
                     "will be drawn\n";
    }
    area.skins.emplace();
    area.scene.emplace(area.assets,
                       mcpp::entity::load_entity_types(mcpp::entity::default_registries_path(
                           mcpp::protocol::kTargetProtocol)),
                       &*area.skins);
    auto& scene = *area.scene;
    events.entities.on_add = [&scene](const mcpp::Entity& entity) { scene.add(entity); };
    events.entities.on_move = [&scene](const mcpp::Entity& entity) { scene.move(entity); };
    events.entities.on_remove = [&scene](int32_t id) { scene.remove(id); };
    events.entities.on_animate = [&scene](int32_t id, uint8_t action) { scene.animate(id, action); };
    events.entities.on_hurt = [&scene](int32_t id, float) { scene.hurt(id); };
    events.entities.on_event = [&scene](int32_t id, uint8_t status) { scene.event(id, status); };
}

void entities_start(Game& game) {
    auto& area = game.entities;
    auto& renderer = area.renderer.emplace(game.device, game.renderer->colour_format());
    renderer.set_font(area.assets.font());
    game.renderer->set_in_world([&renderer](VkCommandBuffer command, const float* view_projection,
                                            uint32_t width, uint32_t height, const float* fog,
                                            const mcpp::gpu::Camera& camera) {
        renderer.record(command, view_projection, width, height, fog, camera.fog_start,
                        camera.fog_end);
    });
}

void entities_input(Game&) {}

void entities_tick(Game& game) {
    auto& area = game.entities;
    const int ticks = area.clock.advance(game.dt);
    for (int i = 0; i < ticks; ++i) area.scene->tick();
}

void entities_draw(Game& game) {
    auto& area = game.entities;
    auto& renderer = *area.renderer;
    renderer.begin();
    mcpp::entity::View view;
    for (int axis = 0; axis < 3; ++axis) {
        view.eye[axis] = game.camera.eye[axis];
        view.forward[axis] = game.camera.target[axis] - game.camera.eye[axis];
    }
    view.partial = area.clock.partial();
    view.self = game.session->entity_id();
    const auto light = [&game](int32_t x, int32_t y, int32_t z) -> std::array<uint8_t, 2> {
        auto& world = game.world;
        std::lock_guard lock(world.world_guard);
        const auto found = world.world.find({x >> 4, z >> 4});
        if (found == world.world.end()) return {15, 0};
        const int32_t index = found->second.index_of(y);
        return {found->second.sky_light_at(x & 15, index, z & 15),
                found->second.block_light_at(x & 15, index, z & 15)};
    };
    area.scene->draw(renderer, *game.session, view, light);
}

void entities_stop(Game& game) {
    game.renderer->set_in_world({});
    game.entities.renderer.reset();
}

} // namespace client
