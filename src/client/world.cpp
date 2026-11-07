#include "client/game.hpp"

#include "client/breaking.hpp"
#include "client/sound.hpp"
#include "mcpp/gpu/weather.hpp"
#include "mcpp/protocol_versions.hpp"
#include "mcpp/jmath.hpp"
#include "mcpp/world/biomes.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <iostream>
#include <iterator>

namespace client {
namespace {

using mcpp::world::Chunk;

// The biome the game reads at a quarter-block cell: y clamped into the world,
// and plains where nothing is loaded.
template <typename FindChunk>
int32_t biome_cell(FindChunk&& find_chunk, int32_t plains, int32_t qx, int32_t qy, int32_t qz) {
    const Chunk* chunk = find_chunk(qx >> 2, qz >> 2);
    if (chunk == nullptr) return plains;
    const int32_t top = static_cast<int32_t>(chunk->sections.size()) * 4 - 1;
    const int32_t qy_index = std::clamp(qy - (chunk->min_y >> 2), 0, std::max(top, 0));
    return chunk->biome_at((qx * 4) & 15, qy_index * 4, (qz * 4) & 15);
}

template <typename FindChunk>
mcpp::world::AttributeValues attributes_at(const mcpp::Session& session, int64_t day_time,
                                           const double eye[3], FindChunk&& find_chunk,
                                           const mcpp::Weather& weather) {
    const int32_t plains = std::max(
        session.registries().index_of("minecraft:worldgen/biome", "minecraft:plains"), 0);
    const auto cell = [&](int32_t qx, int32_t qy, int32_t qz) {
        return biome_cell(find_chunk, plains, qx, qy, qz);
    };
    const auto weights = mcpp::world::gaussian_biomes(eye[0], eye[1], eye[2], cell);
    const auto floor_int = [](double v) { return static_cast<int32_t>(std::floor(v)); };
    const int32_t underfoot =
        cell(floor_int(eye[0]) >> 2, floor_int(eye[1]) >> 2, floor_int(eye[2]) >> 2);
    return session.environment().evaluate(
        day_time, weights, static_cast<size_t>(std::max(underfoot, 0)),
        {weather.rain_level, weather.thunder_level * weather.rain_level});
}

// Camera.getFluidInCamera, for the camera's own block: water when below its
// surface, lava or powder snow when the eye is in them.
template <typename BlockAt>
mcpp::world::Medium medium_at(const float eye[3], BlockAt&& block_at,
                              const mcpp::world::RenderTable& fluids,
                              const mcpp::world::BlockTable& table) {
    const int32_t x = static_cast<int32_t>(std::floor(eye[0]));
    const int32_t y = static_cast<int32_t>(std::floor(eye[1]));
    const int32_t z = static_cast<int32_t>(std::floor(eye[2]));
    const int32_t state = block_at(x, y, z);
    const auto fluid = fluids.fluid(state);
    if (fluid.fluid != mcpp::world::Fluid::None) {
        const bool same_above = fluids.fluid(block_at(x, y + 1, z)).fluid == fluid.fluid;
        const float height = same_above ? 1.0f : fluid.own_height();
        if (eye[1] < static_cast<float>(y) + height) {
            return fluid.fluid == mcpp::world::Fluid::Water ? mcpp::world::Medium::Water
                                                            : mcpp::world::Medium::Lava;
        }
    }
    if (state > 0 && table.block_name(state) == "minecraft:powder_snow") {
        return mcpp::world::Medium::PowderSnow;
    }
    return mcpp::world::Medium::Air;
}

float water_vision(int32_t time) {
    if (time >= 600) return 1.0f;
    const float early = std::clamp(static_cast<float>(time) / 100.0f, 0.0f, 1.0f);
    const float late = time < 100 ? 0.0f
                                  : std::clamp((static_cast<float>(time) - 100.0f) / 500.0f, 0.0f, 1.0f);
    return early * 0.6f + late * 0.39999998f;
}

mcpp::world::AtmosphereInputs inputs_for(const mcpp::Session& session, const mcpp::gpu::Camera& camera,
                                         int32_t render_distance, const mcpp::Weather& weather) {
    mcpp::world::AtmosphereInputs in;
    float forward[3] = {camera.target[0] - camera.eye[0], camera.target[1] - camera.eye[1],
                        camera.target[2] - camera.eye[2]};
    const float length =
        std::sqrt(forward[0] * forward[0] + forward[1] * forward[1] + forward[2] * forward[2]);
    for (int i = 0; i < 3; ++i) {
        in.forward[i] = length > 0.0f ? forward[i] / length : 0.0f;
        in.eye[i] = camera.eye[i];
    }
    const auto& dimension = session.dimension();
    in.min_y = dimension.min_y;
    in.flat_world = dimension.flat_world;
    in.render_distance = render_distance;
    in.rain = weather.rain_level;
    in.thunder = weather.thunder_level * weather.rain_level;
    in.skybox = mcpp::world::skybox_named(dimension.skybox);
    in.ambient_light = dimension.ambient_light;
    return in;
}

void forget_chunk(Game& game, int32_t chunk_x, int32_t chunk_z) {
    auto& area = game.world;
    {
        std::lock_guard lock(area.world_guard);
        area.world.erase({chunk_x, chunk_z});
    }
    {
        // Out of the seen set too, or walking back into it would never
        // load it again.
        std::lock_guard lock(area.guard);
        area.seen.erase({chunk_x, chunk_z});
        area.gone.insert({chunk_x, chunk_z});
        std::erase_if(area.pending, [&](const mcpp::world::Chunk& chunk) {
            return chunk.x == chunk_x && chunk.z == chunk_z;
        });
    }
    std::lock_guard lock(area.dirty_guard);
    area.dirty.erase({chunk_x, chunk_z});
    area.forgotten.push_back({chunk_x, chunk_z});
}

} // namespace

void world_bind(Game& game, mcpp::SessionEvents& events) {
    events.world.on_block_change = [&game](int32_t x, int32_t y, int32_t z, int32_t state) {
        auto& area = game.world;
        const int32_t chunk_x = x >> 4;
        const int32_t chunk_z = z >> 4;
        std::set<std::pair<int32_t, int32_t>> relit;
        {
            std::lock_guard lock(area.world_guard);
            const auto found = area.world.find({chunk_x, chunk_z});
            if (found == area.world.end()) return;
            if (!found->second.set_block_state(x & 15, found->second.index_of(y), z & 15, state)) return;
            relit = area.light.relight(area.world, x, y, z);
        }
        std::lock_guard lock(area.dirty_guard);
        area.dirty.insert(relit.begin(), relit.end());
        area.dirty.insert({chunk_x, chunk_z});
        // A block on a seam changes what its neighbour may cull, so the chunk
        // on the other side has to be meshed again too.
        if ((x & 15) == 0) area.dirty.insert({chunk_x - 1, chunk_z});
        if ((x & 15) == 15) area.dirty.insert({chunk_x + 1, chunk_z});
        if ((z & 15) == 0) area.dirty.insert({chunk_x, chunk_z - 1});
        if ((z & 15) == 15) area.dirty.insert({chunk_x, chunk_z + 1});
    };

    events.world.on_chunk_forget = [&game](int32_t chunk_x, int32_t chunk_z) {
        forget_chunk(game, chunk_x, chunk_z);
    };

    // A new dimension is a new world: nothing the client held is still there.
    // Every chunk goes the way a forgotten one goes, so the renderer hands back
    // its room and the mesher drops anything it is still holding.
    events.world.on_respawn = [&game](const mcpp::Dimension&) {
        auto& area = game.world;
        std::vector<std::pair<int32_t, int32_t>> held;
        {
            std::lock_guard lock(area.world_guard);
            for (const auto& [key, chunk] : area.world) held.push_back(key);
        }
        {
            std::lock_guard lock(area.guard);
            for (const auto& chunk : area.pending) held.emplace_back(chunk.x, chunk.z);
        }
        for (const auto& [chunk_x, chunk_z] : held) forget_chunk(game, chunk_x, chunk_z);
    };

    events.world.on_chunk_area = [&game](int32_t centre_x, int32_t centre_z, int32_t radius) {
        auto& area = game.world;
        // A couple of chunks of slack past the edge: dropping one the server
        // still counts as ours leaves a hole until it is sent again, and
        // keeping one a little longer costs only memory.
        const int32_t reach = radius + 2;
        std::vector<std::pair<int32_t, int32_t>> outside;
        {
            std::lock_guard lock(area.world_guard);
            for (const auto& [key, chunk] : area.world) {
                if (std::abs(key.first - centre_x) > reach ||
                    std::abs(key.second - centre_z) > reach) {
                    outside.push_back(key);
                }
            }
        }
        for (const auto& [chunk_x, chunk_z] : outside) {
            forget_chunk(game, chunk_x, chunk_z);
        }
    };

    events.world.on_light_update = [&game](int32_t chunk_x, int32_t chunk_z,
                                           mcpp::ByteBuffer& in) {
        auto& area = game.world;
        {
            std::lock_guard lock(area.world_guard);
            const auto found = area.world.find({chunk_x, chunk_z});
            if (found != area.world.end()) {
                mcpp::world::read_light_into(in, found->second);
            } else {
                // Light for a chunk the mesher has not handed over yet still
                // belongs to it; light for one we do not hold is read past.
                std::lock_guard pending_lock(area.guard);
                mcpp::world::Chunk* waiting = nullptr;
                for (auto* list : {&area.pending, &area.chunks}) {
                    for (auto& chunk : *list) {
                        if (chunk.x == chunk_x && chunk.z == chunk_z) waiting = &chunk;
                    }
                }
                if (waiting != nullptr) {
                    mcpp::world::read_light_into(in, *waiting);
                } else {
                    in.read_bytes(in.remaining());
                }
                return;
            }
        }
        std::lock_guard lock(area.dirty_guard);
        area.dirty.insert({chunk_x, chunk_z});
    };

    events.world.on_chunk = [&game](const mcpp::world::Chunk& chunk) {
        auto& area = game.world;
        std::lock_guard lock(area.guard);
        // A server re-sends a chunk whenever it changes. Without this the same
        // geometry is appended again and again until the buffer is full.
        if (!area.seen.insert({chunk.x, chunk.z}).second) return;
        area.gone.erase({chunk.x, chunk.z});
        if (!area.enough.load()) {
            area.chunks.push_back(chunk);
            if (area.chunks.size() >= game.chunk_limit) area.enough.store(true);
        } else {
            area.pending.push_back(chunk);
        }
    };
}

// The colours the mesher averages over a biome border, built once: the colour
// maps are read off disk and the server's biome list stays put while connected.
// Filled before the mesher thread starts, and only read after that.
static const mcpp::world::BiomeTints& biome_tints(Game& game) {
    auto& tints = game.world.biome_tints;
    if (tints.colour.empty() && game.session && !game.session->biomes().empty()) {
        mcpp::world::Colormaps maps;
        maps.open(mcpp::world::Colormaps::default_path(mcpp::protocol::kTargetProtocol));
        tints.colour = game.session->biomes().tints(maps);
        tints.blend_radius = game.screens.options.get_int("biomeBlendRadius");
    }
    return tints;
}

void world_load(Game& game) {
    auto& area = game.world;
    double mesh_ms = 0.0;
    area.first_meshes = mesh_world(area.chunks, game.table, &mesh_ms, biome_tints(game));
    std::lock_guard lock(area.world_guard);
    for (const auto& chunk : area.chunks) area.world.insert_or_assign({chunk.x, chunk.z}, chunk);
}

void world_start(Game& game) {
    auto& renderer = *game.renderer;

    for (const auto& section : game.world.first_meshes) {
        game.world.vertex_bytes += section.vertices.size() * sizeof(mcpp::world::Vertex);
    }
    // A server with view distance 10 sends 441 chunks. Measured on real
    // terrain the vertex and index data come out around 2.7 to 1, so an even
    // split would run out of indices long before it ran out of room.
    renderer.reserve(384u << 20, 160u << 20);
    renderer.append(game.world.first_meshes);

    const auto sky_path = mcpp::gpu::Sky::default_path(mcpp::protocol::kTargetProtocol);
    if (std::filesystem::exists(sky_path)) {
        game.world.sky.emplace(game.device, renderer.colour_format(), renderer.depth_format(),
                               sky_path);
        renderer.set_sky(&*game.world.sky);
    }
    if (!biome_tints(game).colour.empty()) {
        renderer.set_biome_colours(game.world.biome_tints.colour);
    }
    if (std::filesystem::exists(sky_path)) {
        game.world.weather_renderer =
            std::make_shared<mcpp::gpu::WeatherRenderer>(game.device, renderer, sky_path);
        if (game.world.weather_renderer->ready()) {
            renderer.set_weather(game.world.weather_renderer.get());
        }
    }
    game.world.render.open(
        mcpp::world::RenderTable::default_path(mcpp::protocol::kTargetProtocol));
    game.world.render_distance = game.options.view_distance;
    game.world.outline.emplace(game.device, renderer.colour_format());
    renderer.set_outline(&*game.world.outline);

    game.world.block_at = [&game](int32_t x, int32_t y, int32_t z) -> int32_t {
        auto& area = game.world;
        std::lock_guard lock(area.world_guard);
        const auto found = area.world.find({x >> 4, z >> 4});
        if (found == area.world.end()) return -1;
        const int32_t index = found->second.index_of(y);
        if (index < 0) return 1;
        if (index >= static_cast<int32_t>(found->second.sections.size()) * 16) return 0;
        return found->second.block_state(x & 15, index, z & 15);
    };

    game.world.mesher = std::thread([&game] {
        auto& area = game.world;
        constexpr size_t kChunksPerBatch = 12;
        while (area.streaming.load()) {
            std::vector<mcpp::world::Chunk> arrived;
            {
                std::lock_guard lock(area.guard);
                const size_t take = std::min(area.pending.size(), kChunksPerBatch);
                if (take > 0) {
                    arrived.assign(std::make_move_iterator(area.pending.begin()),
                                   std::make_move_iterator(area.pending.begin() + take));
                    area.pending.erase(area.pending.begin(), area.pending.begin() + take);
                }
            }
            if (arrived.empty()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(4));
                continue;
            }
            double batch_ms = 0.0;
            auto pieces = mesh_world(arrived, game.table, &batch_ms, game.world.biome_tints);
            const size_t count = arrived.size();
            {
                std::lock_guard lock(area.guard);
                if (!area.gone.empty()) {
                    std::erase_if(arrived, [&](const mcpp::world::Chunk& chunk) {
                        return area.gone.contains({chunk.x, chunk.z});
                    });
                    std::erase_if(pieces, [&](const mcpp::world::SectionMesh& piece) {
                        return area.gone.contains({piece.origin[0] >> 4, piece.origin[2] >> 4});
                    });
                }
            }
            {
                std::lock_guard lock(area.world_guard);
                for (auto& chunk : arrived) {
                    area.world.insert_or_assign({chunk.x, chunk.z}, std::move(chunk));
                }
            }
            std::lock_guard lock(area.ready_guard);
            area.ready.push_back(WorldArea::Batch{std::move(pieces), count});
        }
    });
}

