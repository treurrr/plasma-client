#include "client/game.hpp"

#include "mcpp/protocol_versions.hpp"

#include <cmath>
#include <format>
#include <iostream>

namespace client {

void entities_bind(Game& game, mcpp::SessionEvents& events) {
    auto& area = game.entities;
    if (!area.assets.open(mcpp::entity::Assets::default_path(mcpp::protocol::kTargetProtocol))) {
        std::cout << "- no entity data; bake it with tools/bake_all.py. Nothing alive "
                     "will be drawn\n";
    }
    area.skins.emplace();
    area.scene.emplace(mcpp::entity::load_entity_types(
                           mcpp::entity::default_registries_path(mcpp::protocol::kTargetProtocol)),
                       &*area.skins);
    auto& scene = *area.scene;
    events.entities.on_add = [&scene](const mcpp::Entity& entity) { scene.add(entity); };
    events.entities.on_moved = [&scene](const mcpp::EntityMove& move) { scene.moved(move); };
    events.entities.on_head = [&scene](int32_t id, float yaw) { scene.head(id, yaw); };
    events.entities.on_remove = [&scene](int32_t id) { scene.remove(id); };
    events.data.on_entity_data = [&game, &scene](int32_t id) {
        const auto& entities = game.session->entities();
        if (const auto found = entities.find(id); found != entities.end()) {
            scene.set_crouching(id, found->second.metadata.pose() == mcpp::proto::Pose::Crouching);
        }
    };
    events.entities.on_animate = [&scene](int32_t id, uint8_t action) { scene.animate(id, action); };
    events.entities.on_hurt = [&scene](int32_t id, float) { scene.hurt(id); };
    events.entities.on_damage = [&scene](int32_t id) { scene.damaged(id); };
    events.entities.on_event = [&scene](int32_t id, uint8_t status) { scene.event(id, status); };
    events.entities.on_player_info = [&scene](const mcpp::PlayerInfo& info) {
        scene.player_info(info);
    };
    events.entities.on_player_remove = [&scene](std::string_view uuid) { scene.player_gone(uuid); };

    // The blocks with a renderer of their own. The world area has already
    // taken these four, so each one is wrapped rather than replaced.
    auto& blocks = area.block_entities;
    if (!area.block_entity_table.open(
            mcpp::entity::BlockEntityTable::default_path(mcpp::protocol::kTargetProtocol))) {
        std::cout << "- no block entity table; bake it with tools/bake_all.py. No chest, "
                     "sign or banner will be drawn\n";
    }
    blocks.load(area.assets, area.block_entity_table, &*area.skins);
    events.world.on_block_entity = [&blocks, next = events.world.on_block_entity](
                                       int32_t x, int32_t y, int32_t z, int32_t type,
                                       const mcpp::nbt::Value& data) {
        if (next) next(x, y, z, type, data);
        blocks.put(x, y, z, data);
    };
    events.world.on_chunk = [&blocks, next = events.world.on_chunk](
                                const mcpp::world::Chunk& chunk) {
        if (next) next(chunk);
        for (const mcpp::world::BlockEntity& one : chunk.block_entities) {
            blocks.put(one.x, one.y, one.z, one.data);
        }
    };
    events.world.on_chunk_forget = [&blocks, next = events.world.on_chunk_forget](
                                       int32_t chunk_x, int32_t chunk_z) {
        if (next) next(chunk_x, chunk_z);
        blocks.forget_chunk(chunk_x, chunk_z);
    };
    events.world.on_block_change = [&game, &blocks, next = events.world.on_block_change](
                                       int32_t x, int32_t y, int32_t z, int32_t state) {
        if (next) next(x, y, z, state);
        // A block entity whose block is gone goes with it; one that stays
        // keeps what the server last said about it.
        if (game.entities.block_entity_table.find(state) == nullptr) blocks.removed(x, y, z);
    };
    events.world.on_respawn = [&blocks, next = events.world.on_respawn](
                                  const mcpp::Dimension& dimension) {
        if (next) next(dimension);
        blocks.clear();
    };
    events.world.on_block_event = [&blocks](int32_t x, int32_t y, int32_t z, uint8_t action,
                                            uint8_t parameter) {
        blocks.event(x, y, z, action, parameter);
    };
}

void entities_start(Game& game) {
    auto& area = game.entities;
    auto& renderer = area.renderer.emplace(game.device, game.renderer->colour_format());
    renderer.load(area.assets);
    area.scene->bind(renderer);
    game.renderer->set_in_world([&renderer](VkCommandBuffer command, const float* view_projection,
                                            uint32_t width, uint32_t height, const float* fog,
                                            const mcpp::gpu::Camera& camera) {
        const double eye[3] = {camera.eye[0], camera.eye[1], camera.eye[2]};
        renderer.record(command, view_projection, eye, width, height, fog, camera.fog_start,
                        camera.fog_end);
    });
}

void entities_input(Game&) {}

void entities_tick(Game& game) {
    auto& area = game.entities;
    const int ticks = area.clock.advance(game.dt);
    for (int i = 0; i < ticks; ++i) {
        area.scene->tick();
        area.block_entities.tick();
    }
}

void entities_draw(Game& game) {
    auto& area = game.entities;
    if (!area.renderer) return;
    auto& renderer = *area.renderer;
    renderer.begin();
    mcpp::entity::View view;
    for (int axis = 0; axis < 3; ++axis) {
        view.eye[axis] = game.camera.eye[axis];
        view.forward[axis] = game.camera.target[axis] - game.camera.eye[axis];
    }
    view.partial = area.clock.partial();
    const auto light = [&game](int32_t x, int32_t y, int32_t z) -> std::array<uint8_t, 2> {
        auto& world = game.world;
        std::lock_guard lock(world.world_guard);
        const auto found = world.world.find({x >> 4, z >> 4});
        if (found == world.world.end()) return {15, 0};
        const int32_t index = found->second.index_of(y);
        return {found->second.sky_light_at(x & 15, index, z & 15),
                found->second.block_light_at(x & 15, index, z & 15)};
    };
    area.scene->draw(renderer, view, light);

    const auto state = [&game](int32_t x, int32_t y, int32_t z) -> int32_t {
        auto& world = game.world;
        std::lock_guard lock(world.world_guard);
        const auto found = world.world.find({x >> 4, z >> 4});
        if (found == world.world.end()) return 0;
        return found->second.block_state(x & 15, found->second.index_of(y), z & 15);
    };
    area.block_entities.draw(renderer, view, state, light, game.world.game_time);
}

void entities_stop(Game& game) {
    game.renderer->set_in_world({});
    game.entities.renderer.reset();
}

} // namespace client
