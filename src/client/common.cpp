#include "client/common.hpp"

#include "mcpp/world/biomes.hpp"
#include "mcpp/session.hpp"
#include "mcpp/protocol_versions.hpp"

#include "mcpp/translations.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <format>
#include <iostream>
#include <map>
#include <memory>
#include <thread>
#include <utility>

namespace client {
namespace {

// Runs body(i) for every i in [0, count) across the machine's threads. Meshing
// is the one place where this client is allowed to be greedy with cores: it
// happens off the frame, and every chunk is independent of every other.
template <typename Body>
void parallel_for(size_t count, Body&& body) {
    const size_t hardware = std::max<size_t>(1, std::thread::hardware_concurrency());
    const size_t workers = std::min(hardware, count);
    if (workers <= 1) {
        for (size_t i = 0; i < count; ++i) body(i);
        return;
    }

    std::atomic<size_t> next{0};
    std::vector<std::thread> threads;
    threads.reserve(workers);
    for (size_t w = 0; w < workers; ++w) {
        threads.emplace_back([&] {
            for (size_t i = next.fetch_add(1, std::memory_order_relaxed); i < count;
                 i = next.fetch_add(1, std::memory_order_relaxed)) {
                body(i);
            }
        });
    }
    for (auto& thread : threads) thread.join();
}

} // namespace

mcpp::auth::Profile signed_in_profile(std::string& username) {
    mcpp::auth::Session account;
    if (const auto profile = account.restore()) {
        username = profile->name;
        std::cout << std::format("- signed in as {}\n", profile->name);
        return *profile;
    }
    if (account.last_failure() != mcpp::auth::Failure::None) {
        std::cout << std::format("- not signed in: {}\n", account.last_message());
    }
    return {};
}

std::string item_display_name(std::string_view name) {
    static const mcpp::Translations language = [] {
        mcpp::Translations loaded;
        loaded.load(mcpp::Translations::default_path());
        return loaded;
    }();
    for (const char* kind : {"item", "block"}) {
        if (const auto* text = language.find(std::format("{}.minecraft.{}", kind, name))) {
            return *text;
        }
    }
    std::string out;
    bool start = true;
    for (const char c : name) {
        if (c == '_') {
            out += ' ';
            start = true;
            continue;
        }
        out += start ? static_cast<char>(std::toupper(c)) : c;
        start = false;
    }
    return out;
}

bool aimed_block(const float eye[3], const float direction[3], float reach,
                 const mcpp::world::BlockAt& at, const mcpp::world::BlockTable& table,
                 int32_t out[3]) {
    int32_t cell[3]{static_cast<int32_t>(std::floor(eye[0])),
                    static_cast<int32_t>(std::floor(eye[1])),
                    static_cast<int32_t>(std::floor(eye[2]))};
    int32_t step[3]{};
    float next[3]{};
    float delta[3]{};
    for (int i = 0; i < 3; ++i) {
        step[i] = direction[i] > 0.0f ? 1 : -1;
        delta[i] = direction[i] != 0.0f ? std::abs(1.0f / direction[i]) : 1e30f;
        const float edge = direction[i] > 0.0f ? static_cast<float>(cell[i] + 1)
                                               : static_cast<float>(cell[i]);
        next[i] = direction[i] != 0.0f ? (edge - eye[i]) / direction[i] : 1e30f;
    }
    for (float travelled = 0.0f; travelled <= reach;) {
        const int32_t state = at(cell[0], cell[1], cell[2]);
        if (state > 0 && !table.fluid(state)) {
            out[0] = cell[0];
            out[1] = cell[1];
            out[2] = cell[2];
            return true;
        }
        const int axis = next[0] < next[1] ? (next[0] < next[2] ? 0 : 2)
                                           : (next[1] < next[2] ? 1 : 2);
        travelled = next[axis];
        cell[axis] += step[axis];
        next[axis] += delta[axis];
    }
    return false;
}

Address parse_address(const std::string& target) {
    Address address{target, 25565};
    if (auto colon = target.rfind(':'); colon != std::string::npos) {
        address.host = target.substr(0, colon);
        address.port = static_cast<uint16_t>(std::stoi(target.substr(colon + 1)));
    }
    return address;
}

float ground_near_centre(const std::vector<mcpp::world::Chunk>& chunks,
                         const mcpp::world::BlockTable& table, float centre_x, float centre_z) {
    float ground = 64.0f;
    for (const auto& chunk : chunks) {
        if (std::abs((chunk.x + 0.5f) * 16.0f - centre_x) > 48.0f) continue;
        if (std::abs((chunk.z + 0.5f) * 16.0f - centre_z) > 48.0f) continue;
        for (int32_t lz = 0; lz < 16; lz += 4) {
            for (int32_t lx = 0; lx < 16; lx += 4) {
                for (int32_t y = static_cast<int32_t>(chunk.sections.size()) * 16 - 1; y >= 0; --y) {
                    const int32_t state = chunk.block_state(lx, y, lz);
                    if (state != 0 && table.full_cube(state)) {
                        ground = std::max(ground, static_cast<float>(y + chunk.min_y));
                        break;
                    }
                }
            }
        }
    }
    return ground;
}

mcpp::world::BiomeTints biome_tints_of(const mcpp::Session& session) {
    mcpp::world::BiomeTints tints;
    if (session.biomes().empty()) return tints;
    mcpp::world::Colormaps maps;
    maps.open(mcpp::world::Colormaps::default_path(mcpp::protocol::kTargetProtocol));
    tints.colour = session.biomes().tints(maps);
    // A shot takes the radius from the environment rather than from
    // options.txt, so one scene can be rendered at several of them.
    if (const char* radius = std::getenv("PLASMA_BIOME_BLEND")) {
        tints.blend_radius = std::atoi(radius);
    }
    return tints;
}

std::vector<mcpp::world::SectionMesh> mesh_world(std::span<const mcpp::world::Chunk> chunks,
                                                 const mcpp::world::BlockTable& table,
                                                 double* milliseconds,
                                                 double* unpack_milliseconds,
                                                 const mcpp::world::BiomeTints& tints) {
    const auto start = std::chrono::steady_clock::now();

    // Unpack first, in parallel. The map is only built once every entry exists,
    // so the meshing pass can read it from every thread without a lock.
    std::vector<std::unique_ptr<mcpp::world::ChunkBlocks>> unpacked(chunks.size());
    parallel_for(chunks.size(), [&](size_t i) {
        unpacked[i] = std::make_unique<mcpp::world::ChunkBlocks>(chunks[i]);
    });

    if (unpack_milliseconds != nullptr) {
        *unpack_milliseconds =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count();
    }

    std::map<std::pair<int32_t, int32_t>, const mcpp::world::ChunkBlocks*> by_position;
    for (size_t i = 0; i < chunks.size(); ++i) {
        by_position.emplace(std::pair{chunks[i].x, chunks[i].z}, unpacked[i].get());
    }
    const auto find_blocks = [&](int32_t x, int32_t z) -> const mcpp::world::ChunkBlocks* {
        auto it = by_position.find({x, z});
        return it == by_position.end() ? nullptr : it->second;
    };

    std::vector<std::vector<mcpp::world::SectionMesh>> per_chunk(chunks.size());
    parallel_for(chunks.size(), [&](size_t i) {
        const auto& chunk = chunks[i];
        const mcpp::world::Neighbours neighbours{
            .west = find_blocks(chunk.x - 1, chunk.z),
            .east = find_blocks(chunk.x + 1, chunk.z),
            .north = find_blocks(chunk.x, chunk.z - 1),
            .south = find_blocks(chunk.x, chunk.z + 1),
            .north_west = find_blocks(chunk.x - 1, chunk.z - 1),
            .north_east = find_blocks(chunk.x + 1, chunk.z - 1),
            .south_west = find_blocks(chunk.x - 1, chunk.z + 1),
            .south_east = find_blocks(chunk.x + 1, chunk.z + 1),
        };
        per_chunk[i] = mcpp::world::mesh_sections(chunk, *unpacked[i], table, neighbours, tints);
    });

    size_t total = 0;
    for (const auto& pieces : per_chunk) total += pieces.size();

    std::vector<mcpp::world::SectionMesh> all;
    all.reserve(total);
    for (auto& pieces : per_chunk) {
        all.insert(all.end(), std::make_move_iterator(pieces.begin()),
                   std::make_move_iterator(pieces.end()));
    }

    if (milliseconds) {
        *milliseconds = std::chrono::duration<double, std::milli>(
                            std::chrono::steady_clock::now() - start).count();
    }
    return all;
}

GeometryBudget geometry_budget(uint64_t device_local_free, int32_t render_distance) {
    constexpr size_t kBytesPerChunk = 620u << 10;
    // What the rest of the frame holds on the device: the colour and depth
    // targets, the atlas and its mipmaps, the entity and interface atlases,
    // the staging buffers, and whatever the driver keeps for itself.
    constexpr uint64_t kHeadroom = 1024ull << 20;
    constexpr uint64_t kCeiling = 6144ull << 20;
    // What the client reserved before it asked the device, and what it falls
    // back to when the device has nothing to spare.
    constexpr size_t kFloorVertices = 384u << 20;
    constexpr size_t kFloorIndices = 160u << 20;

    GeometryBudget budget;
    budget.device_free = device_local_free;
    const auto across = static_cast<size_t>(2 * std::max(render_distance, 2) + 1);
    budget.chunks_wanted = across * across;

    const uint64_t room = device_local_free > kHeadroom ? device_local_free - kHeadroom : 0;
    const uint64_t allowed = std::min<uint64_t>(room, kCeiling);
    const uint64_t wanted = static_cast<uint64_t>(budget.chunks_wanted) * kBytesPerChunk;
    const uint64_t total = std::min(allowed, wanted);
    budget.whole_distance = total >= wanted;

    // Measured on real terrain the vertex and index data come out around
    // 2.7 to 1, so an even split would run out of indices long before it ran
    // out of room.
    budget.vertex_bytes = static_cast<size_t>(total * 73 / 100);
    budget.index_bytes = static_cast<size_t>(total - budget.vertex_bytes);
    if (budget.vertex_bytes < kFloorVertices) {
        budget.vertex_bytes = kFloorVertices;
        budget.index_bytes = kFloorIndices;
    }
    // Vulkan wants both buffer sizes a whole number of their element size.
    budget.vertex_bytes &= ~size_t{31};
    budget.index_bytes &= ~size_t{31};
    budget.whole_distance =
        static_cast<uint64_t>(budget.vertex_bytes + budget.index_bytes) >= wanted;
    return budget;
}

} // namespace client