void world_input(Game&) {}

// The client's own tick: the day moves on between the server's corrections,
// the camera's attributes are read once a tick, and a frame blends the last
// two the way the game's probe does.
void advance_sky(Game& game) {
    auto& area = game.world;
    auto& session = *game.session;
    if (session.environment().selected().empty()) return;
    const auto& time = session.time();
    if (time.updates != area.time_updates) {
        area.time_updates = time.updates;
        area.day_time = time.day_time;
        area.game_time = time.game_time;
        area.time_ticking = time.ticking;
    }
    const double eye[3] = {game.camera.eye[0], game.camera.eye[1], game.camera.eye[2]};
    const auto find_chunk = [&](int32_t cx, int32_t cz) -> const Chunk* {
        const auto found = area.world.find({cx, cz});
        return found == area.world.end() ? nullptr : &found->second;
    };
    const auto sample = [&] {
        std::lock_guard lock(area.world_guard);
        return attributes_at(session, area.day_time, eye, find_chunk, session.weather());
    };
    const auto block_at = [&](int32_t x, int32_t y, int32_t z) { return area.block_at(x, y, z); };
    const auto medium = medium_at(game.camera.eye, block_at, area.render, game.table);
    if (!area.probed) {
        area.probe.reset(sample());
        area.probed = true;
    }

    const double frame_ticks = static_cast<double>(game.dt) * 20.0;
    area.ticks += frame_ticks;
    int ticked = 0;
    while (area.ticks >= 1.0 && ticked < 10) {
        area.ticks -= 1.0;
        ++ticked;
        ++area.game_time;
        if (area.time_ticking) ++area.day_time;
        area.probe.tick(sample());
        mcpp::jmath::LegacyRandom random(static_cast<int64_t>(area.flicker_seed));
        area.flicker += (random.next_float() - random.next_float()) * random.next_float() *
                        random.next_float() * 0.1f;
        area.flicker *= 0.9f;
        area.flicker_seed = static_cast<uint64_t>(random.next_long());
        if (medium == mcpp::world::Medium::Water) {
            area.water_vision_time = std::clamp(area.water_vision_time + 1, 0, 600);
        } else if (area.water_vision_time > 0) {
            area.water_vision_time = std::clamp(area.water_vision_time - 10, 0, 600);
        }
    }
    if (ticked == 10) area.ticks = 0.0;
    area.partial = static_cast<float>(area.ticks);

    auto in = inputs_for(session, game.camera, area.render_distance, session.weather());
    in.medium = medium;
    in.water_vision = water_vision(area.water_vision_time);
    in.block_flicker = area.flicker;
    {
        const int32_t x = static_cast<int32_t>(std::floor(eye[0]));
        const int32_t y = static_cast<int32_t>(std::floor(eye[1]));
        const int32_t z = static_cast<int32_t>(std::floor(eye[2]));
        int32_t sky_light = 15;
        bool precipitation = true;
        {
            std::lock_guard lock(area.world_guard);
            if (const auto* chunk = find_chunk(x >> 4, z >> 4)) {
                const int32_t index = chunk->index_of(y);
                if (index >= 0 && index < static_cast<int32_t>(chunk->sections.size()) * 16) {
                    sky_light = chunk->sky_light_at(x & 15, index, z & 15);
                    const auto* biome = session.biomes().at(
                        static_cast<size_t>(std::max(chunk->biome_at(x & 15, index, z & 15), 0)));
                    if (biome != nullptr) precipitation = biome->has_precipitation;
                }
            }
        }
        area.rain_fog.update(in.rain, sky_light, precipitation, static_cast<float>(frame_ticks));
    }
    in.rain_fog = area.rain_fog.multiplier;
    game.renderer->set_atmosphere(mcpp::world::atmosphere(area.probe, area.partial, in));
    if (area.sky) area.sky->set_cloud_time(area.game_time, area.partial);
    game.renderer->set_texture_ticks(static_cast<uint64_t>(std::max<int64_t>(area.game_time, 0)));
}


