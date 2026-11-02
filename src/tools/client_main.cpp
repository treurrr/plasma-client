#include "mcpp/features.hpp"
#include "mcpp/json.hpp"
#include "mcpp/packet_io.hpp"
#include "mcpp/protocol_versions.hpp"
#include "mcpp/session.hpp"
#include "mcpp/gpu/device.hpp"
#include "mcpp/gpu/offscreen.hpp"
#include "mcpp/gpu/world_renderer.hpp"
#ifdef MCPP_HAVE_WINDOW
#include "mcpp/gpu/window.hpp"
#endif
#include "mcpp/image_png.hpp"
#include "mcpp/world/atlas.hpp"
#include "mcpp/world/block_table.hpp"
#include "mcpp/world/mesher.hpp"

#include <atomic>
#include <chrono>
#include <numbers>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <format>
#include <iostream>
#include <mutex>
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

struct Address {
    std::string host;
    uint16_t port = 25565;
};

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
                        ground = std::max(ground, static_cast<float>(y - 64));
                        break;
                    }
                }
            }
        }
    }
    return ground;
}

float ground_near_centre(const std::vector<mcpp::world::Chunk>& chunks,
                         const mcpp::world::BlockTable& table, float centre_x, float centre_z);

// Joins a server, keeps chunks until it has enough, and leaves again.
std::vector<mcpp::world::Chunk> load_chunks(const Address& address, size_t limit, bool quiet) {
    std::mutex guard;
    std::vector<mcpp::world::Chunk> chunks;
    std::atomic<bool> enough{false};

    mcpp::SessionOptions options;
    options.host = address.host;
    options.port = address.port;
    options.username = "PlasmaShot";

    mcpp::SessionEvents events;
    if (!quiet) {
        events.on_status = [](std::string_view line) { std::cout << "- " << line << '\n'; };
    }
    events.on_chunk = [&](const mcpp::world::Chunk& chunk) {
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

// Unpacks every chunk once - a neighbour is read by four others - then meshes
// every section of every chunk.
std::vector<mcpp::world::SectionMesh> mesh_world(const std::vector<mcpp::world::Chunk>& chunks,
                                                 const mcpp::world::BlockTable& table,
                                                 double* milliseconds) {
    const auto start = std::chrono::steady_clock::now();

    std::map<std::pair<int32_t, int32_t>, mcpp::world::ChunkBlocks> unpacked;
    for (const auto& chunk : chunks) {
        unpacked.emplace(std::pair{chunk.x, chunk.z}, mcpp::world::ChunkBlocks(chunk));
    }
    const auto find_blocks = [&](int32_t x, int32_t z) -> const mcpp::world::ChunkBlocks* {
        auto it = unpacked.find({x, z});
        return it == unpacked.end() ? nullptr : &it->second;
    };

    std::vector<mcpp::world::SectionMesh> all;
    for (const auto& chunk : chunks) {
        const mcpp::world::Neighbours neighbours{
            .west = find_blocks(chunk.x - 1, chunk.z),
            .east = find_blocks(chunk.x + 1, chunk.z),
            .north = find_blocks(chunk.x, chunk.z - 1),
            .south = find_blocks(chunk.x, chunk.z + 1),
        };
        auto pieces = mcpp::world::mesh_sections(chunk, *find_blocks(chunk.x, chunk.z), table,
                                                 neighbours);
        all.insert(all.end(), std::make_move_iterator(pieces.begin()),
                   std::make_move_iterator(pieces.end()));
    }

    if (milliseconds) {
        *milliseconds = std::chrono::duration<double, std::milli>(
                            std::chrono::steady_clock::now() - start).count();
    }
    return all;
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
    std::cout << std::format("upload: {:.0f} ms\n\n", renderer.upload_ms());
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


#ifdef MCPP_HAVE_WINDOW
int do_play(const std::string& target, size_t chunk_limit) {
    const auto address = parse_address(target);
    mcpp::world::BlockTable table;
    if (!table.open(mcpp::world::BlockTable::default_path(mcpp::protocol::kTargetProtocol))) {
        std::cerr << "no block table; bake one with tools/bake_block_table.py\n";
        return 1;
    }

    std::mutex guard;
    std::vector<mcpp::world::Chunk> chunks;
    std::atomic<bool> enough{false};

    mcpp::SessionOptions options;
    options.host = address.host;
    options.port = address.port;
    options.username = "PlasmaDev";

    double spawn[3] = {0.0, 0.0, 0.0};
    float spawn_yaw = 0.0f;
    float spawn_pitch = 0.0f;
    bool placed = false;

    mcpp::SessionEvents events;
    events.on_status = [](std::string_view line) { std::cout << "- " << line << '\n'; };
    events.on_position = [&](double x, double y, double z, float yaw, float pitch) {
        spawn[0] = x;
        spawn[1] = y;
        spawn[2] = z;
        spawn_yaw = yaw;
        spawn_pitch = pitch;
        placed = true;
        std::cout << std::format("- the server put us at {:.2f} {:.2f} {:.2f}\n", x, y, z);
    };
    events.on_chunk = [&](const mcpp::world::Chunk& chunk) {
        std::lock_guard lock(guard);
        if (chunks.size() < chunk_limit) chunks.push_back(chunk);
        if (chunks.size() >= chunk_limit) enough.store(true);
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
    std::cout << std::format("- {} chunks fetched\n", chunks.size());
    if (chunks.empty()) {
        session.close();
        return 1;
    }

    double mesh_ms = 0.0;
    const auto combined = mesh_world(chunks, table, &mesh_ms);

    mcpp::gpu::Device device;
    mcpp::gpu::Device::Options gpu_options;
    gpu_options.instance_extensions = mcpp::gpu::Window::required_instance_extensions();
    gpu_options.swapchain = true;
    device.create(gpu_options);

    mcpp::world::Atlas atlas;
    if (!atlas.open(mcpp::world::Atlas::default_path(mcpp::protocol::kTargetProtocol))) {
        std::cerr << "no atlas; bake one with tools/bake_atlas.py\n";
        return 1;
    }

    mcpp::gpu::Window window(device, 1600, 900, "Plasma Client");
    mcpp::gpu::WorldRenderer renderer(device, window.width(), window.height(), atlas);
    renderer.upload(combined);

    float centre_x = 0.0f;
    float centre_z = 0.0f;
    for (const auto& chunk : chunks) {
        centre_x += (chunk.x + 0.5f) * 16.0f;
        centre_z += (chunk.z + 0.5f) * 16.0f;
    }
    centre_x /= static_cast<float>(chunks.size());
    centre_z /= static_cast<float>(chunks.size());

    // Stand where the server put us, not where the chunks happen to be centred.
    float eye[3] = {static_cast<float>(spawn[0]), static_cast<float>(spawn[1]) + 1.62f,
                    static_cast<float>(spawn[2])};
    if (!placed) {
        eye[0] = centre_x;
        eye[1] = ground_near_centre(chunks, table, centre_x, centre_z) + 4.0f;
        eye[2] = centre_z;
        std::cout << "- no teleport from the server; standing at the centre of the chunks\n";
    }
    float yaw = placed ? spawn_yaw - 90.0f : -90.0f;
    float pitch = placed ? -spawn_pitch : -8.0f;

    std::cout << "- window open: WASD to move, mouse to look, shift and space for down and up,\n"
                 "  left control to sprint, escape to leave\n";

    auto previous = std::chrono::steady_clock::now();
    size_t frames = 0;
    size_t ticks_sent = 0;
    float tick_accumulator = 0.0f;
    double gpu_total = 0.0;

    while (window.open()) {
        const auto input = window.poll();
        if (input.quit) break;

        const auto now = std::chrono::steady_clock::now();
        const float dt = std::chrono::duration<float>(now - previous).count();
        previous = now;

        yaw += input.yaw_delta * 0.12f;
        pitch = std::clamp(pitch - input.pitch_delta * 0.12f, -89.0f, 89.0f);

        const float yr = yaw * std::numbers::pi_v<float> / 180.0f;
        const float pr = pitch * std::numbers::pi_v<float> / 180.0f;
        const float forward[3] = {std::cos(yr) * std::cos(pr), std::sin(pr),
                                  std::sin(yr) * std::cos(pr)};
        const float right[3] = {-std::sin(yr), 0.0f, std::cos(yr)};

        const float speed = (input.sprint ? 46.0f : 14.0f) * dt;
        for (int i = 0; i < 3; ++i) {
            eye[i] += forward[i] * input.forward * speed + right[i] * input.right * speed;
        }
        eye[1] += input.up * speed;

        mcpp::gpu::Camera camera{};
        for (int i = 0; i < 3; ++i) {
            camera.eye[i] = eye[i];
            camera.target[i] = eye[i] + forward[i];
        }
        camera.fov_degrees = 70.0f;

        // Drain whatever the server sent, then report where we are. Standing
        // still in the socket is how a client gets timed out.
        while (session.has_data()) {
            try {
                if (!session.pump()) break;
            } catch (const std::exception&) {
                break;
            }
        }
        tick_accumulator += dt;
        if (tick_accumulator >= 0.05f) {
            tick_accumulator = 0.0f;
            try {
                session.send_position(eye[0], eye[1] - 1.62, eye[2], yaw + 90.0f, -pitch, false);
                ++ticks_sent;
            } catch (const std::exception& error) {
                std::cerr << std::format("movement stopped: {}\n", error.what());
                break;
            }
        }

        window.present(renderer, camera);
        gpu_total += renderer.last_gpu_ms();
        ++frames;
    }

    session.close();
    if (frames > 0) {
        std::cout << std::format("- {} frames, {:.3f} ms gpu on average, {} positions sent\n",
                                 frames, gpu_total / static_cast<double>(frames), ticks_sent);
    }
    device.save_pipeline_cache();
    return 0;
}
#endif

int do_worldshot(const std::string& target, const std::string& out_path, size_t chunk_limit,
                 const std::string& view) {
    const auto address = parse_address(target);
    mcpp::world::BlockTable table;
    if (!table.open(mcpp::world::BlockTable::default_path(mcpp::protocol::kTargetProtocol))) {
        std::cerr << "no block table; bake one with tools/bake_block_table.py\n";
        return 1;
    }

    std::mutex guard;
    std::vector<mcpp::world::Chunk> chunks;
    std::atomic<bool> enough{false};

    mcpp::SessionOptions options;
    options.host = address.host;
    options.port = address.port;
    options.username = "PlasmaShot";

    mcpp::SessionEvents events;
    events.on_status = [](std::string_view line) { std::cout << "- " << line << '\n'; };
    events.on_chunk = [&](const mcpp::world::Chunk& chunk) {
    std::lock_guard lock(guard);
    if (chunks.size() < chunk_limit) chunks.push_back(chunk);
    if (chunks.size() >= chunk_limit) enough.store(true);
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
                column = std::format("highest block at (8,8) of chunk ({},{}): y={} state={} {}",
                                     first.x, first.z, y - 64, state, table.block_name(state));
                break;
            }
        }
        std::cout << "- " << column << '\n';
        std::map<std::string, int> tops;
        int lowest = 1000;
        int highest = -1000;
        for (int32_t lz = 0; lz < 16; ++lz) {
            for (int32_t lx = 0; lx < 16; ++lx) {
                for (int32_t y = static_cast<int32_t>(first.sections.size()) * 16 - 1; y >= 0; --y) {
                    const int32_t state = first.block_state(lx, y, lz);
                    if (state == 0) continue;
                    ++tops[std::string(table.block_name(state))];
                    lowest = std::min(lowest, y - 64);
                    highest = std::max(highest, y - 64);
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
    const auto combined = mesh_world(chunks, table, &mesh_ms);

    mcpp::gpu::Device device;
    mcpp::gpu::Device::Options gpu_options;
    gpu_options.validation = true;
    device.create(gpu_options);

    mcpp::world::Atlas atlas;
    if (!atlas.open(mcpp::world::Atlas::default_path(mcpp::protocol::kTargetProtocol))) {
        std::cerr << "no atlas; bake one with tools/bake_atlas.py\n";
        return 1;
    }

    const uint32_t shot_width = view == "macro" ? 1920u : 1600u;
    const uint32_t shot_height = view == "macro" ? 1080u : 900u;
    mcpp::gpu::WorldRenderer renderer(device, shot_width, shot_height, atlas);
    renderer.upload(combined);

    mcpp::gpu::Camera camera{};
    if (view == "macro") {
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
                            ground = std::max(ground, static_cast<float>(y - 64));
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

    auto pixels = renderer.render(camera);
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
    chunk.sections.push_back(section_from_states(states));
    const mcpp::world::ChunkBlocks unpacked(chunk);
    const auto mesh = mcpp::world::mesh_chunk(chunk, unpacked, table, {}, 0);

    mcpp::gpu::Device device;
    mcpp::gpu::Device::Options options;
    options.validation = true;
    device.create(options);

    mcpp::gpu::WorldRenderer renderer(device, 1920, 1080, atlas);
    renderer.upload(mesh);

    mcpp::gpu::Camera camera{};
    if (view == "front") {
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
    events.on_chat = [&](std::string_view line) { print('|', line); };
    events.on_disconnect = [&](std::string_view line) { print('x', line); };
    events.on_chunk = [&](const mcpp::world::Chunk& chunk) {
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
    const std::string& host = address.host;
    const uint16_t port = address.port;

    mcpp::TcpSocket socket;
    socket.connect(host, port);
    mcpp::PacketConnection connection(std::move(socket));

    mcpp::ByteBuffer handshake;
    // -1 = unknown: ViaVersion mirrors back whatever we claim, which would
    // hide the server's real version.
    handshake.write_varint(-1);
    handshake.write_string(host);
    handshake.write_u16(port);
    handshake.write_varint(1);
    connection.send(0x00, handshake);
    connection.send(0x00, mcpp::ByteBuffer{});

    auto packet = connection.receive();
    auto root = mcpp::json::parse(packet.data.read_string(32767));

    std::string version = "?";
    int protocol = -1;
    if (const auto* v = root.find("version")) {
        if (const auto* name = v->find("name")) version = name->string_or("?");
        if (const auto* proto = v->find("protocol")) protocol = static_cast<int>(proto->number_or(-1));
    }

    int online = -1;
    int max = -1;
    if (const auto* players = root.find("players")) {
        if (const auto* o = players->find("online")) online = static_cast<int>(o->number_or(-1));
        if (const auto* m = players->find("max")) max = static_cast<int>(m->number_or(-1));
    }

    std::cout << std::format("{}:{}  {} (protocol {})  {}/{} players\n", host, port, version,
                             protocol, online, max);

    if (protocol == mcpp::protocol::kTargetProtocol) {
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

    try {
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
            return do_play(argv[2], limit);
#else
            (void)limit;
            std::cerr << "this build has no window: glfw3 was not found at configure time\n";
            return 1;
#endif
        }
        if (command == "worldshot" && argc > 2) {
            const std::string out = argc > 3 ? argv[3] : "world.png";
            const size_t limit = argc > 4 ? std::stoul(argv[4]) : 120;
            const std::string view = argc > 5 ? argv[5] : "wide";
            return do_worldshot(argv[2], out, limit, view);
        }
        if (command == "render") {
            const std::string out = argc > 2 ? argv[2] : "frame.png";
            const uint32_t width = argc > 3 ? static_cast<uint32_t>(std::stoul(argv[3])) : 1280;
            const uint32_t height = argc > 4 ? static_cast<uint32_t>(std::stoul(argv[4])) : 720;
            return do_render(out, width, height);
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
                    argument == "east") {
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
