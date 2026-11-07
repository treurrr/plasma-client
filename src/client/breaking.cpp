#include "client/breaking.hpp"

#include "client/game.hpp"
#include "client/game_mode.hpp"
#include "client/sound.hpp"

#include "mcpp/gpu/particles.hpp"
#include "mcpp/protocol_versions.hpp"
#include "mcpp/world/biomes.hpp"
#include "mcpp/world/destruction.hpp"
#include "mcpp/world/particles.hpp"

#include <chrono>
#include <cmath>
#include <format>
#include <iostream>

namespace client {

using mcpp::world::BlockShapes;

struct BreakingArea {
    mcpp::world::ParticleTable table;
    mcpp::world::ParticleSprites sprites;
    mcpp::world::ParticleEngine engine;
    mcpp::world::BlockDestruction destruction;
    BlockPrediction prediction;
    std::optional<MultiPlayerGameMode> mode;
    std::optional<mcpp::gpu::ParticleRenderer> renderer;
    std::function<void(int32_t, int32_t, int32_t, int32_t)> apply_block;
    mcpp::world::BlockAt at;
    std::vector<uint32_t> tints;
    std::vector<mcpp::world::ParticleQuad> quads;
    std::vector<mcpp::world::CrackVertex> cracks;
    std::vector<mcpp::world::BlockDestruction::Shown> shown;
    int32_t permission_level = 0;
    double tick_ms = 0.0;
    double draw_ms = 0.0;
    size_t ticks = 0;
    size_t frames = 0;
    size_t most = 0;
};

namespace {

BreakingArea* area(const Game& game) { return game.breaking.get(); }

// The block at a position while the caller holds the world: -1 where no
// chunk is, air outside the world's height.
int32_t block_unlocked(const Game& game, int32_t x, int32_t y, int32_t z) {
    const auto found = game.world.world.find({x >> 4, z >> 4});
    if (found == game.world.world.end()) return -1;
    const int32_t index = found->second.index_of(y);
    if (index < 0 || index >= static_cast<int32_t>(found->second.sections.size()) * 16) return 0;
    return found->second.block_state(x & 15, index, z & 15);
}

// `LevelRenderer.getLightColor`: the block's light, or full for an emissive one.
int32_t light_unlocked(const Game& game, int32_t x, int32_t y, int32_t z) {
    const auto found = game.world.world.find({x >> 4, z >> 4});
    if (found == game.world.world.end()) return -1;
    const auto& chunk = found->second;
    const int32_t index = chunk.index_of(y);
    const int32_t state = block_unlocked(game, x, y, z);
    const auto& render = game.world.render.at(std::max(0, state));
    if (render.has(mcpp::world::RenderInfo::kEmissive)) return 15728880;
    const int32_t sky = chunk.sky_light_at(x & 15, index, z & 15);
    const int32_t block = std::max<int32_t>(chunk.block_light_at(x & 15, index, z & 15), render.emission);
    return block << 4 | sky << 20;
}

uint32_t biome_unlocked(const Game& game, const BreakingArea& b, mcpp::world::ParticleTable::Colour colour,
                        int32_t x, int32_t y, int32_t z) {
    using C = mcpp::world::ParticleTable::Colour;
    const auto found = game.world.world.find({x >> 4, z >> 4});
    const int32_t biome = found == game.world.world.end()
                              ? 0
                              : found->second.biome_at(x & 15, found->second.index_of(y), z & 15);
    const size_t slot = static_cast<size_t>(std::max(0, biome)) * 4 +
                        (colour == C::Grass ? 0 : colour == C::Foliage ? 1 : colour == C::Water ? 2 : 3);
    return slot < b.tints.size() ? b.tints[slot] & 0xFFFFFFu : 0xFFFFFFu;
}

mcpp::world::ParticleLevel particle_level(const Game& game, const BreakingArea& b) {
    return mcpp::world::ParticleLevel{
        b.at, BlockShapes::game(), b.table,
        [&game](int32_t x, int32_t y, int32_t z) { return light_unlocked(game, x, y, z); },
        [&game, &b](mcpp::world::ParticleTable::Colour c, int32_t x, int32_t y, int32_t z) {
            return biome_unlocked(game, b, c, x, y, z);
        },
        game.session->dimension().min_y, b.sprites.is_open() ? &b.sprites : nullptr};
}

// What a level_particles packet turns into: the type we know by that name and
// the options the packet carried.
void spawn_packet_particles(Game& game, const mcpp::LevelParticles& packet) {
    auto* b = area(game);
    if (b == nullptr || !b->sprites.is_open()) return;
    const auto type = mcpp::world::particle_type_from_name(packet.type);
    if (type == mcpp::world::ParticleType::None) return;
    mcpp::world::ParticleOptions options;
    options.block_state = packet.block_state;
    options.colour = packet.colour;
    options.to_colour = packet.to_colour;
    options.scale = packet.scale;
    options.power = packet.power;
    std::lock_guard lock(game.world.world_guard);
    b->engine.spawn_cloud(particle_level(game, *b), type, options, packet.x, packet.y, packet.z,
                          packet.x_dist, packet.y_dist, packet.z_dist, packet.max_speed, packet.count);
}

const mcpp::ItemStack& held(const Game& game) {
    const auto& inventory = game.session->inventory();
    return inventory.hotbar(static_cast<size_t>(inventory.held));
}

// The particles of a block broken, ours or someone else's.
void destroy_effect(Game& game, const int32_t pos[3], int32_t state) {
    auto* b = area(game);
    if (b == nullptr || !b->table.is_open()) return;
    std::lock_guard lock(game.world.world_guard);
    b->engine.destroy(particle_level(game, *b), pos, state);
}

// `ClientLevel.syncBlockState`: the server's block back, the player put back if it now stands in it.
void sync_block(Game& game, const std::array<int32_t, 3>& pos, const BlockPrediction::Verified& v) {
    auto* b = area(game);
    int32_t now = 0;
    {
        std::lock_guard lock(game.world.world_guard);
        now = std::max(0, block_unlocked(game, pos[0], pos[1], pos[2]));
    }
    if (now == v.state) return;
    b->apply_block(pos[0], pos[1], pos[2], v.state);
    auto& body = game.player.body;
    const mcpp::world::BlockAt only = [&](int32_t x, int32_t y, int32_t z) {
        return x == pos[0] && y == pos[1] && z == pos[2] ? v.state : 0;
    };
    if (!mcpp::world::fits(body, body.pose, mcpp::world::World{only, BlockShapes::game(), nullptr, -64})) {
        mcpp::world::place(body, v.player[0], v.player[1], v.player[2]);
    }
}

} // namespace

void breaking_bind(Game& game, mcpp::SessionEvents& events) {
    game.breaking = std::make_shared<BreakingArea>();
    auto& b = *game.breaking;
    b.at = [&game](int32_t x, int32_t y, int32_t z) { return block_unlocked(game, x, y, z); };
    b.apply_block = events.world.on_block_change;
    // `ClientLevel.setServerVerifiedBlockState`: held back while our own change stands.
    events.world.on_block_change = [&game](int32_t x, int32_t y, int32_t z, int32_t state) {
        auto* a = area(game);
        const int32_t pos[3] = {x, y, z};
        if (a->prediction.update(pos, state)) return;
        if (a->apply_block) a->apply_block(x, y, z, state);
    };
    events.world.on_particles = [&game](const mcpp::LevelParticles& packet) {
        spawn_packet_particles(game, packet);
    };
    // `ClientLevel.handleBlockChangedAck`: what the server answered, made true.
    events.player.on_block_changed_ack = [&game](int32_t sequence) {
        for (const auto& [pos, verified] : area(game)->prediction.end_up_to(sequence)) sync_block(game, pos, verified);
    };
    // `ClientPacketListener.handleBlockDestruction`: someone else's crack.
    events.player.on_block_destruction = [&game](int32_t id, int32_t x, int32_t y, int32_t z, int32_t stage) {
        const int32_t pos[3] = {x, y, z};
        area(game)->destruction.progress(id, pos, stage);
    };
    // `ClientPacketListener.handleEntityEvent`: 24 to 28 are our operator level.
    auto entity_event = events.entities.on_event;
    events.entities.on_event = [&game, entity_event](int32_t id, uint8_t status) {
        if (entity_event) entity_event(id, status);
        if (status >= 24 && status <= 28 && game.session && id == game.session->entity_id()) {
            area(game)->permission_level = status - 24;
            if (area(game)->mode) area(game)->mode->permission_level = status - 24;
        }
    };
}

void breaking_start(Game& game) {
    auto& b = *area(game);
    const int32_t protocol = mcpp::protocol::kTargetProtocol;
    if (!b.table.open(mcpp::world::ParticleTable::default_path(protocol), game.table)) {
        std::cout << "- no particle table; bake one with tools/bake_particles.py\n";
    }
    if (!b.sprites.open(mcpp::world::ParticleSprites::default_path(protocol),
                        static_cast<uint32_t>(game.table.texture_count()))) {
        std::cout << "- no particle sprites; bake them with tools/bake_particle_sprites.py\n";
    }
    if (!game.session->biomes().empty()) {
        mcpp::world::Colormaps maps;
        maps.open(mcpp::world::Colormaps::default_path(protocol));
        b.tints = game.session->biomes().tints(maps);
    }
    GameModeLevel level;
    level.state_at = [&game](const int32_t* p) {
        std::lock_guard lock(game.world.world_guard);
        return std::max(0, block_unlocked(game, p[0], p[1], p[2]));
    };
    level.set_block = [&game](const int32_t* p, int32_t state) {
        int32_t before = 0;
        {
            std::lock_guard lock(game.world.world_guard);
            before = block_unlocked(game, p[0], p[1], p[2]);
        }
        if (before < 0 || before == state) return false;
        world_predict_block(game, p[0], p[1], p[2], state);
        return true;
    };
    level.break_effect = [&game](const int32_t* p, int32_t state) {
        sound_block(game, p[0], p[1], p[2], state, mcpp::audio::BlockSoundKind::Break);
        destroy_effect(game, p, state);
    };
    level.progress = [&game](const int32_t* p, int32_t stage) {
        area(game)->destruction.progress(game.session->entity_id(), p, stage);
    };
    level.hit_sound = [&game](const int32_t* p, int32_t state) {
        sound_block(game, p[0], p[1], p[2], state, mcpp::audio::BlockSoundKind::Hit);
    };
    level.held = [&game]() -> const mcpp::ItemStack& { return held(game); };
    level.destroy_progress = [&game](int32_t state) {
        const auto& stack = held(game);
        const auto tool = tool_on_block(ToolTable::game().find(stack.count > 0 ? stack.id : -1), state,
                                        BlockShapes::game(), game.session->tags());
        const auto& info = BlockShapes::game().state(state);
        return destroy_progress(info.hardness, (info.flags & mcpp::world::kNeedsTool) != 0,
                                dig_context(*game.session, game.player.body, tool));
    };
    level.player_position = [&game](double* out) {
        out[0] = game.player.body.x;
        out[1] = game.player.body.y;
        out[2] = game.player.body.z;
    };
    b.mode.emplace(*game.session, b.prediction, std::move(level));
    b.mode->permission_level = b.permission_level;

    auto& renderer = *game.renderer;
    b.renderer.emplace(game.device, renderer, mcpp::world::ParticleTable::cracks_path(protocol));
    renderer.set_after_entities([&game](VkCommandBuffer command, const float* view_projection, uint32_t, uint32_t,
                                        const float*, const mcpp::gpu::Camera& camera) {
        area(game)->renderer->record_cracks(command, view_projection, camera);
    });
    renderer.set_after_translucent([&game](VkCommandBuffer command, const float* view_projection, uint32_t,
                                           uint32_t, const float*, const mcpp::gpu::Camera& camera) {
        area(game)->renderer->record_particles(command, view_projection, camera);
    });
}

bool breaking_start_attack(Game& game) {
    auto* b = area(game);
    auto& player = game.player;
    if (b == nullptr || !b->mode || player.miss_time > 0) return false;
    auto& mode = *b->mode;
    bool broke = false;
    const auto air = [&](const int32_t* p) {
        std::lock_guard lock(game.world.world_guard);
        const int32_t state = std::max(0, block_unlocked(game, p[0], p[1], p[2]));
        return (BlockShapes::game().state(state).flags & mcpp::world::kAir) != 0;
    };
    if (player.aiming && !air(player.aimed)) {
        mode.start_destroy_block(player.aimed, player.hit_face);
        broke = air(player.aimed);
    } else if (mode.has_miss_time()) {
        player.miss_time = 10;
    }
    if (!mode.spectator()) game.session->send_swing();
    return broke;
}

void breaking_continue_attack(Game& game, bool down) {
    auto* b = area(game);
    auto& player = game.player;
    if (b == nullptr || !b->mode) return;
    if (!down) player.miss_time = 0;
    if (player.miss_time > 0 || player.using_item) return;
    if (down && player.aiming) {
        int32_t state = 0;
        {
            std::lock_guard lock(game.world.world_guard);
            state = std::max(0, block_unlocked(game, player.aimed[0], player.aimed[1], player.aimed[2]));
        }
        if ((BlockShapes::game().state(state).flags & mcpp::world::kAir) != 0) return;
        if (b->mode->continue_destroy_block(player.aimed, player.hit_face)) {
            if (b->table.is_open()) {
                std::lock_guard lock(game.world.world_guard);
                b->engine.crack(particle_level(game, *b), player.aimed, player.hit_face);
            }
            game.session->send_swing();
        }
    } else {
        b->mode->stop_destroy_block();
    }
}

bool breaking_destroying(const Game& game) {
    const auto* b = area(game);
    return b != nullptr && b->mode && b->mode->destroying();
}

void breaking_tick(Game& game) {
    auto* b = area(game);
    if (b == nullptr) return;
    const auto started = std::chrono::steady_clock::now();
    b->destruction.tick();
    {
        std::lock_guard lock(game.world.world_guard);
        b->engine.tick(particle_level(game, *b));
    }
    b->most = std::max(b->most, b->engine.count());
    b->tick_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    ++b->ticks;
}

void breaking_draw(Game& game) {
    auto* b = area(game);
    if (b == nullptr || !b->renderer) return;
    const auto started = std::chrono::steady_clock::now();
    const auto& camera = game.camera;
    const auto& body = game.player.body;
    mcpp::world::ParticleView view{};
    for (int i = 0; i < 3; ++i) view.camera[i] = camera.eye[i];
    mcpp::world::camera_rotation(body.yaw, body.pitch, view.rotation);
    view.partial = static_cast<float>(game.player.partial_tick);
    // `Frustum.offset`: the view's apex three blocks behind the camera.
    float forward[3] = {camera.target[0] - camera.eye[0], camera.target[1] - camera.eye[1],
                        camera.target[2] - camera.eye[2]};
    const float length = std::sqrt(forward[0] * forward[0] + forward[1] * forward[1] + forward[2] * forward[2]);
    for (auto& f : forward) f /= length;
    for (int i = 0; i < 3; ++i) view.apex[i] = camera.eye[i] - forward[i] * 3.0;
    const auto aspect = static_cast<float>(game.renderer->width()) / static_cast<float>(game.renderer->height());
    const auto m = mcpp::gpu::camera_projection(camera, aspect) * mcpp::gpu::camera_rotation(camera);
    const auto row = [&](int r, int a) { return m.m[a * 4 + r]; };
    for (int p = 0; p < 4; ++p) {
        const int r = p / 2;
        const float sign = p % 2 == 0 ? 1.0f : -1.0f;
        for (int a = 0; a < 4; ++a) view.planes[p][a] = row(3, a) + sign * row(r, a);
    }
    view.planes[4][0] = forward[0];
    view.planes[4][1] = forward[1];
    view.planes[4][2] = forward[2];
    view.planes[4][3] = -0.05f;

    b->cracks.clear();
    {
        std::lock_guard lock(game.world.world_guard);
        b->engine.extract(view, particle_level(game, *b), b->quads);
        b->destruction.shown(view.camera, b->shown);
        const auto& shapes = BlockShapes::game();
        for (const auto& s : b->shown) {
            const int32_t state = std::max(0, block_unlocked(game, s.pos[0], s.pos[1], s.pos[2]));
            static constexpr int kStep[6][3] = {{0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}};
            bool hidden[6];
            for (int f = 0; f < 6; ++f) {
                const int32_t n = block_unlocked(game, s.pos[0] + kStep[f][0], s.pos[1] + kStep[f][1],
                                                 s.pos[2] + kStep[f][2]);
                hidden[f] = n > 0 && (game.table.occludes(n) ||
                                      (game.table.self_culls(state) && game.table.block_of(n) == game.table.block_of(state)));
            }
            double offset[3] = {0.0, 0.0, 0.0};
            if (shapes.block(state).offset != 0) shapes.offset(state, s.pos[0], s.pos[2], offset);
            mcpp::world::crack_quads(game.table, state, s.pos, offset, hidden, view.camera,
                                     static_cast<uint32_t>(s.stage), b->cracks);
        }
    }
    b->renderer->set_frame(b->quads, b->cracks, view.camera);
    b->draw_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    ++b->frames;
}

void breaking_stop(Game& game) {
    auto* b = area(game);
    if (b == nullptr) return;
    if (b->ticks > 0 && b->frames > 0) {
        std::cout << std::format("- particles: {:.3f} ms a tick, {:.3f} ms a frame on the cpu, at most {} alive\n",
                                 b->tick_ms / static_cast<double>(b->ticks),
                                 b->draw_ms / static_cast<double>(b->frames), b->most);
    }
}

// `WeatherEffectRenderer.tickRainParticles`: the drop it adds, which is smoke
// where it lands on lava, magma or a lit campfire.
void breaking_spawn_rain(Game& game, bool smoke, double x, double y, double z) {
    auto* b = area(game);
    if (b == nullptr || !b->sprites.is_open()) return;
    const auto type = smoke ? mcpp::world::ParticleType::Smoke : mcpp::world::ParticleType::Rain;
    b->engine.spawn(particle_level(game, *b), type, {}, x, y, z, 0.0, 0.0, 0.0);
}

void breaking_level_event(Game& game, int32_t type, int32_t x, int32_t y, int32_t z, int32_t data) {
    // `LevelEventHandler.levelEvent`: 2001, a block broken, shows its bits; the sound area plays its sound.
    const int32_t pos[3] = {x, y, z};
    if (type == 2001) {
        destroy_effect(game, pos, data);
        return;
    }
    auto* b = area(game);
    if (b == nullptr || !b->sprites.is_open()) return;
    std::lock_guard lock(game.world.world_guard);
    b->engine.level_event(particle_level(game, *b), type, pos, data);
}

} // namespace client