// What the weather sees of the world: the top of a column, what falls on it,
// and the light it falls through. The world lock is already held.
mcpp::world::WeatherLevel weather_level(Game& game) {
    auto& area = game.world;
    const auto& session = *game.session;
    const int32_t sea_level = session.dimension().sea_level;
    mcpp::world::WeatherLevel level;
    level.min_y = session.dimension().min_y;
    level.height = [&area, min_y = session.dimension().min_y](int32_t x, int32_t z) {
        const auto found = area.world.find({x >> 4, z >> 4});
        return found == area.world.end() ? min_y : found->second.motion_blocking_at(x & 15, z & 15);
    };
    level.precipitation = [&area, &session, sea_level](int32_t x, int32_t y, int32_t z) {
        // `WeatherEffectRenderer.getPrecipitationAt`: nothing at all where the
        // chunk is not loaded, so the weather stops at the world's edge.
        const auto found = area.world.find({x >> 4, z >> 4});
        if (found == area.world.end()) return mcpp::world::Precipitation::None;
        const int32_t index = found->second.index_of(y);
        const int32_t rows = static_cast<int32_t>(found->second.sections.size()) * 16;
        const int32_t row = std::clamp(index, 0, rows - 1);
        const auto* biome = session.biomes().at(
            static_cast<size_t>(std::max(found->second.biome_at(x & 15, row, z & 15), 0)));
        if (biome == nullptr) return mcpp::world::Precipitation::None;
        return mcpp::world::precipitation_at(biome->has_precipitation, biome->temperature, y,
                                             sea_level);
    };
    level.light = [&area](int32_t x, int32_t y, int32_t z) {
        const auto found = area.world.find({x >> 4, z >> 4});
        if (found == area.world.end()) return 0;
        const int32_t index = found->second.index_of(y);
        const int32_t rows = static_cast<int32_t>(found->second.sections.size()) * 16;
        if (index < 0 || index >= rows) return mcpp::world::light_pack(0, 15);
        return mcpp::world::light_pack(found->second.block_light_at(x & 15, index, z & 15),
                                       found->second.sky_light_at(x & 15, index, z & 15));
    };
    return level;
}

