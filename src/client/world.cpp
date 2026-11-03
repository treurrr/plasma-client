#include "client/game.hpp"

#include "mcpp/protocol_versions.hpp"
#include "mcpp/world/biomes.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <iostream>
#include <iterator>

namespace client {
namespace {

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
            if (found == area.world.end()) return;
            mcpp::world::read_light_into(in, found->second);
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

void world_load(Game& game) {
    auto& area = game.world;
    double mesh_ms = 0.0;
    area.first_meshes = mesh_world(area.chunks, game.table, &mesh_ms);
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
        game.world.sky.emplace(game.device, renderer.colour_format(), sky_path);
        renderer.set_sky(&*game.world.sky);
    }
    if (!game.session->biomes().empty()) {
        mcpp::world::Colormaps maps;
        maps.open(mcpp::world::Colormaps::default_path(mcpp::protocol::kTargetProtocol));
        renderer.set_biome_colours(game.session->biomes().tints(maps));
    }
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
            auto pieces = mesh_world(arrived, game.table, &batch_ms);
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
            renderer.replace(
                mcpp::world::mesh_sections(found->second, *blocks, game.table, neighbours));
        }
    }

    // The sky is the standing biome's, so walking from a plains into a
    // taiga changes the colour overhead the way it does in the game.
    if (game.tick_due && !session.biomes().empty()) {
        const float* eye = game.player.eye;
        const int32_t cx = static_cast<int32_t>(std::floor(eye[0] / 16.0));
        const int32_t cz = static_cast<int32_t>(std::floor(eye[2] / 16.0));
        std::lock_guard lock(area.world_guard);
        if (const auto found = area.world.find({cx, cz}); found != area.world.end()) {
            const int32_t biome = found->second.biome_at(
                static_cast<int32_t>(std::floor(eye[0])) & 15,
                found->second.index_of(static_cast<int32_t>(std::floor(eye[1]))),
                static_cast<int32_t>(std::floor(eye[2])) & 15);
            renderer.set_sky_colours(
                session.biomes().sky(static_cast<size_t>(std::max(biome, 0))), 0xC0D8FFu);
        }
    }
}

void world_draw(Game&) {}

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
