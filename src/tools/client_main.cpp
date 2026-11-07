#include "client/blockshot.hpp"
#include "client/outlineshot.hpp"
#include "client/chattest.hpp"
#include "client/crafttest.hpp"
#include "client/common.hpp"
#include "client/entityshot.hpp"
#include "client/inspect.hpp"
#include "client/perfreport.hpp"
#include "client/screenshot.hpp"
#include "client/hud_shot.hpp"
#ifdef MCPP_HAVE_WINDOW
#include "client/game.hpp"
#include "client/player_rules.hpp"
#include "client/game_mode.hpp"
#include "client/breakshot.hpp"
#include "client/handshot.hpp"
#include "client/itemshot.hpp"
#endif

#include "mcpp/auth.hpp"
#include "mcpp/features.hpp"
#include "mcpp/gpu/ui.hpp"
#include "mcpp/ui/menu.hpp"
#include "mcpp/json.hpp"
#include "mcpp/packet_io.hpp"
#include "mcpp/protocol_versions.hpp"
#include "mcpp/session.hpp"
#include "mcpp/status.hpp"
#include "mcpp/gpu/device.hpp"
#include "mcpp/gpu/offscreen.hpp"
#include "mcpp/gpu/outline.hpp"
#include "mcpp/gpu/sky.hpp"
#include "mcpp/gpu/world_renderer.hpp"
#ifdef MCPP_HAVE_WINDOW
#include "mcpp/gpu/text.hpp"
#include "mcpp/gpu/window.hpp"
#endif
#include "mcpp/image_png.hpp"
#include "mcpp/world/atlas.hpp"
#include "mcpp/gpu/weather.hpp"
#include "mcpp/world/biomes.hpp"
#include "mcpp/translations.hpp"
#include "mcpp/world/light.hpp"
#include "mcpp/world/block_table.hpp"
#include "mcpp/world/mesher.hpp"
#include "mcpp/world/physics.hpp"

#include <atomic>
#include <chrono>
#include <numbers>
#include <algorithm>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <iostream>
#include <mutex>
#include <optional>
#include <numeric>
#include <algorithm>
#include <map>
#include <ranges>
#include <set>
#include <utility>
#include <vector>
#include <string>
#include <thread>