// `WeatherEffectRenderer.extractRenderState`, `WeatherEffectRenderer.tickRainParticles`: the
// columns of falling texture around the camera, and once a tick the drops
// that land and the sound one of them makes.
void advance_weather(Game& game) {
    auto& area = game.world;
    const auto& weather = game.session->weather();
    const float intensity = weather.rain_level;
    const double camera[3] = {game.camera.eye[0], game.camera.eye[1], game.camera.eye[2]};
    const int32_t radius = std::clamp(game.screens.options.get_int("weatherRadius"), 2, 15);
    std::lock_guard lock(area.world_guard);
    const auto level = weather_level(game);
    mcpp::world::extract_weather(level, area.weather_ticks, area.partial, camera, intensity,
                                 radius, area.weather);
    if (!game.tick_due || intensity <= 0.0f) return;
    ++area.weather_ticks;

    mcpp::world::RainTick tick;
    tick.min_y = level.min_y;
    tick.height = level.height;
    tick.precipitation = level.precipitation;
    // The collision shape's top under the drop is the mesher's business and
    // the fluid height the physics'; a full block's top is what almost every
    // column comes to, and that is what is used here.
    tick.surface = [](int32_t, int32_t, int32_t, double, double) { return 1.0; };
    tick.hot = [&game, &area](int32_t x, int32_t y, int32_t z) {
        const auto found = area.world.find({x >> 4, z >> 4});
        if (found == area.world.end() || !game.table.is_open()) return false;
        const int32_t index = found->second.index_of(y);
        const int32_t rows = static_cast<int32_t>(found->second.sections.size()) * 16;
        if (index < 0 || index >= rows) return false;
        const auto name = game.table.block_name(found->second.block_state(x & 15, index, z & 15));
        return name == "minecraft:lava" || name == "minecraft:magma_block" ||
               name == "minecraft:campfire" || name == "minecraft:soul_campfire";
    };
    tick.particle = [&game](bool smoke, double x, double y, double z) {
        breaking_spawn_rain(game, smoke, x, y, z);
    };
    tick.sound = [&game](std::string_view event, int32_t x, int32_t y, int32_t z, float volume,
                         float pitch) {
        sound_world(game, event, mcpp::audio::Category::Weather, x + 0.5, y + 0.5, z + 0.5, volume,
                    pitch);
    };
    mcpp::world::tick_rain_particles(tick, area.weather_ticks, intensity, camera, radius,
                                     area.rain_sound_time);
}

