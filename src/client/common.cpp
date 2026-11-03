#include "client/common.hpp"

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

std::vector<mcpp::world::SectionMesh> mesh_world(const std::vector<mcpp::world::Chunk>& chunks,
                                                 const mcpp::world::BlockTable& table,
                                                 double* milliseconds) {
    const auto start = std::chrono::steady_clock::now();

    // Unpack first, in parallel. The map is only built once every entry exists,
    // so the meshing pass can read it from every thread without a lock.
    std::vector<std::unique_ptr<mcpp::world::ChunkBlocks>> unpacked(chunks.size());
    parallel_for(chunks.size(), [&](size_t i) {
        unpacked[i] = std::make_unique<mcpp::world::ChunkBlocks>(chunks[i]);
    });

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
        };
        per_chunk[i] = mcpp::world::mesh_sections(chunk, *unpacked[i], table, neighbours);
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

} // namespace client