namespace {

using client::Address;
using client::aimed_block;
using client::ground_near_centre;
using client::item_display_name;
using client::mesh_world;
using client::parse_address;
using client::signed_in_profile;

int do_login() {
    mcpp::auth::Session account;
    if (const auto existing = account.restore()) {
        std::cout << std::format("already signed in as {} ({})\n", existing->name,
                                 existing->uuid);
        return 0;
    }
    const auto profile = account.sign_in([](const mcpp::auth::DeviceCode& code) {
        std::cout << std::format("open {} and enter the code {}\n", code.verification_uri,
                                 code.user_code);
        std::cout.flush();
    });
    if (!profile) {
        std::cerr << std::format("sign-in failed: {}\n", account.last_message().empty()
                                     ? mcpp::auth::describe(account.last_failure())
                                     : account.last_message());
        return 1;
    }
    std::cout << std::format("signed in as {} ({})\n", profile->name, profile->uuid);
    return 0;
}

int do_logout() {
    mcpp::auth::Session account;
    account.sign_out();
    std::cout << "signed out\n";
    return 0;
}



void print_banner() {
    std::cout << std::format("Plasma Client 0.1.0  ·  target version {} (protocol {})\n",
                             mcpp::protocol::kTargetVersionName,
                             mcpp::protocol::kTargetProtocol);
}

void print_versions() {
    std::cout << "\nversions:\n";
    for (const auto& version : mcpp::protocol::all_versions()) {
        std::cout << std::format("  {:<9} protocol {:<4} {}\n", version.name, version.protocol,
                                 version.supported ? "supported" : "planned");
    }
}

void print_features() {
    auto& registry = mcpp::features::Registry::instance();
    registry.load(mcpp::features::Registry::default_config_path());

    print_banner();
    std::cout << std::format("\nconfig: {}\n", mcpp::features::Registry::default_config_path().string());

    auto current = mcpp::features::Category::Hud;
    bool first = true;
    for (size_t i = 0; i < registry.all().size(); ++i) {
        const auto& feature = registry.all()[i];
        if (first || feature.category != current) {
            std::cout << std::format("\n  {}\n", mcpp::features::category_name(feature.category));
            current = feature.category;
            first = false;
        }
        const bool on = registry.enabled(static_cast<mcpp::features::Id>(i));
        std::cout << std::format("    [{}] {:<24}{}\n", on ? 'x' : ' ', feature.label,
                                 feature.server_may_object ? "  some servers forbid this" : "");
    }
    std::cout << "\nNo reach, aim assist, auto-clicker or ESP. That stays that way.\n";
}



// Joins a server, keeps chunks until it has enough, and leaves again.
std::vector<mcpp::world::Chunk> load_chunks(const Address& address, size_t limit, bool quiet) {
    std::mutex guard;
    std::vector<mcpp::world::Chunk> chunks;
    std::atomic<bool> enough{false};

    mcpp::SessionOptions options;
    options.host = address.host;
    options.port = address.port;
    std::string username = "PlasmaShot";
    options.profile = signed_in_profile(username);
    options.username = username;

    mcpp::SessionEvents events;
    if (!quiet) {
        events.on_status = [](std::string_view line) { std::cout << "- " << line << '\n'; };
    }
    events.world.on_chunk = [&](const mcpp::world::Chunk& chunk) {
        std::lock_guard lock(guard);
        if (chunks.size() < limit) chunks.push_back(chunk);
        if (chunks.size() >= limit) enough.store(true);
    };

    mcpp::Session session(options, events);
    session.connect();
    while (!enough.load()) {
        try {
            session.pump();
        } catch (const mcpp::TimeoutError&) {
            break;
        }
    }
    session.close();
    return chunks;
}



// Totals across a set of section meshes, for reporting.
struct MeshTotals {
    size_t triangles = 0;
    size_t bytes = 0;
};

MeshTotals totals_of(const std::vector<mcpp::world::SectionMesh>& sections) {
    MeshTotals totals;
    for (const auto& section : sections) {
        totals.triangles += section.triangles();
        totals.bytes += section.bytes();
    }
    return totals;
}

// Loads the same chunks, draws them, forgets them, and loads them again, many
// times over, in a buffer too small to hold more than a few rounds without
// handing room back. A client that walks around for an hour does exactly
// this; the buffer has to come out of it no fuller than it went in.
int do_churn(const std::string& target, size_t chunk_limit, int rounds) {
    mcpp::world::BlockTable table;
    mcpp::world::Atlas atlas;
    const auto protocol = mcpp::protocol::kTargetProtocol;
    if (!table.open(mcpp::world::BlockTable::default_path(protocol)) ||
        !atlas.open(mcpp::world::Atlas::default_path(protocol))) {
        std::cerr << "block table or atlas missing\n";
        return 1;
    }
    const auto chunks = load_chunks(parse_address(target), chunk_limit, true);
    if (chunks.empty()) return 1;
    double mesh_ms = 0.0;
    const auto pieces = mesh_world(chunks, table, &mesh_ms);
    size_t vertex_bytes = 0;
    size_t index_bytes = 0;
    for (const auto& piece : pieces) {
        vertex_bytes += piece.vertices.size() * sizeof(mcpp::world::Vertex);
        index_bytes += piece.indices.size() * sizeof(uint32_t);
    }

    mcpp::gpu::Device device;
    mcpp::gpu::Device::Options options;
    device.create(options);
    mcpp::gpu::WorldRenderer renderer(device, 320, 180, atlas);
    // Room for two and a half copies of the world: enough to churn through if
    // room comes back, nowhere near enough for the rounds if it does not.
    renderer.reserve(vertex_bytes * 5 / 2, index_bytes * 5 / 2);

    mcpp::gpu::Camera camera{};
    camera.eye[1] = 120.0f;
    camera.target[0] = 1.0f;
    camera.target[1] = 100.0f;

    std::cout << std::format("- {} chunks, {:.1f} MB of geometry, buffer {:.1f} MB\n", chunks.size(),
                             (vertex_bytes + index_bytes) / 1e6,
                             (vertex_bytes + index_bytes) * 2.5 / 1e6);
    // Every other round forgets a different, scattered half and loads it back,
    // which is what walking does: chunks leave from between ones that stay,
    // and the room they leave is in pieces, not at the end.
    if (!renderer.append(pieces)) {
        std::cout << "- the first load does not fit\n";
        return 1;
    }
    for (int round = 0; round < rounds; ++round) {
        const int parity = round % 2;
        std::vector<mcpp::world::SectionMesh> half;
        for (const auto& chunk : chunks) {
            if (((chunk.x + chunk.z) & 1) != parity) continue;
            renderer.forget(chunk.x, chunk.z);
        }
        for (int frame = 0; frame < 4; ++frame) renderer.draw(camera);
        for (const auto& piece : pieces) {
            if ((((piece.origin[0] >> 4) + (piece.origin[2] >> 4)) & 1) == parity) {
                half.push_back(piece);
            }
        }
        // Back in a different order from the one they left in, so a big
        // section meets a small hole first: that is how room really splinters.
        if (round % 3 != 0) std::ranges::reverse(half);
        if (round % 3 == 2) std::ranges::rotate(half, half.begin() + half.size() / 3);
        if (!renderer.append(half)) {
            std::cout << std::format("- round {}: out of room\n", round);
            return 1;
        }
        for (int frame = 0; frame < 4; ++frame) renderer.draw(camera);
        if (round % 10 == 0 || round == rounds - 1) {
            const auto& room = renderer.vertex_room();
            std::cout << std::format(
                "- round {}: free room in {} piece(s), largest {:.0f}% of the buffer\n", round,
                room.spans(), 100.0 * static_cast<double>(room.largest_free()) /
                                  static_cast<double>(room.capacity()));
        }
    }
    std::cout << std::format("- {} rounds of load, draw, forget with nothing lost\n", rounds);
    return 0;
}

// Records a world to a file: every packet from configuration on, until the
// given number of chunks has arrived, plus a moment more so the light and the
// entities that follow them are in it too.
int do_record(const std::string& target, const std::string& file, size_t chunk_limit,
              const std::string& as) {
    const auto address = parse_address(target);
    mcpp::SessionOptions options;
    options.host = address.host;
    options.port = address.port;
    std::string username = as;
    options.profile = signed_in_profile(username);
    options.username = username;
    options.record = file;

    size_t chunks = 0;
    mcpp::SessionEvents events;
    events.on_status = [](std::string_view line) { std::cout << "- " << line << '\n'; };
    events.world.on_chunk = [&](const mcpp::world::Chunk&) { ++chunks; };
    mcpp::Session session(options, events);
    session.connect();
    std::optional<std::chrono::steady_clock::time_point> enough;
    while (true) {
        if (chunks >= chunk_limit && !enough) enough = std::chrono::steady_clock::now();
        if (enough && std::chrono::steady_clock::now() - *enough > std::chrono::seconds(2)) break;
        try {
            if (!session.pump()) break;
        } catch (const mcpp::TimeoutError&) {
            break;
        }
    }
    session.close();
    std::cout << std::format("- {} chunks recorded to {} ({:.1f} MB)\n", chunks, file,
                             static_cast<double>(std::filesystem::file_size(file)) / 1e6);
    return chunks == 0 ? 1 : 0;
}

int do_bench(const std::string& target, size_t chunk_limit, size_t frames, uint32_t width,
             uint32_t height) {

    mcpp::world::BlockTable table;
    mcpp::world::Atlas atlas;
    const auto protocol = mcpp::protocol::kTargetProtocol;
    if (!table.open(mcpp::world::BlockTable::default_path(protocol)) ||
        !atlas.open(mcpp::world::Atlas::default_path(protocol))) {
        std::cerr << "block table or atlas missing\n";
        return 1;
    }

    const auto chunks = load_chunks(parse_address(target), chunk_limit, true);
    if (chunks.empty()) {
        std::cerr << "no chunks received\n";
        return 1;
    }

    double mesh_ms = 0.0;
    const auto mesh = mesh_world(chunks, table, &mesh_ms);

    int32_t min_cx = chunks.front().x;
    int32_t max_cx = min_cx;
    int32_t min_cz = chunks.front().z;
    int32_t max_cz = min_cz;
    for (const auto& chunk : chunks) {
        min_cx = std::min(min_cx, chunk.x);
        max_cx = std::max(max_cx, chunk.x);
        min_cz = std::min(min_cz, chunk.z);
        max_cz = std::max(max_cz, chunk.z);
    }
    const float centre_x = (min_cx + max_cx + 1) * 16.0f / 2.0f;
    const float centre_z = (min_cz + max_cz + 1) * 16.0f / 2.0f;
    const float ground = ground_near_centre(chunks, table, centre_x, centre_z);

    mcpp::gpu::Device device;
    mcpp::gpu::Device::Options options;
    device.create(options);

    mcpp::gpu::WorldRenderer renderer(device, width, height, atlas);
    renderer.upload(mesh);

    std::vector<double> gpu_times;
    std::vector<double> cpu_times;
    gpu_times.reserve(frames);
    cpu_times.reserve(frames);

    // A turning camera, so the measurement is not one lucky viewpoint.
    for (size_t frame = 0; frame < frames; ++frame) {
        const double angle = 6.2831853 * static_cast<double>(frame) / static_cast<double>(frames);
        mcpp::gpu::Camera camera{};
        camera.eye[0] = centre_x + 40.0f * static_cast<float>(std::cos(angle));
        camera.eye[1] = ground + 18.0f;
        camera.eye[2] = centre_z + 40.0f * static_cast<float>(std::sin(angle));
        camera.target[0] = centre_x;
        camera.target[1] = ground;
        camera.target[2] = centre_z;

        const auto started = std::chrono::steady_clock::now();
        const double gpu_ms = renderer.draw(camera);
        const double wall_ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
                .count();
        if (frame >= 10) {   // let the driver settle first
            gpu_times.push_back(gpu_ms);
            cpu_times.push_back(wall_ms - gpu_ms);
        }
    }

    {
        auto sorted = cpu_times;
        std::ranges::sort(sorted);
        const double total = std::accumulate(sorted.begin(), sorted.end(), 0.0);
        std::cout << std::format("  cpu beside the gpu: mean {:.3f} ms, median {:.3f} ms\n",
                                 total / static_cast<double>(sorted.size()),
                                 sorted[sorted.size() / 2]);
    }
    std::ranges::sort(gpu_times);
    const auto count = gpu_times.size();
    const auto mean_of_slowest = [&](double fraction) {
        const size_t take = std::max<size_t>(1, static_cast<size_t>(
                                                    fraction * static_cast<double>(count)));
        const double total = std::accumulate(gpu_times.end() - static_cast<long>(take),
                                             gpu_times.end(), 0.0);
        return total / static_cast<double>(take);
    };
    const double average =
        std::accumulate(gpu_times.begin(), gpu_times.end(), 0.0) / static_cast<double>(count);
    const double median = gpu_times[count / 2];

    const auto as_fps = [](double ms) { return ms > 0 ? 1000.0 / ms : 0.0; };

    const auto totals = totals_of(mesh);
    std::cout << std::format("{}x{}, {} chunks, {} sections, {} triangles, {:.1f} MB vertex data\n",
                             width, height, chunks.size(), renderer.section_count(),
                             totals.triangles, totals.bytes / 1e6);
    std::cout << std::format("in frame: {} sections, {} triangles ({:.0f}% skipped)\n",
                             renderer.drawn_sections(), renderer.drawn_triangles(),
                             totals.triangles > 0
                                 ? 100.0 * (1.0 - static_cast<double>(renderer.drawn_triangles()) /
                                                      static_cast<double>(totals.triangles))
                                 : 0.0);
    std::cout << std::format("gpu: {}\n", device.adapter().name);
    std::cout << std::format("meshing: {:.0f} ms ({:.2f} ms per chunk)\n", mesh_ms,
                             mesh_ms / static_cast<double>(chunks.size()));
    std::cout << std::format("upload: {:.0f} ms  (allocate {:.0f}, memcpy {:.0f}, gpu copy {:.0f})\n\n",
                             renderer.upload_ms(), renderer.upload_alloc_ms(),
                             renderer.upload_copy_ms(), renderer.upload_gpu_ms());
    std::cout << std::format("{} frames measured, first 10 discarded\n", count);
    std::cout << std::format("  mean        {:7.3f} ms   {:8.0f} fps\n", average, as_fps(average));
    std::cout << std::format("  median      {:7.3f} ms   {:8.0f} fps\n", median, as_fps(median));
    // A 1% low is the mean frame time of the slowest one percent of frames,
    // not the 99th percentile: one stutter should show up, and a percentile
    // hides how bad it was.
    std::cout << std::format("  1% low      {:7.3f} ms   {:8.0f} fps\n", mean_of_slowest(0.01),
                             as_fps(mean_of_slowest(0.01)));
    std::cout << std::format("  0.1% low    {:7.3f} ms   {:8.0f} fps\n", mean_of_slowest(0.001),
                             as_fps(mean_of_slowest(0.001)));
    std::cout << std::format("  fastest     {:7.3f} ms\n  slowest     {:7.3f} ms\n",
                             gpu_times.front(), gpu_times.back());
    return 0;
}

int do_worldshot(const std::string& target, const std::string& out_path, size_t chunk_limit,
                 const std::string& view_argument, const std::string& overlay_kind) {
    // view@t=13000,rain=1,rd=12 sets the sky the shot is taken under.
    const std::string view = view_argument.substr(0, view_argument.find('@'));
    const std::string sky_options = view_argument.find('@') == std::string::npos
                                        ? std::string()
                                        : view_argument.substr(view_argument.find('@') + 1);
    const auto address = parse_address(target.starts_with("replay:") ? "localhost" : target);
    mcpp::world::BlockTable table;
    if (!table.open(mcpp::world::BlockTable::default_path(mcpp::protocol::kTargetProtocol))) {
        std::cerr << "no block table; bake one with tools/bake_block_table.py\n";
        return 1;
    }

    std::mutex guard;
    std::vector<mcpp::world::Chunk> chunks;
    std::atomic<bool> enough{false};

    mcpp::SessionOptions options;
    // replay:<file> draws a recorded world instead of asking a server, so a
    // picture taken today can be taken again next week and compared.
    if (target.starts_with("replay:")) {
        options.replay = target.substr(7);
    } else {
        options.host = address.host;
        options.port = address.port;
    }
    std::string username = overlay_kind == "paus" ? "PlasmaT" : "PlasmaShot";
    options.profile = signed_in_profile(username);
    options.username = username;

    mcpp::SessionEvents events;
    events.on_status = [](std::string_view line) { std::cout << "- " << line << '\n'; };
    events.world.on_chunk = [&](const mcpp::world::Chunk& chunk) {
    std::lock_guard lock(guard);
    if (chunks.size() < chunk_limit) chunks.push_back(chunk);
    if (chunks.size() >= chunk_limit) enough.store(true);
    };

    mcpp::Session session(options, events);
    session.connect();
    while (!enough.load()) {
        try {
            if (!session.pump()) break;
        } catch (const mcpp::TimeoutError&) {
            break;
        }
    }
    std::cout << std::format("- the server puts our latency at {} ms\n", session.ping_ms());
    session.close();
    std::cout << std::format("- {} chunks fetched\n", chunks.size());
    if (chunks.empty()) return 1;

    int32_t min_cx = chunks.front().x;
    int32_t max_cx = min_cx;
    int32_t min_cz = chunks.front().z;
    int32_t max_cz = min_cz;
    for (const auto& chunk : chunks) {
        min_cx = std::min(min_cx, chunk.x);
        max_cx = std::max(max_cx, chunk.x);
        min_cz = std::min(min_cz, chunk.z);
        max_cz = std::max(max_cz, chunk.z);
    }
    const float centre_x = (min_cx + max_cx + 1) * 16.0f / 2.0f;
    const float centre_z = (min_cz + max_cz + 1) * 16.0f / 2.0f;
    const float extent = std::max(max_cx - min_cx + 1, max_cz - min_cz + 1) * 16.0f;
    {
        const auto& first = chunks.front();
        std::string column;
        for (int32_t y = static_cast<int32_t>(first.sections.size()) * 16 - 1; y >= 0; --y) {
            const int32_t state = first.block_state(8, y, 8);
            if (state != 0) {
                column = std::format("highest block at (8,8) of chunk ({},{}): y={} state={} {}"
                                     ", sky light {} above it and {} ten blocks under it",
                                     first.x, first.z, y + first.min_y, state, table.block_name(state),
                                     first.sky_light_at(8, y + 1, 8),
                                     first.sky_light_at(8, y - 10, 8));
                break;
            }
        }
        std::cout << "- " << column << '\n';
        if (!session.biomes().empty()) {
            mcpp::world::Colormaps maps;
            maps.open(mcpp::world::Colormaps::default_path(mcpp::protocol::kTargetProtocol));
            std::map<std::string, int> present;
            for (const auto& chunk : chunks) {
                for (int32_t z = 2; z < 16; z += 6) {
                    for (int32_t x = 2; x < 16; x += 6) {
                        const int32_t id = chunk.biome_at(x, chunk.index_of(63), z);
                        if (const auto* biome = session.biomes().at(static_cast<size_t>(id))) {
                            ++present[std::format("{} #{:06x}", biome->name,
                                                  session.biomes().grass(
                                                      static_cast<size_t>(id), maps))];
                        }
                    }
                }
            }
            // Black is never a colour a biome asks for, so it only ever means a
            // field the registry sent in a shape we failed to read.
            for (size_t i = 0; i < session.biomes().size(); ++i) {
                if (session.biomes().grass(i, maps) == 0 ||
                    session.biomes().foliage(i, maps) == 0) {
                    std::cout << std::format("- warning: {} resolves to black\n",
                                             session.biomes().at(i)->name);
                }
            }
            std::cout << std::format("- {} biomes across the loaded chunks:\n", present.size());
            for (const auto& [name, count] : present) {
                std::cout << std::format("    {:4}x {}\n", count, name);
            }
            const int32_t id = first.biome_at(8, first.index_of(63), 8);
            if (const auto* biome = session.biomes().at(static_cast<size_t>(id))) {
                std::cout << std::format(
                    "- biome at the surface: {} ({}), temperature {:.2f}, downfall {:.2f}, "
                    "grass #{:06x}, foliage #{:06x}, water #{:06x}\n",
                    biome->name, id, biome->temperature, biome->downfall,
                    session.biomes().grass(static_cast<size_t>(id), maps),
                    session.biomes().foliage(static_cast<size_t>(id), maps),
                    session.biomes().water(static_cast<size_t>(id)));
            }
        }
        std::map<std::string, int> tops;
        int lowest = 1000;
        int highest = -1000;
        for (int32_t lz = 0; lz < 16; ++lz) {
            for (int32_t lx = 0; lx < 16; ++lx) {
                for (int32_t y = static_cast<int32_t>(first.sections.size()) * 16 - 1; y >= 0; --y) {
                    const int32_t state = first.block_state(lx, y, lz);
                    if (state == 0) continue;
                    ++tops[std::string(table.block_name(state))];
                    lowest = std::min(lowest, y + first.min_y);
                    highest = std::max(highest, y + first.min_y);
                    break;
                }
            }
        }
        std::cout << std::format("- top block per column, y {} to {}:\n", lowest, highest);
        for (const auto& [name, count] : tops) {
            std::cout << std::format("    {:>4}x  {}\n", count, name);
        }
    }
    std::cout << std::format("- chunks span x {}..{}, z {}..{} ({} blocks wide)\n",
                             min_cx * 16, (max_cx + 1) * 16, min_cz * 16, (max_cz + 1) * 16,
                             static_cast<int>(extent));

    double mesh_ms = 0.0;
    const auto combined = mesh_world(chunks, table, &mesh_ms, nullptr, client::biome_tints_of(session));

    mcpp::gpu::Device device;
    mcpp::gpu::Device::Options gpu_options;
    gpu_options.validation = true;
    device.create(gpu_options);

    mcpp::world::Atlas atlas;
    if (!atlas.open(mcpp::world::Atlas::default_path(mcpp::protocol::kTargetProtocol))) {
        std::cerr << "no atlas; bake one with tools/bake_atlas.py\n";
        return 1;
    }

    const bool full_hd = view == "macro" || view.starts_with("at:");
    const uint32_t shot_width = full_hd ? 1920u : 1600u;
    const uint32_t shot_height = full_hd ? 1080u : 900u;
    mcpp::gpu::WorldRenderer renderer(device, shot_width, shot_height, atlas);
    renderer.upload(combined);

    std::optional<mcpp::gpu::Sky> sky;
    const auto sky_path = mcpp::gpu::Sky::default_path(mcpp::protocol::kTargetProtocol);
    if (std::filesystem::exists(sky_path)) {
        sky.emplace(device, renderer.colour_format(), renderer.depth_format(), sky_path);
        renderer.set_sky(&*sky);
    } else {
        std::cout << "- no sky textures; bake them with tools/bake_sky.py\n";
    }

    std::optional<mcpp::gpu::Outline> outline;
    if (overlay_kind != "none") {
        outline.emplace(device, renderer.colour_format());
        renderer.set_outline(&*outline);
    }

    mcpp::gpu::Camera camera{};
    // at:x,y,z,tx,ty,tz puts the eye and the point it looks at exactly, for a
    // picture of one thing rather than of wherever the chunks happened to land.
    if (view.starts_with("at:")) {
        float values[6]{};
        size_t at = 3;
        for (float& value : values) {
            const size_t comma = view.find(',', at);
            value = std::stof(view.substr(at, comma - at));
            at = comma == std::string::npos ? view.size() : comma + 1;
        }
        for (int axis = 0; axis < 3; ++axis) {
            camera.eye[axis] = values[axis];
            camera.target[axis] = values[3 + axis];
        }
        camera.fov_degrees = 70.0f;
    } else if (view == "macro") {
        const float ground = ground_near_centre(chunks, table, centre_x, centre_z);
        camera.eye[0] = centre_x;
        camera.eye[1] = ground + 1.5f;
        camera.eye[2] = centre_z + 9.0f;
        camera.target[0] = centre_x;
        camera.target[1] = ground + 0.5f;
        camera.target[2] = centre_z;
        camera.fov_degrees = 50.0f;
    } else if (view == "close") {
        camera.eye[0] = centre_x + 6.0f;
        camera.eye[1] = ground_near_centre(chunks, table, centre_x, centre_z) + 4.0f;
        camera.eye[2] = centre_z + 22.0f;
        camera.target[0] = centre_x;
        camera.target[1] = camera.eye[1] - 6.0f;
        camera.target[2] = centre_z - 6.0f;
        camera.fov_degrees = 70.0f;
    } else if (view == "air") {
        // Outside the patch and above everything, looking across the surface.
        camera.eye[0] = centre_x - extent * 0.55f;
        camera.eye[1] = 135.0f;
        camera.eye[2] = centre_z - extent * 0.55f;
        camera.target[0] = centre_x;
        camera.target[1] = 62.0f;
        camera.target[2] = centre_z;
        camera.fov_degrees = 60.0f;
    } else if (view == "eye") {
        // Find the surface under the centre so the camera stands on the ground.
        // The highest solid block anywhere near the centre, so the camera
        // clears the hills instead of standing inside one.
        float ground = 64.0f;
        for (const auto& chunk : chunks) {
            if (std::abs((chunk.x + 0.5f) * 16.0f - centre_x) > 48.0f) continue;
            if (std::abs((chunk.z + 0.5f) * 16.0f - centre_z) > 48.0f) continue;
            for (int32_t lz = 0; lz < 16; lz += 4) {
                for (int32_t lx = 0; lx < 16; lx += 4) {
                    for (int32_t y = static_cast<int32_t>(chunk.sections.size()) * 16 - 1;
                         y >= 0; --y) {
                        const int32_t state = chunk.block_state(lx, y, lz);
                        if (state != 0 && table.full_cube(state)) {
                            ground = std::max(ground, static_cast<float>(y + chunk.min_y));
                            break;
                        }
                    }
                }
            }
        }
        camera.eye[0] = centre_x;
        camera.eye[1] = ground + 12.0f;
        camera.eye[2] = centre_z + 110.0f;
        camera.target[0] = centre_x;
        camera.target[1] = ground - 4.0f;
        camera.target[2] = centre_z - 60.0f;
        camera.fov_degrees = 65.0f;
        std::cout << std::format("- ground under the centre at y={}\n", static_cast<int>(ground));
    } else {
        camera.eye[0] = centre_x + extent * 0.9f;
        camera.eye[1] = 120.0f + extent * 0.45f;
        camera.eye[2] = centre_z + extent * 0.9f;
        camera.target[0] = centre_x;
        camera.target[1] = 60.0f;
        camera.target[2] = centre_z;
    }

    // Fog that reaches exactly as far as the world we loaded, so the edge of
    // the loaded area fades into the sky instead of ending in mid-air. The
    // game keeps it to the last stretch of the view, not half of it.
    {
        float reach = 0.0f;
        for (const float corner_x : {min_cx * 16.0f, (max_cx + 1) * 16.0f}) {
            for (const float corner_z : {min_cz * 16.0f, (max_cz + 1) * 16.0f}) {
                const float dx = corner_x - camera.eye[0];
                const float dz = corner_z - camera.eye[2];
                reach = std::max(reach, std::sqrt(dx * dx + dz * dz));
            }
        }
        camera.fog_end = reach;
        camera.fog_start = reach * 0.85f;
    }

    // The crosshair picks out a block the same way it does while playing, so a
    // screenshot of the client shows what the client would be pointing at.
    if (outline.has_value()) {
        const auto chunk_block = [&](int32_t x, int32_t y, int32_t z) -> int32_t {
            for (const auto& chunk : chunks) {
                if (chunk.x != (x >> 4) || chunk.z != (z >> 4)) continue;
                const int32_t index = chunk.index_of(y);
                if (index < 0) return 1;
                if (index >= static_cast<int32_t>(chunk.sections.size()) * 16) return 0;
                return chunk.block_state(x & 15, index, z & 15);
            }
            return -1;
        };
        float direction[3]{camera.target[0] - camera.eye[0], camera.target[1] - camera.eye[1],
                           camera.target[2] - camera.eye[2]};
        const float length = std::sqrt(direction[0] * direction[0] + direction[1] * direction[1] +
                                       direction[2] * direction[2]);
        for (float& component : direction) component /= length;
        int32_t aimed[3]{};
        const bool aiming = aimed_block(camera.eye, direction, 4.5f, chunk_block, table, aimed);
        renderer.set_outlined_block(aimed, aiming);
        std::cout << (aiming ? std::format("- crosshair on the block at {} {} {}\n", aimed[0],
                                           aimed[1], aimed[2])
                             : std::string("- crosshair on nothing within reach\n"));
    }

    // Every biome the server listed gets its four tints, and the sky follows the
    // one the camera stands in, the way the game works it out from the
    // temperature.
    if (!session.biomes().empty() && !chunks.empty()) {
        mcpp::world::Colormaps maps;
        maps.open(mcpp::world::Colormaps::default_path(mcpp::protocol::kTargetProtocol));
        renderer.set_biome_colours(session.biomes().tints(maps));
    }
    if (!session.environment().selected().empty()) {
        renderer.set_atmosphere(client::world_shot_atmosphere(session, chunks, camera, sky_options));
    }
    // J: rain or snow over the shot, from the same @ options.
    std::optional<mcpp::gpu::WeatherRenderer> weather;
    const auto falling = client::world_shot_weather(session, chunks, camera, sky_options);
    if (falling.intensity > 0.0f && std::filesystem::exists(sky_path)) {
        weather.emplace(device, renderer, sky_path);
        if (weather->ready()) {
            const double eye[3] = {camera.eye[0], camera.eye[1], camera.eye[2]};
            weather->set_frame(falling, eye);
            renderer.set_weather(&*weather);
            std::cout << std::format(
                "- weather: {} rain and {} snow columns, {} and {} corners\n",
                falling.rain.size(), falling.snow.size(), weather->rain_vertices(),
                weather->snow_vertices());
        }
    }

    mcpp::gpu::WorldRenderer::Overlay overlay;
    std::optional<mcpp::gpu::TextRenderer> text;
    std::optional<mcpp::gpu::UiRenderer> quads;
    if (overlay_kind != "none") {
        // A frame rate the scene actually reaches, measured here rather than
        // written into the picture.
        const auto before = std::chrono::steady_clock::now();
        constexpr int kWarm = 120;
        for (int i = 0; i < kWarm; ++i) renderer.draw(camera);
        const double per_frame = std::chrono::duration<double, std::milli>(
                                     std::chrono::steady_clock::now() - before).count() / kWarm;

        auto& registry = mcpp::features::Registry::instance();
        registry.lock(mcpp::features::Id::Hitboxes, "ranked policy");

        text.emplace(device, mcpp::gpu::TextRenderer::default_font_path(),
                     renderer.colour_format());
        quads.emplace(device, renderer.colour_format(),
                      mcpp::gpu::UiRenderer::default_items_path(mcpp::protocol::kTargetProtocol),
        mcpp::gpu::UiRenderer::default_gui_path(mcpp::protocol::kTargetProtocol));

        quads->begin();
        text->begin();
        if (overlay_kind == "paus") {
            client::draw_pause_over(*quads, renderer, shot_width, shot_height);
        } else if (overlay_kind == "menu") {
            mcpp::ui::MenuState state;
            state.server = address.host;
            state.fps = static_cast<int>(1000.0 / std::max(per_frame, 0.001));
            state.ping_ms = 1;
            state.x = camera.eye[0];
            state.y = camera.eye[1];
            state.z = camera.eye[2];
            state.yaw = -135.0f;
            mcpp::ui::draw_menu(*quads, *text, static_cast<float>(shot_width),
                                static_cast<float>(shot_height), state);
        } else {
            mcpp::ui::HudState state;
            state.fps = static_cast<int>(1000.0 / std::max(per_frame, 0.001));
            state.gpu_ms = renderer.last_gpu_ms();
            state.cpu_ms = per_frame;
            state.ping_ms = 1;
            state.x = camera.eye[0];
            state.y = camera.eye[1];
            state.z = camera.eye[2];
            state.forward = true;
            state.sections = renderer.drawn_sections();
            const auto& inventory = session.inventory();
            for (size_t i = 0; i < mcpp::Inventory::kHotbarSlots; ++i) {
                state.hotbar[i] = inventory.hotbar(i).id;
                state.hotbar_counts[i] = inventory.hotbar(i).count;
            }
            state.held = inventory.held;
            const auto& in_hand = inventory.hotbar(static_cast<size_t>(inventory.held));
            if (in_hand.count > 0) state.held_name = item_display_name(quads->item_name(in_hand.id));
            const auto& vitals = session.vitals();
            state.health = vitals.health;
            state.food = vitals.food;
            state.experience = vitals.experience;
            state.level = vitals.level;
            state.in_world = true;
            for (size_t i = 0; i < mcpp::Inventory::kArmourSlots; ++i) {
                const auto& piece = inventory.slots[mcpp::Inventory::kArmourFirst + i];
                if (piece.count > 0) state.armour += quads->item_armour(piece.id);
            }
            mcpp::ui::draw_hud(*quads, *text, static_cast<float>(shot_width),
                               static_cast<float>(shot_height), state);
        }
        overlay = [&](VkCommandBuffer command, uint32_t w, uint32_t h) {
            quads->record(command, w, h);
            text->record(command, w, h);
        };
        std::cout << std::format("- {:.0f} fps measured over {} frames; hitboxes locked to "
                                 "show the server lock\n", 1000.0 / per_frame, kWarm);
    }

    auto pixels = renderer.render(camera, overlay);
    mcpp::write_png(out_path, shot_width, shot_height, pixels);
    device.save_pipeline_cache();

    std::cout << std::format("{}\n", out_path);
    const auto totals = totals_of(combined);
    std::cout << std::format("  {} chunks, {} triangles, {:.1f} MB vertex data\n",
                             chunks.size(), totals.triangles, totals.bytes / 1e6);
    std::cout << std::format("  meshing {:.0f} ms, upload {:.0f} ms\n",
                             mesh_ms, renderer.upload_ms());
    std::cout << std::format("  gpu time in the frame: {:.2f} ms\n", renderer.last_gpu_ms());
    return 0;
}

// The game's own interface pieces on their own, at the game's gui scale, so a
// sprite drawn in nine slices can be checked at more than one size without a
// world, a server or a pointer in the way.
int do_uishot(const std::string& out_path) {
    constexpr uint32_t kWidth = 1280;
    constexpr uint32_t kHeight = 720;
    mcpp::world::Atlas atlas;
    if (!atlas.open(mcpp::world::Atlas::default_path(mcpp::protocol::kTargetProtocol))) {
        std::cerr << "no atlas; bake one with tools/bake_all.py\n";
        return 1;
    }
    mcpp::gpu::Device device;
    mcpp::gpu::Device::Options options;
    device.create(options);
    mcpp::gpu::WorldRenderer renderer(device, kWidth, kHeight, atlas);
    mcpp::gpu::UiRenderer quads(
        device, renderer.colour_format(),
        mcpp::gpu::UiRenderer::default_items_path(mcpp::protocol::kTargetProtocol),
        mcpp::gpu::UiRenderer::default_gui_path(mcpp::protocol::kTargetProtocol));
    if (!quads.has_sprites() || !quads.has_font()) {
        std::cerr << "no interface atlas; bake one with tools/bake_gui.py\n";
        return 1;
    }

    constexpr float gui = 3.0f;
    const auto overlay = [&](VkCommandBuffer command, uint32_t width, uint32_t height) {
        quads.begin();
        quads.panel(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f,
                    0x6E6E78FFu);
        // A button at the game's own width, and narrower and wider, so a
        // stretch shows up as a smeared border and a good slice does not.
        float y = 30.0f * gui;
        for (const float game_width : {50.0f, 200.0f, 400.0f}) {
            const float w = std::min(game_width * gui, static_cast<float>(width) - 40.0f);
            const float x = std::round((static_cast<float>(width) - w) * 0.5f);
            quads.nine_slice("widget/button", x, y, w, 20.0f * gui, gui);
            const std::string label = std::format("Button {:.0f}", game_width);
            quads.text(label, std::round(x + (w - quads.text_width(label, gui)) * 0.5f),
                       y + 6.0f * gui, gui, 0xFFFFFFFFu);
            y += 26.0f * gui;
        }
        quads.nine_slice("widget/button_highlighted", 40.0f * gui, y, 200.0f * gui, 20.0f * gui,
                         gui);
        quads.nine_slice("widget/button_disabled", 250.0f * gui, y, 120.0f * gui, 20.0f * gui,
                         gui);
        y += 34.0f * gui;
        const std::string tip = "Diamond Sword";
        const float text_w = quads.text_width(tip, gui);
        const float text_x = 60.0f * gui;
        quads.nine_slice("tooltip/background", text_x - 11.0f * gui, y - 11.0f * gui,
                         text_w + 22.0f * gui, 30.0f * gui, gui);
        quads.nine_slice("tooltip/frame", text_x - 11.0f * gui, y - 11.0f * gui,
                         text_w + 22.0f * gui, 30.0f * gui, gui);
        quads.text(tip, text_x, y, gui, 0xFFFFFFFFu);
        quads.record(command, width, height);
    };
    mcpp::gpu::Camera camera{};
    camera.eye[1] = 10000.0f;
    camera.target[0] = 1.0f;
    camera.target[1] = 10000.0f;
    const auto pixels = renderer.render(camera, overlay);
    mcpp::write_png(out_path, kWidth, kHeight, pixels);
    std::cout << out_path << '\n';
    return 0;
}

int do_render(const std::string& out_path, uint32_t width, uint32_t height) {
    using clock = std::chrono::steady_clock;
    const auto ms = [](auto from, auto to) {
        return std::chrono::duration<double, std::milli>(to - from).count();
    };

    const auto t0 = clock::now();
    mcpp::gpu::Device device;
    mcpp::gpu::Device::Options options;
    options.validation = true;
    device.create(options);
    const auto t1 = clock::now();

    mcpp::gpu::Offscreen frame(device, width, height);
    const auto t2 = clock::now();

    auto pixels = frame.render();
    const auto t3 = clock::now();

    mcpp::write_png(out_path, width, height, pixels);
    device.save_pipeline_cache();

    std::cout << std::format("{} ({}x{})\n", out_path, width, height);
    std::cout << std::format("  gpu: {}\n", device.adapter().name);
    std::cout << std::format("  device {:.1f} ms, setup {:.1f} ms (pipeline {:.1f} ms), "
                             "frame {:.1f} ms\n",
                             ms(t0, t1), ms(t1, t2), frame.pipeline_build_ms(), ms(t2, t3));
    std::cout << std::format("  gpu time in the frame: {:.3f} ms\n", frame.last_gpu_ms());
    return 0;
}

int do_gpu() {
    using clock = std::chrono::steady_clock;
    const auto ms = [](auto from, auto to) {
        return std::chrono::duration<double, std::milli>(to - from).count();
    };

    const auto before_list = clock::now();
    const auto adapters = mcpp::gpu::Device::list_adapters();
    const auto after_list = clock::now();

    std::cout << std::format("validation layers: {}\n",
                             mcpp::gpu::Device::validation_available() ? "available"
                                                                      : "not installed");
    std::cout << std::format("{} adapter(s) found in {:.1f} ms\n\n", adapters.size(),
                             ms(before_list, after_list));

    for (const auto& adapter : adapters) {
        const auto& caps = adapter.capabilities;
        std::cout << std::format("{}\n", adapter.name);
        std::cout << std::format("  {}, Vulkan {}, {}\n",
                                 mcpp::gpu::adapter_type_name(adapter.type),
                                 mcpp::gpu::api_version_string(adapter.api_version),
                                 adapter.driver);
        std::cout << std::format("  {:.1f} GB device-local memory\n",
                                 static_cast<double>(adapter.device_local_bytes) / 1e9);
        std::cout << std::format("  mesh shaders {}  descriptor indexing {}  "
                                 "buffer device address {}\n",
                                 caps.mesh_shader ? "yes" : "no",
                                 caps.descriptor_indexing ? "yes" : "no",
                                 caps.buffer_device_address ? "yes" : "no");
        std::cout << std::format("  dynamic rendering {}  sync2 {}  timeline semaphores {}  "
                                 "demote-to-helper {}\n\n",
                                 caps.dynamic_rendering ? "yes" : "no",
                                 caps.synchronization2 ? "yes" : "no",
                                 caps.timeline_semaphore ? "yes" : "no",
                                 caps.demote_to_helper ? "yes" : "no");
    }

    mcpp::gpu::Device device;
    mcpp::gpu::Device::Options options;
    options.validation = true;

    const auto before_create = clock::now();
    device.create(options);
    const auto after_create = clock::now();

    std::cout << std::format("chosen: {}\n", device.adapter().name);
    std::cout << std::format("  device created in {:.1f} ms\n", ms(before_create, after_create));
    std::cout << std::format("  pipeline cache: {} ({} bytes read)\n",
                             mcpp::gpu::Device::default_pipeline_cache().string(),
                             device.pipeline_cache_bytes_loaded());
    device.save_pipeline_cache();
    return 0;
}

// Builds one uniform section of a given state and reports what the mesher does
// with it, so a culling rule can be checked against a number.
// A section built from an explicit list of states, so a scene can be set up
// without a server. Direct palette: the ids go into the data array as-is.
mcpp::world::ChunkSection section_from_states(const std::vector<int32_t>& states) {
    constexpr uint8_t kBits = 15;
    constexpr size_t kPerLong = 64 / kBits;

    mcpp::ByteBuffer buffer;
    buffer.write_u8(kBits);
    const size_t longs = (states.size() + kPerLong - 1) / kPerLong;
    for (size_t index = 0; index < longs; ++index) {
        uint64_t packed = 0;
        for (size_t slot = 0; slot < kPerLong; ++slot) {
            const size_t at = index * kPerLong + slot;
            if (at >= states.size()) break;
            packed |= static_cast<uint64_t>(states[at]) << (slot * kBits);
        }
        buffer.write_i64(static_cast<int64_t>(packed));
    }

    mcpp::ByteBuffer biomes;
    biomes.write_u8(0);
    biomes.write_varint(0);

    mcpp::world::ChunkSection section;
    section.non_air_count = 4096;
    section.blocks = mcpp::world::PalettedContainer::read(buffer, 4096, 8);
    section.biomes = mcpp::world::PalettedContainer::read(biomes, 64, 3);
    return section;
}

// Joins, loads the world around spawn, and walks a body through it without
// opening a window. A screenshot cannot show whether the ground holds.
// Breaks a block and puts one back, without a window, and says what the server
// did about it. A client that can walk but not change anything is half a
// client, and this is the half that is hard to see in a screenshot.
int do_digtest(const std::string& target) {
    const auto address = parse_address(target);
    mcpp::world::BlockTable table;
    if (!table.open(mcpp::world::BlockTable::default_path(mcpp::protocol::kTargetProtocol))) {
        std::cerr << "no block table; bake one with tools/bake_all.py\n";
        return 1;
    }

    std::map<std::pair<int32_t, int32_t>, mcpp::world::Chunk> world;
    double spawn[3] = {0.0, 80.0, 0.0};
    bool placed = false;
    std::atomic<bool> ready{false};
    std::vector<std::string> changes;

    mcpp::SessionOptions options;
    options.host = address.host;
    options.port = address.port;
    std::string username = "PlasmaDig";
    options.profile = signed_in_profile(username);
    options.username = username;

    mcpp::SessionEvents events;
    events.on_status = [](std::string_view line) { std::cout << "- " << line << '\n'; };
    events.player.on_position = [&](double x, double y, double z, float, float) {
        spawn[0] = x;
        spawn[1] = y;
        spawn[2] = z;
        placed = true;
    };
    events.world.on_chunk = [&](const mcpp::world::Chunk& chunk) {
        world.insert_or_assign({chunk.x, chunk.z}, chunk);
        if (world.size() >= 25 && placed) ready.store(true);
    };
    mcpp::world::LightEngine light;
    std::vector<std::string> said_in_chat;
    std::vector<std::string> said_over_hotbar;
    events.ui.on_chat = [&](std::string_view line) { said_in_chat.emplace_back(line); };
    events.ui.on_action_bar = [&](std::string_view line) { said_over_hotbar.emplace_back(line); };
    std::function<void(int32_t, int32_t)> events_forget_hook;
    events.world.on_chunk_forget = [&](int32_t chunk_x, int32_t chunk_z) {
        if (events_forget_hook) events_forget_hook(chunk_x, chunk_z);
    };
    std::vector<std::string> journeys;
    events.world.on_respawn = [&](const mcpp::Dimension& dimension) {
        journeys.push_back(std::format("{} from y {}, {} chunks let go", dimension.name,
                                       dimension.min_y, world.size()));
        world.clear();
    };
    // Counted on its own, before anything is erased, so it shows the safety
    // net would have caught the old area even with no forget sent at all.
    size_t outside_area = 0;
    int32_t area_reach = -1;
    events.world.on_chunk_area = [&](int32_t centre_x, int32_t centre_z, int32_t radius) {
        area_reach = radius + 2;
        outside_area = 0;
        for (const auto& [key, chunk] : world) {
            if (std::abs(key.first - centre_x) > area_reach ||
                std::abs(key.second - centre_z) > area_reach) {
                ++outside_area;
            }
        }
    };
    events.world.on_block_change = [&](int32_t x, int32_t y, int32_t z, int32_t state) {
        const auto found = world.find({x >> 4, z >> 4});
        if (found != world.end() && found->second.set_block_state(x & 15, found->second.index_of(y), z & 15, state)) {
            light.relight(world, x, y, z);
        }
        changes.push_back(std::format("{} {} {} is now {} ({})", x, y, z, state,
                                      table.block_name(state)));
    };
    size_t relit = 0;
    events.world.on_light_update = [&](int32_t chunk_x, int32_t chunk_z, mcpp::ByteBuffer& in) {
        const auto found = world.find({chunk_x, chunk_z});
        if (found == world.end()) return;
        mcpp::world::read_light_into(in, found->second);
        ++relit;
    };

    mcpp::Session session(options, events);
    session.connect();
    const auto pump_for = [&](double seconds) {
        const auto until = std::chrono::steady_clock::now() +
                           std::chrono::duration<double>(seconds);
        while (std::chrono::steady_clock::now() < until) {
            try {
                if (!session.pump()) return false;
            } catch (const mcpp::TimeoutError&) {
                return false;
            }
        }
        return true;
    };
    while (!ready.load()) {
        try {
            if (!session.pump()) break;
        } catch (const mcpp::TimeoutError&) {
            break;
        }
    }
    if (!ready.load()) {
        std::cerr << "never got into the world\n";
        return 1;
    }

    std::cout << std::format("- vitals: health {:.1f}, food {}, xp {:.2f}, level {}\n",
                             session.vitals().health, session.vitals().food,
                             session.vitals().experience, session.vitals().level);

    const auto block_at = [&](int32_t x, int32_t y, int32_t z) -> int32_t {
        const auto found = world.find({x >> 4, z >> 4});
        if (found == world.end()) return -1;
        const int32_t index = found->second.index_of(y);
        if (index < 0 || index >= static_cast<int32_t>(found->second.sections.size()) * 16) {
            return 0;
        }
        return found->second.block_state(x & 15, index, z & 15);
    };

    // The highest solid block under the spawn, which is what the player would
    // be standing on and the one thing certain to be there.
    const auto foot_x = static_cast<int32_t>(std::floor(spawn[0]));
    const auto foot_z = static_cast<int32_t>(std::floor(spawn[2]));
    int32_t target_y = static_cast<int32_t>(std::floor(spawn[1])) - 1;
    while (target_y > -64 && block_at(foot_x, target_y, foot_z) == 0) --target_y;
    const int32_t before = block_at(foot_x, target_y, foot_z);
    std::cout << std::format("- standing on {} {} {}: {} ({})\n", foot_x, target_y, foot_z,
                             before, table.block_name(before));

    // Back to the spawn first: the server remembers where the last run left
    // this player, and the test should not depend on that.
    session.send_command("tp @s 4 70 3");
    pump_for(1.5);

    // A relative teleport is an offset, not a place. Read as a place, ten up
    // from 70 lands at 10.
    session.send_command("tp @s ~ ~10 ~");
    pump_for(1.0);
    std::cout << std::format("- ten up from 70 by a relative teleport: y {:.2f}\n", spawn[1]);
    session.send_command("tp @s 4 70 3");
    pump_for(1.0);

    // Effects, attributes and difficulty, each set by a command and read back
    // from what the server sent about it.
    session.send_command("effect give @s minecraft:speed 30 1");
    session.send_command("attribute @s minecraft:block_interaction_range modifier add "
                         "plasma:test 1.5 add_value");
    session.send_command("difficulty hard");
    pump_for(1.0);
    for (const auto& [id, effect] : session.effects(session.entity_id())) {
        const auto* entry = session.registries().at("minecraft:mob_effect", id);
        std::cout << std::format("- effect {} level {} for {} ticks\n",
                                 entry != nullptr ? entry->name : std::to_string(id),
                                 effect.amplifier + 1, effect.duration);
    }
    const auto entries = [&](std::string_view name) {
        const auto* registry = session.registries().find(name);
        return registry != nullptr ? registry->size() : 0;
    };
    std::cout << std::format("- {} damage types from the server, {} built-in mob effects\n",
                             entries("minecraft:damage_type"), entries("minecraft:mob_effect"));
    std::cout << std::format("- reach {:.2f}, speed {:.4f}, difficulty {}{}, {} tag registries\n",
                             session.attribute(session.entity_id(),
                                               "minecraft:block_interaction_range", -1.0),
                             session.attribute(session.entity_id(), "minecraft:movement_speed",
                                               -1.0),
                             session.difficulty(), session.difficulty_locked() ? " locked" : "",
                             session.tags().size());
    for (const auto& line : said_in_chat) std::cout << "  chat: " << line << "\n";
    session.send_command("effect clear @s");
    session.send_command("attribute @s minecraft:block_interaction_range modifier remove "
                         "plasma:test");
    session.send_command("difficulty normal");
    pump_for(1.0);
    std::cout << std::format("- after clearing: {} effects, reach {:.2f}, difficulty {}\n",
                             session.effects(session.entity_id()).size(),
                             session.attribute(session.entity_id(),
                                               "minecraft:block_interaction_range", -1.0),
                             session.difficulty());

    // The test gives itself something to build with, so placing is actually
    // exercised rather than silently doing nothing with an empty hand.
    session.send_command("give @s minecraft:stone 64");
    pump_for(1.0);
    session.send_held_slot(0);
    const auto& hand = session.inventory().hotbar(0);
    std::cout << std::format("- in hand: item {} x{}\n", hand.id, hand.count);

    session.send_position(spawn[0], spawn[1], spawn[2], 0.0f, 90.0f, true);
    session.send_dig(foot_x, target_y, foot_z, 1, false);
    session.send_swing();
    // The server times the dig itself and ignores a finish that comes too
    // early, so this waits the way a player holding the button would: stone
    // with bare hands is about seven and a half seconds.
    for (int tick = 0; tick < 200; ++tick) {
        if (!pump_for(0.05)) break;
        if (tick % 4 == 0) session.send_swing();
        session.send_position(spawn[0], spawn[1], spawn[2], 0.0f, 90.0f, true);
    }
    session.send_dig(foot_x, target_y, foot_z, 1, true);
    pump_for(1.5);
    const int32_t after = block_at(foot_x, target_y, foot_z);
    std::cout << std::format("- after digging: {} ({})\n", after, table.block_name(after));
    {
        const auto found = world.find({foot_x >> 4, foot_z >> 4});
        if (found != world.end()) {
            const auto& chunk = found->second;
            const int32_t index = chunk.index_of(target_y);
            std::cout << std::format(
                "- light in the hole: sky {} block {}; above it sky {}; beside it sky {}\n",
                chunk.sky_light_at(foot_x & 15, index, foot_z & 15),
                chunk.block_light_at(foot_x & 15, index, foot_z & 15),
                chunk.sky_light_at(foot_x & 15, index + 1, foot_z & 15),
                chunk.sky_light_at((foot_x + 1) & 15, index, foot_z & 15));
        }
    }

    session.send_place(foot_x, target_y - 1, foot_z, 1, 0.5f, 1.0f, 0.5f);
    session.send_swing();
    pump_for(1.5);
    const int32_t rebuilt = block_at(foot_x, target_y, foot_z);
    std::cout << std::format("- after placing: {} ({})\n", rebuilt, table.block_name(rebuilt));

    std::cout << std::format("- {} light updates arrived\n", relit);
    for (const auto& line : journeys) std::cout << "- respawned into " << line << '\n';
    for (const auto& line : said_over_hotbar) std::cout << "- over the hotbar: " << line << '\n';
    for (const auto& line : said_in_chat) {
        if (line.find("plasma-test") != std::string::npos) std::cout << "- in chat: " << line << '\n';
    }

    // Go far away; every chunk around the old spot should be forgotten, and
    // each one forgotten should be one we were actually holding.
    size_t forgotten = 0;
    size_t matched = 0;
    events_forget_hook = [&](int32_t chunk_x, int32_t chunk_z) {
        ++forgotten;
        if (world.erase({chunk_x, chunk_z}) != 0) ++matched;
    };
    const size_t held = world.size();
    session.send_command("tp @s 4000 120 4000");
    pump_for(3.0);
    std::cout << std::format("- far away: {} chunks forgotten, {} of them ones we held (of {})\n",
                             forgotten, matched, held);
    std::cout << std::format("- the new area (reach {}) would have let go of {} of them on its own\n",
                             area_reach, outside_area);
    for (const auto& line : changes) std::cout << "    " << line << '\n';
    session.close();

    if (changes.empty()) {
        std::cerr << "the server reported no block changes at all\n";
        return 1;
    }
    return 0;
}

int do_walktest(const std::string& target, double seconds) {
    const auto address = parse_address(target);
    mcpp::world::BlockTable table;
    if (!table.open(mcpp::world::BlockTable::default_path(mcpp::protocol::kTargetProtocol))) {
        std::cerr << "no block table; bake one with tools/bake_all.py\n";
        return 1;
    }

    std::map<std::pair<int32_t, int32_t>, mcpp::world::Chunk> world;
    std::atomic<bool> enough{false};
    double spawn[3] = {0.0, 80.0, 0.0};
    bool placed = false;

    mcpp::SessionOptions options;
    options.host = address.host;
    options.port = address.port;
    std::string username = "PlasmaWalk";
    options.profile = signed_in_profile(username);
    options.username = username;

    mcpp::SessionEvents events;
    events.on_status = [](std::string_view line) { std::cout << "- " << line << '\n'; };
    events.player.on_position = [&](double x, double y, double z, float, float) {
        spawn[0] = x;
        spawn[1] = y;
        spawn[2] = z;
        placed = true;
    };
    events.world.on_chunk = [&](const mcpp::world::Chunk& chunk) {
        world.insert_or_assign({chunk.x, chunk.z}, chunk);
        if (world.size() >= 64 && placed) enough.store(true);
    };

    mcpp::Session session(options, events);
    session.connect();
    while (!enough.load()) {
        try {
            if (!session.pump()) break;
        } catch (const mcpp::TimeoutError&) {
            break;
        }
    }
    session.close();
    if (world.empty()) return 1;
    std::cout << std::format("- {} chunks loaded, spawn at {:.1f} {:.1f} {:.1f}\n", world.size(),
                             spawn[0], spawn[1], spawn[2]);

    const auto block_at = [&](int32_t x, int32_t y, int32_t z) -> int32_t {
        const auto found = world.find({x >> 4, z >> 4});
        if (found == world.end()) return -1;
        const int32_t index = found->second.index_of(y);
        if (index < 0) return 1;
        if (index >= static_cast<int32_t>(found->second.sections.size()) * 16) return 0;
        return found->second.block_state(x & 15, index, z & 15);
    };

    // Four directions, because one of them is bound to be uphill and that is
    // the one that shows whether stepping up works.
    constexpr double kStep = mcpp::world::kTickSeconds;
    const auto steps = static_cast<int>(seconds / kStep);
    bool fell = false;
    const auto& shapes = mcpp::world::BlockShapes::game();
    const mcpp::world::BlockAt at = [&](int32_t x, int32_t y, int32_t z) {
        const int32_t state = block_at(x, y, z);
        return state == 1 && y < -64 ? 0 : state;
    };
    const mcpp::world::World walk_world{at, shapes, nullptr, -64};
    for (const float yaw : {0.0f, 90.0f, 180.0f, 270.0f}) {
        mcpp::world::Body body;
        body.x = body.xo = spawn[0];
        body.y = body.yo = spawn[1] + 6.0;
        body.z = body.zo = spawn[2];
        body.yaw = yaw;
        mcpp::world::Keys keys;
        keys.forward = true;
        const mcpp::world::Stats stats;

        double grounded = 0.0;
        double lowest = body.y;
        double highest = body.y;
        double last_x = body.x;
        double last_z = body.z;
        int stuck = 0;
        for (int i = 0; i < steps; ++i) {
            // A player walking into a hillside jumps. A full block is too tall
            // to step onto, in this client and in the game.
            if (std::hypot(body.x - last_x, body.z - last_z) < 0.01) {
                ++stuck;
            } else {
                stuck = 0;
            }
            last_x = body.x;
            last_z = body.z;
            keys.jump = stuck > 6 && body.on_ground;

            mcpp::world::tick(body, keys, stats, walk_world);
            if (body.on_ground) grounded += kStep;
            lowest = std::min(lowest, body.y);
            highest = std::max(highest, body.y);
        }
        const double travelled = std::hypot(body.x - spawn[0], body.z - spawn[2]);
        std::cout << std::format(
            "- yaw {:>3.0f}: {:5.1f} blocks in {:.0f}s, y {:.1f} to {:.1f}, "
            "on the ground {:.0f}% of the time\n",
            yaw, travelled, seconds, lowest, highest, grounded / seconds * 100.0);
        fell = fell || lowest < -60.0;
    }
    if (fell) {
        std::cerr << "fell out of the world\n";
        return 1;
    }
    return 0;
}

int do_showcase(const std::string& out_path, const std::vector<int32_t>& samples,
                const std::string& view) {
    mcpp::world::BlockTable table;
    mcpp::world::Atlas atlas;
    const auto protocol = mcpp::protocol::kTargetProtocol;
    if (!table.open(mcpp::world::BlockTable::default_path(protocol)) ||
        !atlas.open(mcpp::world::Atlas::default_path(protocol))) {
        std::cerr << "block table or atlas missing\n";
        return 1;
    }

    constexpr int32_t kStone = 1;
    std::vector<int32_t> states(4096, 0);
    const auto at = [](int32_t x, int32_t y, int32_t z) {
        return static_cast<size_t>(y) * 256 + static_cast<size_t>(z) * 16 +
               static_cast<size_t>(x);
    };

    for (int32_t z = 0; z < 16; ++z) {
        for (int32_t x = 0; x < 16; ++x) states[at(x, 0, z)] = kStone;
    }
    for (size_t i = 0; i < samples.size() && i < 12; ++i) {
        const auto x = static_cast<int32_t>(2 + (i % 6) * 2);
        const auto z = static_cast<int32_t>(4 + (i / 6) * 3);
        states[at(x, 1, z)] = samples[i];
        states[at(x, 2, z)] = samples[i];
    }

    mcpp::world::Chunk chunk;
    chunk.min_y = 0;   // one section, sitting at the origin
    chunk.sections.push_back(section_from_states(states));
    const mcpp::world::ChunkBlocks unpacked(chunk);
    const auto mesh = mcpp::world::mesh_chunk(chunk, unpacked, table, {});

    mcpp::gpu::Device device;
    mcpp::gpu::Device::Options options;
    options.validation = true;
    device.create(options);

    mcpp::gpu::WorldRenderer renderer(device, 1920, 1080, atlas);
    renderer.upload(mesh);

    mcpp::gpu::Camera camera{};
    if (view.starts_with("at:")) {
        float values[6]{};
        size_t at_comma = 3;
        for (float& value : values) {
            const size_t comma = view.find(',', at_comma);
            value = std::stof(view.substr(at_comma, comma - at_comma));
            at_comma = comma == std::string::npos ? view.size() : comma + 1;
        }
        for (int axis = 0; axis < 3; ++axis) {
            camera.eye[axis] = values[axis];
            camera.target[axis] = values[3 + axis];
        }
        camera.fov_degrees = 70.0f;
    } else if (view == "front") {
        // Straight at the south face of the first sample, nothing else in frame.
        camera.eye[0] = 2.5f;
        camera.eye[1] = 2.5f;
        camera.eye[2] = 11.0f;
        camera.target[0] = 2.5f;
        camera.target[1] = 2.5f;
        camera.target[2] = 4.0f;
        camera.fov_degrees = 30.0f;
    } else if (view == "east") {
        camera.eye[0] = 10.0f;
        camera.eye[1] = 2.5f;
        camera.eye[2] = 4.5f;
        camera.target[0] = 3.0f;
        camera.target[1] = 2.5f;
        camera.target[2] = 4.5f;
        camera.fov_degrees = 30.0f;
    } else if (view == "top") {
        camera.eye[0] = 2.5f;
        camera.eye[1] = 11.0f;
        camera.eye[2] = 4.6f;
        camera.target[0] = 2.5f;
        camera.target[1] = 3.0f;
        camera.target[2] = 4.5f;
        camera.fov_degrees = 30.0f;
    } else {
        camera.eye[0] = 8.0f;
        camera.eye[1] = 6.5f;
        camera.eye[2] = 19.0f;
        camera.target[0] = 8.0f;
        camera.target[1] = 2.0f;
        camera.target[2] = 7.0f;
        camera.fov_degrees = 45.0f;
    }

    auto pixels = renderer.render(camera);
    mcpp::write_png(out_path, 1920, 1080, pixels);
    device.save_pipeline_cache();

    std::cout << std::format("{}\n  {} blocks shown, {} triangles\n", out_path,
                             samples.size(), mesh.triangles());
    for (int32_t state : samples) {
        std::cout << std::format("  {:>6}  {}\n", state, table.block_name(state));
    }
    return 0;
}

int do_meshtest(int32_t state) {
    mcpp::world::BlockTable table;
    if (!table.open(mcpp::world::BlockTable::default_path(mcpp::protocol::kTargetProtocol))) {
        std::cerr << "no block table\n";
        return 1;
    }

    mcpp::ByteBuffer blocks;
    blocks.write_u8(0);
    blocks.write_varint(state);
    mcpp::ByteBuffer biomes;
    biomes.write_u8(0);
    biomes.write_varint(0);

    mcpp::world::Chunk chunk;
    mcpp::world::ChunkSection section;
    section.non_air_count = 4096;
    section.blocks = mcpp::world::PalettedContainer::read(blocks, 4096, 8);
    section.biomes = mcpp::world::PalettedContainer::read(biomes, 64, 3);
    chunk.sections.push_back(std::move(section));

    const mcpp::world::ChunkBlocks unpacked(chunk);
    const auto mesh = mcpp::world::mesh_chunk(chunk, unpacked, table);
    std::cout << std::format("state {} {}\n", state, table.block_name(state));
    std::cout << std::format("  flags {:#04x}  full cube {}  opaque {}  fluid {}\n",
                             table.flags(state), table.full_cube(state) ? "yes" : "no",
                             table.occludes(state) ? "yes" : "no",
                             table.fluid(state) ? "yes" : "no");
    std::cout << std::format("  quads per block in the table: {}\n", table.quads(state).size());
    std::cout << std::format("  solid section: {} quads kept, {} culled\n",
                             mesh.quads_emitted, mesh.quads_culled);
    std::cout << std::format("  (the shell alone would be {})\n", 6 * 16 * 16);
    return 0;
}

int do_blocks(const std::string& query) {
    using clock = std::chrono::steady_clock;

    const auto path = mcpp::world::BlockTable::default_path(mcpp::protocol::kTargetProtocol);
    mcpp::world::BlockTable table;

    const auto before_open = clock::now();
    const bool opened = table.open(path);
    const auto after_open = clock::now();

    if (!opened) {
        std::cerr << std::format("no block table at {}\nbake one with "
                                 "tools/bake_block_table.py\n", path.string());
        return 1;
    }

    const auto us = [](auto from, auto to) {
        return std::chrono::duration_cast<std::chrono::microseconds>(to - from).count();
    };

    // mmap is lazy, so opening is not the whole cost: walk every state to force
    // the pages the renderer will touch anyway.
    size_t quads = 0;
    size_t full_cubes = 0;
    const auto before_walk = clock::now();
    for (size_t state = 0; state < table.state_count(); ++state) {
        quads += table.quads(static_cast<int32_t>(state)).size();
        if (table.full_cube(static_cast<int32_t>(state))) ++full_cubes;
    }
    const auto after_walk = clock::now();

    std::cout << std::format("{}\n  {:.2f} MB, protocol {}\n", path.string(),
                             static_cast<double>(table.mapped_bytes()) / 1e6, table.protocol());
    std::cout << std::format("  {} states, {} quads, {} full cubes\n",
                             table.state_count(), quads, full_cubes);
    std::cout << std::format("  mmap {} us, whole table walked {} us\n",
                             us(before_open, after_open), us(before_walk, after_walk));

    if (query.empty()) return 0;

    size_t shown = 0;
    for (size_t state = 0; state < table.state_count() && shown < 4; ++state) {
        const auto id = static_cast<int32_t>(state);
        const auto name = table.block_name(id);
        if (name.find(query) == std::string_view::npos && std::to_string(state) != query) continue;

        const auto geometry = table.quads(id);
        std::cout << std::format("\nstate {} {}  flags {:#04x}  {} quads{}\n", state, name,
                                 table.flags(id), geometry.size(),
                                 table.full_cube(id) ? "  (full cube)" : "");
        for (const auto& quad : geometry) {
            const auto units = [](int16_t v) {
                return static_cast<double>(v) / mcpp::world::kUnitsPerModelUnit;
            };
            std::cout << std::format(
                "  face {} cull {:>2}  from ({:g},{:g},{:g}) to ({:g},{:g},{:g})  {}\n",
                quad.face, quad.cullface,
                units(quad.from[0]), units(quad.from[1]), units(quad.from[2]),
                units(quad.to[0]), units(quad.to[1]), units(quad.to[2]),
                table.texture_name(quad.texture));
        }
        ++shown;
    }
    return 0;
}

int do_join(const std::string& target, const std::string& username, int32_t protocol) {
    const auto address = parse_address(target);

    std::mutex out_mutex;
    auto print = [&](char marker, std::string_view line) {
        std::lock_guard guard(out_mutex);
        std::cout << std::format("{} {}\n", marker, line) << std::flush;
    };

    mcpp::SessionOptions options;
    options.host = address.host;
    options.port = address.port;
    options.username = username;
    options.protocol = protocol;
    options.debug_packets = std::getenv("PLASMA_DEBUG") != nullptr;
    if (const char* dump = std::getenv("PLASMA_CHUNK_DUMP")) options.chunk_dump = dump;
    mcpp::world::BlockTable block_table;
    const bool meshing = block_table.open(mcpp::world::BlockTable::default_path(protocol));

    size_t chunks = 0;
    size_t sections = 0;
    size_t emitted = 0;
    size_t culled = 0;
    size_t triangles = 0;
    size_t mesh_bytes = 0;
    double mesh_ms = 0.0;
    std::set<int32_t> states;
    std::map<int32_t, size_t> histogram;

    mcpp::SessionEvents events;
    events.on_status = [&](std::string_view line) { print('-', line); };
    events.ui.on_chat = [&](std::string_view line) { print('|', line); };
    events.on_disconnect = [&](std::string_view line) { print('x', line); };
    events.world.on_chunk = [&](const mcpp::world::Chunk& chunk) {
        ++chunks;
        sections += chunk.non_empty_sections();
        for (const auto& section : chunk.sections) {
            for (int32_t state : section.blocks.palette()) states.insert(state);
            for (size_t i = 0; i < section.blocks.size(); ++i) {
                ++histogram[section.blocks.at(i)];
            }
        }
        if (meshing) {
            const auto start = std::chrono::steady_clock::now();
            const mcpp::world::ChunkBlocks unpacked_chunk(chunk);
            const auto mesh = mcpp::world::mesh_chunk(chunk, unpacked_chunk, block_table);
            mesh_ms += std::chrono::duration<double, std::milli>(
                           std::chrono::steady_clock::now() - start).count();
            emitted += mesh.quads_emitted;
            culled += mesh.quads_culled;
            triangles += mesh.triangles();
            mesh_bytes += mesh.bytes();
        }
        if (chunks % 32 == 0) {
            print('#', std::format("{} chunks, {} filled sections, {} block states, last ({},{})",
                                   chunks, sections, states.size(), chunk.x, chunk.z));
        }
    };

    mcpp::Session session(options, events);
    session.connect();

    std::atomic<bool> running{true};
    std::thread reader([&] {
        while (running.load()) {
            try {
                session.pump();
            } catch (const mcpp::TimeoutError&) {
                continue;
            } catch (const std::exception& error) {
                if (running.load()) print('x', error.what());
                running.store(false);
                return;
            }
        }
    });

    print('-', "type to chat, /command to send a command, /quit to stop");
    std::string line;
    while (running.load() && std::getline(std::cin, line)) {
        if (line == "/quit") break;
        if (line.empty()) continue;
        try {
            if (line.front() == '/') {
                session.send_command(line.substr(1));
            } else {
                session.send_chat(line);
            }
        } catch (const std::exception& error) {
            print('x', error.what());
        }
    }

    running.store(false);
    session.close();
    reader.join();
    if (chunks > 0) {
        print('#', std::format("total {} chunks, {} filled sections, {} unique block states",
                               chunks, sections, states.size()));
        if (meshing) {
            const double total = static_cast<double>(emitted + culled);
            print('#', std::format("mesh: {} quads kept, {} culled ({:.1f}%), {} triangles",
                                   emitted, culled, total > 0 ? 100.0 * culled / total : 0.0,
                                   triangles));
            print('#', std::format("      {:.1f} MB vertex data, {:.1f} ms meshing "
                                   "({:.2f} ms per chunk)",
                                   mesh_bytes / 1e6, mesh_ms, mesh_ms / chunks));
        } else {
            print('#', "no block table loaded, so nothing was meshed");
        }
        std::vector<std::pair<int32_t, size_t>> ranked(histogram.begin(), histogram.end());
        std::ranges::sort(ranked, [](const auto& a, const auto& b) { return a.second > b.second; });
        for (size_t i = 0; i < ranked.size() && i < 10; ++i) {
            print('#', std::format("  state {:<6} {} blocks", ranked[i].first, ranked[i].second));
        }
    }
    return 0;
}

int do_ping(const std::string& target) {
    const auto address = parse_address(target);
    mcpp::status::Request request;
    request.host = address.host;
    request.port = address.port;
    request.measure_latency = false;
    const auto reply = mcpp::status::query(request);

    std::cout << std::format("{}:{}  {} (protocol {})  {}/{} players\n", request.host,
                             request.port, reply.version, reply.protocol, reply.online, reply.max);

    if (reply.protocol == mcpp::protocol::kTargetProtocol) {
        std::cout << "match: we speak the same version.\n";
    } else {
        std::cout << std::format("we speak {} ({}); only works if the server has ViaVersion.\n",
                                 mcpp::protocol::kTargetProtocol, mcpp::protocol::kTargetVersionName);
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    const std::string command = argc > 1 ? argv[1] : "";

    // --vanilla and --plasma pick which client the player sees, and override
    // whatever the config last saved. Everything under the surface is shared.
    std::vector<char*> arguments(argv, argv + argc);
    for (auto it = arguments.begin(); it != arguments.end();) {
        const std::string_view argument(*it);
        if (argument == "--vanilla" || argument == "--plasma") {
            mcpp::features::Registry::instance().set_profile(
                argument == "--vanilla" ? mcpp::features::Profile::Vanilla
                                        : mcpp::features::Profile::Plasma);
            it = arguments.erase(it);
        } else {
            ++it;
        }
    }
    argv = arguments.data();
    argc = static_cast<int>(arguments.size());

    try {
        if (const int entities = client::entity_command(argc, argv); entities >= 0) {
            return entities;
        }
        if (command == "features") {
            print_features();
            return 0;
        }
        if (command == "versions") {
            print_banner();
            print_versions();
            return 0;
        }
        if (command == "ping" && argc > 2) {
            return do_ping(argv[2]);
        }
        if (command == "bench" && argc > 2) {
            const size_t limit = argc > 3 ? std::stoul(argv[3]) : 200;
            const size_t frames = argc > 4 ? std::stoul(argv[4]) : 600;
            const uint32_t width = argc > 5 ? static_cast<uint32_t>(std::stoul(argv[5])) : 1920;
            const uint32_t height = argc > 6 ? static_cast<uint32_t>(std::stoul(argv[6])) : 1080;
            return do_bench(argv[2], limit, frames, width, height);
        }
        if (command == "play" && argc > 2) {
            const size_t limit = argc > 3 ? std::stoul(argv[3]) : 240;
#ifdef MCPP_HAVE_WINDOW
            return client::play(argv[2], limit);
#else
            (void)limit;
            std::cerr << "this build has no window: glfw3 was not found at configure time\n";
            return 1;
#endif
        }
        if ((command == "worldshot" || command == "menushot" || command == "hudshot" ||
             command == "pauseshot") &&
            argc > 2) {
            const std::string out = argc > 3 ? argv[3] : "world.png";
            const size_t limit = argc > 4 ? std::stoul(argv[4]) : 120;
            const std::string view = argc > 5 ? argv[5] : "wide";
            const std::string overlay = command == "worldshot" ? "none"
                                                               : command.substr(0, 4);
            return do_worldshot(argv[2], out, limit, view, overlay);
        }
        if (command == "worldbench" && argc > 2) {
            std::vector<int32_t> distances;
            std::stringstream list(argc > 5 ? argv[5] : "8,16,32");
            for (std::string item; std::getline(list, item, ',');) distances.push_back(std::stoi(item));
            return client::world_bench(argv[2], argc > 3 ? std::stoul(argv[3]) : 200,
                                       argc > 4 ? std::stoul(argv[4]) : 600, distances,
                                       argc > 6 ? std::stoll(argv[6]) : 6000,
                                       argc > 7 ? std::stoul(argv[7]) : 0);
        }
        if (command == "perfreport") {
            return client::perfreport(std::vector<std::string>(argv + 2, argv + argc));
        }
        if (command == "inspect" && argc > 2) {
            return client::inspect(argv[2], argc > 3 ? argv[3] : "PlasmaShot",
                                   argc > 4 ? std::stod(argv[4]) : 3.0, argc > 5 ? argv[5] : "");
        }
        if (command == "clicktest") {
            return client::clicktest(std::vector<std::string>(argv + 2, argv + argc));
        }
        if (command == "crafttest") {
            return client::crafttest(std::vector<std::string>(argv + 2, argv + argc));
        }
        // F: the chat line, with the server as the referee.
        if (command == "chattest") {
            return client::chattest(std::vector<std::string>(argv + 2, argv + argc));
        }
        if (command == "booktest") {
            return client::booktest(std::vector<std::string>(argv + 2, argv + argc));
        }
        if (command == "outlineshot") {
            return client::outlineshot(std::vector<std::string>(argv + 2, argv + argc));
        }
        if (command == "blockshot") {
            return client::blockshot(std::vector<std::string>(argv + 2, argv + argc));
        }
        if (command == "blockserver") {
            return client::blockserver(std::vector<std::string>(argv + 2, argv + argc));
        }
        if (command == "blockbench") {
            return client::blockbench(std::vector<std::string>(argv + 2, argv + argc));
        }
        if (command == "screenshot") {
            return client::screenshot(std::vector<std::string>(argv + 2, argv + argc));
        }
        if (command == "hudshot-scenes") {
            return client::hud_shot(std::vector<std::string>(argv + 2, argv + argc));
        }
        if (command == "uishot") {
            return do_uishot(argc > 2 ? argv[2] : "ui.png");
        }
        if (command == "render") {
            const std::string out = argc > 2 ? argv[2] : "frame.png";
            const uint32_t width = argc > 3 ? static_cast<uint32_t>(std::stoul(argv[3])) : 1280;
            const uint32_t height = argc > 4 ? static_cast<uint32_t>(std::stoul(argv[4])) : 720;
            return do_render(out, width, height);
        }
        if (command == "record" && argc > 3) {
            return do_record(argv[2], argv[3], argc > 4 ? std::stoul(argv[4]) : 200,
                             argc > 5 ? argv[5] : "PlasmaShot");
        }
        if (command == "churn" && argc > 2) {
            return do_churn(argv[2], argc > 3 ? std::stoul(argv[3]) : 64,
                            argc > 4 ? std::stoi(argv[4]) : 40);
        }
        if (command == "digtest" && argc > 2) {
            return do_digtest(argv[2]);
        }
        if (command == "itemshot") {
            return client::itemshot(std::vector<std::string>(argv + 2, argv + argc));
        }
        if (command == "handshot") {
            return client::handshot(argc, argv);
        }
        if (command == "breakshot") {
            return client::breakshot(argc > 2 ? argv[2] : "break");
        }
        if (command == "digtest2" && argc > 2) {
            return client::digtest2(argv[2]);
        }
        if (command == "movetest" && argc > 2) {
            return client::movetest(argv[2]);
        }
        if (command == "walktest" && argc > 2) {
            return do_walktest(argv[2], argc > 3 ? std::stod(argv[3]) : 10.0);
        }
        if (command == "login") {
            return do_login();
        }
        if (command == "logout") {
            return do_logout();
        }
        if (command == "gpu") {
            return do_gpu();
        }
        if (command == "showcase" && argc > 2) {
            std::vector<int32_t> samples;
            std::string view = "angled";
            for (int i = 3; i < argc; ++i) {
                const std::string argument = argv[i];
                if (argument == "front" || argument == "top" || argument == "angled" ||
                    argument == "east" || argument.starts_with("at:")) {
                    view = argument;
                } else {
                    samples.push_back(std::stoi(argument));
                }
            }
            return do_showcase(argv[2], samples, view);
        }
        if (command == "meshtest" && argc > 2) {
            return do_meshtest(std::stoi(argv[2]));
        }
        if (command == "blocks") {
            return do_blocks(argc > 2 ? argv[2] : "");
        }
        if (command == "join" && argc > 2) {
            const std::string username = argc > 3 ? argv[3] : "PlasmaDev";
            const int32_t protocol = argc > 4 ? std::stoi(argv[4])
                                              : mcpp::protocol::kTargetProtocol;
            return do_join(argv[2], username, protocol);
        }

        print_banner();
        std::cout << std::format(
            "\nusage: {} <command>\n"
            "  features            which features are on\n"
            "  versions            supported protocol versions\n"
            "  ping <host:port>    ask a server for its details\n"
            "  blocks [name]       the pre-baked block table\n"
            "  showcase <out.png> <state...>  render blocks on their own to inspect them\n"
            "  gpu                 which gpu and which features\n"
            "  render <out.png> [w] [h]   render a frame and write it out\n"
            "  bench <host:port> [chunks] [frames] [w] [h]   measure frame time and fps\n"
            "  play <host:port> [chunks]   open a window and walk through the world\n"
            "  worldshot <host:port> [out.png] [chunks]\n"
            "                      log in, mesh chunks and render the world\n"
            "  perfreport <file.plperf> [out.png]\n"
            "                      a session recorded while playing, as a table and a graph\n"
            "  inspect <host:port|replay:file> [name] [seconds] [record.plrc]\n"
            "                      print every registry, slot, entity and data packet\n"
            "  join <host:port> [name] [protocol]\n"
            "                      log in and stay in the play state\n"
            "\nno window yet: frames are written to PNG.\n",
            argv[0]);
        return command.empty() ? 0 : 2;
    } catch (const std::exception& error) {
        std::cerr << std::format("error: {}\n", error.what());
        return 1;
    }
}