void world_tick(Game& game) {
    auto& area = game.world;
    auto& renderer = *game.renderer;
    auto& session = *game.session;

    // Mesh and append whatever arrived, in batches, so one chunk does not
    // cost a submit of its own.
    // One finished batch a frame. Meshing happens on the worker, so all the
    // frame pays for is the copy to the gpu.
    std::vector<mcpp::world::SectionMesh> ready_batch;
    size_t ready_chunks = 0;
    {
        std::lock_guard lock(area.ready_guard);
        if (!area.ready.empty()) {
            ready_batch = std::move(area.ready.front().pieces);
            ready_chunks = area.ready.front().chunks;
            area.ready.erase(area.ready.begin());
        }
    }
    if (!ready_batch.empty()) {
        if (!renderer.append(ready_batch)) {
            std::cout << std::format("- out of {} room after {} sections; stopping here\n",
                                     renderer.out_of_vertex_room() ? "vertex" : "index",
                                     renderer.appended_sections());
            area.streaming.store(false);
        } else {
            const size_t before = area.streamed;
            area.streamed += ready_chunks;
            if (area.streamed / 100 != before / 100) {
                std::cout << std::format("- {} chunks streamed in, {} sections drawn\n",
                                         area.streamed, renderer.appended_sections());
            }
        }
    }

    {
        std::lock_guard lock(area.dirty_guard);
        for (const auto& [chunk_x, chunk_z] : area.forgotten) renderer.forget(chunk_x, chunk_z);
        area.forgotten.clear();
    }

    // Anything the server changed under us is meshed again here, at most a
    // couple of chunks a frame so a wall of changes cannot stall one.
    {
        std::vector<std::pair<int32_t, int32_t>> remesh;
        {
            std::lock_guard lock(area.dirty_guard);
            while (!area.dirty.empty() && remesh.size() < 2) {
                remesh.push_back(*area.dirty.begin());
                area.dirty.erase(area.dirty.begin());
            }
        }
        for (const auto& [chunk_x, chunk_z] : remesh) {
            std::lock_guard lock(area.world_guard);
            const auto found = area.world.find({chunk_x, chunk_z});
            if (found == area.world.end()) continue;
            const auto blocks = std::make_unique<mcpp::world::ChunkBlocks>(found->second);
            mcpp::world::Neighbours neighbours;
            std::vector<std::unique_ptr<mcpp::world::ChunkBlocks>> around;
            const auto side = [&](int32_t dx, int32_t dz)
                -> const mcpp::world::ChunkBlocks* {
                const auto side_chunk = area.world.find({chunk_x + dx, chunk_z + dz});
                if (side_chunk == area.world.end()) return nullptr;
                around.push_back(
                    std::make_unique<mcpp::world::ChunkBlocks>(side_chunk->second));
                return around.back().get();
            };
            neighbours.west = side(-1, 0);
            neighbours.east = side(1, 0);
            neighbours.north = side(0, -1);
            neighbours.south = side(0, 1);
            neighbours.north_west = side(-1, -1);
            neighbours.north_east = side(1, -1);
            neighbours.south_west = side(-1, 1);
            neighbours.south_east = side(1, 1);
            renderer.replace(mcpp::world::mesh_sections(found->second, *blocks, game.table,
                                                        neighbours, game.world.biome_tints));
        }
    }

    advance_sky(game);
    advance_weather(game);
}

// The columns extracted this tick, turned into corners about the camera the
// frame is drawn from.
void world_draw(Game& game) {
    auto& area = game.world;
    if (!area.weather_renderer) return;
    const double camera[3] = {game.camera.eye[0], game.camera.eye[1], game.camera.eye[2]};
    area.weather_renderer->set_frame(area.weather, camera);
}

void world_stop(Game& game) {
    game.world.streaming.store(false);
    game.world.mesher.join();
}

// Same bookkeeping as a change from the server, so a guess and a fact are
// drawn by the same path and a wrong guess is simply overwritten.
void world_predict_block(Game& game, int32_t x, int32_t y, int32_t z, int32_t state) {
    auto& area = game.world;
    const int32_t chunk_x = x >> 4;
    const int32_t chunk_z = z >> 4;
    std::set<std::pair<int32_t, int32_t>> relit;
    {
        std::lock_guard lock(area.world_guard);
        const auto found = area.world.find({chunk_x, chunk_z});
        if (found == area.world.end()) return;
        if (!found->second.set_block_state(x & 15, found->second.index_of(y), z & 15, state)) return;
        relit = area.light.relight(area.world, x, y, z);
    }
    std::lock_guard lock(area.dirty_guard);
    area.dirty.insert(relit.begin(), relit.end());
    area.dirty.insert({chunk_x, chunk_z});
    if ((x & 15) == 0) area.dirty.insert({chunk_x - 1, chunk_z});
    if ((x & 15) == 15) area.dirty.insert({chunk_x + 1, chunk_z});
    if ((z & 15) == 0) area.dirty.insert({chunk_x, chunk_z - 1});
    if ((z & 15) == 15) area.dirty.insert({chunk_x, chunk_z + 1});
}

} // namespace client

namespace client {

mcpp::world::WeatherState world_shot_weather(const mcpp::Session& session,
                                             const std::vector<mcpp::world::Chunk>& chunks,
                                             const mcpp::gpu::Camera& camera,
                                             std::string_view options) {
    float rain = session.weather().rain_level;
    int32_t radius = 10;
    bool snow = false;
    int32_t ticks = 0;
    while (!options.empty()) {
        const size_t comma = options.find(',');
        const std::string_view option = options.substr(0, comma);
        options = comma == std::string_view::npos ? std::string_view{} : options.substr(comma + 1);
        const size_t equals = option.find('=');
        if (equals == std::string_view::npos) continue;
        const std::string key(option.substr(0, equals));
        const std::string value(option.substr(equals + 1));
        if (key == "rain") rain = std::stof(value);
        if (key == "snow") snow = std::stof(value) > 0.0f;
        if (key == "wr") radius = std::stoi(value);
        if (key == "wt") ticks = std::stoi(value);
    }
    mcpp::world::WeatherState state;
    if (rain <= 0.0f) return state;

    const auto find_chunk = [&](int32_t cx, int32_t cz) -> const mcpp::world::Chunk* {
        for (const auto& chunk : chunks) {
            if (chunk.x == cx && chunk.z == cz) return &chunk;
        }
        return nullptr;
    };
    const int32_t sea_level = session.dimension().sea_level;
    mcpp::world::WeatherLevel level;
    level.min_y = session.dimension().min_y;
    level.height = [&](int32_t x, int32_t z) {
        const auto* chunk = find_chunk(x >> 4, z >> 4);
        return chunk == nullptr ? level.min_y : chunk->motion_blocking_at(x & 15, z & 15);
    };
    level.precipitation = [&](int32_t x, int32_t y, int32_t z) {
        const auto* chunk = find_chunk(x >> 4, z >> 4);
        if (chunk == nullptr) return mcpp::world::Precipitation::None;
        if (snow) return mcpp::world::Precipitation::Snow;
        const int32_t rows = static_cast<int32_t>(chunk->sections.size()) * 16;
        const int32_t row = std::clamp(chunk->index_of(y), 0, rows - 1);
        const auto* biome = session.biomes().at(
            static_cast<size_t>(std::max(chunk->biome_at(x & 15, row, z & 15), 0)));
        if (biome == nullptr) return mcpp::world::Precipitation::None;
        return mcpp::world::precipitation_at(biome->has_precipitation, biome->temperature, y, sea_level);
    };
    level.light = [&](int32_t x, int32_t y, int32_t z) {
        const auto* chunk = find_chunk(x >> 4, z >> 4);
        if (chunk == nullptr) return 0;
        const int32_t index = chunk->index_of(y);
        const int32_t rows = static_cast<int32_t>(chunk->sections.size()) * 16;
        if (index < 0 || index >= rows) return mcpp::world::light_pack(0, 15);
        return mcpp::world::light_pack(chunk->block_light_at(x & 15, index, z & 15),
                                       chunk->sky_light_at(x & 15, index, z & 15));
    };
    const double eye[3] = {camera.eye[0], camera.eye[1], camera.eye[2]};
    mcpp::world::extract_weather(level, ticks, 0.0f, eye, rain, std::clamp(radius, 2, 15), state);
    return state;
}

mcpp::world::Atmosphere world_shot_atmosphere(const mcpp::Session& session,
                                             const std::vector<mcpp::world::Chunk>& chunks,
                                             const mcpp::gpu::Camera& camera,
                                             std::string_view options) {
    int64_t day_time = session.time().day_time;
    int32_t render_distance = 12;
    mcpp::Weather weather = session.weather();
    while (!options.empty()) {
        const size_t comma = options.find(',');
        const std::string_view option = options.substr(0, comma);
        options = comma == std::string_view::npos ? std::string_view{} : options.substr(comma + 1);
        const size_t equals = option.find('=');
        if (equals == std::string_view::npos) continue;
        const std::string key(option.substr(0, equals));
        const std::string value(option.substr(equals + 1));
        if (key == "t") day_time = std::stoll(value);
        if (key == "rd") render_distance = std::stoi(value);
        if (key == "rain") weather.rain_level = std::stof(value);
        if (key == "thunder") weather.thunder_level = std::stof(value);
    }
    const auto find_chunk = [&](int32_t cx, int32_t cz) -> const mcpp::world::Chunk* {
        for (const auto& chunk : chunks) {
            if (chunk.x == cx && chunk.z == cz) return &chunk;
        }
        return nullptr;
    };
    const double eye[3] = {camera.eye[0], camera.eye[1], camera.eye[2]};
    mcpp::world::AttributeProbe probe;
    probe.reset(attributes_at(session, day_time, eye, find_chunk, weather));
    auto in = inputs_for(session, camera, render_distance, weather);
    static const mcpp::world::BlockTable no_table;
    static const mcpp::world::RenderTable render = [] {
        mcpp::world::RenderTable loaded;
        loaded.open(mcpp::world::RenderTable::default_path(mcpp::protocol::kTargetProtocol));
        return loaded;
    }();
    const auto block_at = [&](int32_t x, int32_t y, int32_t z) -> int32_t {
        const auto* chunk = find_chunk(x >> 4, z >> 4);
        if (chunk == nullptr) return 0;
        const int32_t index = chunk->index_of(y);
        if (index < 0 || index >= static_cast<int32_t>(chunk->sections.size()) * 16) return 0;
        return chunk->block_state(x & 15, index, z & 15);
    };
    in.medium = medium_at(camera.eye, block_at, render, no_table);
    // A picture is taken with the eyes long since used to the water.
    in.water_vision = 1.0f;
    return mcpp::world::atmosphere(probe, 0.0f, in);
}

} // namespace client
