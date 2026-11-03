// The world renderer's benchmark, measured the way docs/METHOD.md says: fixed
// flights through the same recorded world at several render distances, every
// frame recorded, reported as average fps, 1% and 0.1% lows, p99 frame time,
// cpu and gpu ms per frame, chunk load time and resident memory.
#include "client/game.hpp"

#include "mcpp/protocol_versions.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <numeric>
#include <sstream>

namespace client {
namespace {

double resident_mb() {
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.starts_with("VmRSS:")) {
            std::istringstream fields(line.substr(6));
            double kb = 0.0;
            fields >> kb;
            return kb / 1024.0;
        }
    }
    return 0.0;
}

struct Flight {
    const char* name;
    // Where the camera is at t in 0..1, and where it looks.
    void (*place)(float t, const float centre[3], float extent, mcpp::gpu::Camera& camera);
};

const Flight kFlights[] = {
    {"low pass over the water",
     [](float t, const float c[3], float extent, mcpp::gpu::Camera& camera) {
         camera.eye[0] = c[0] - extent * 0.4f + extent * 0.8f * t;
         camera.eye[1] = 66.0f;
         camera.eye[2] = c[2] - extent * 0.2f;
         camera.target[0] = camera.eye[0] + 10.0f;
         camera.target[1] = 63.0f;
         camera.target[2] = camera.eye[2] - 4.0f;
     }},
    {"circle over the trees",
     [](float t, const float c[3], float extent, mcpp::gpu::Camera& camera) {
         const float angle = 6.2831853f * t;
         camera.eye[0] = c[0] + extent * 0.25f * std::cos(angle);
         camera.eye[1] = c[1] + 20.0f;
         camera.eye[2] = c[2] + extent * 0.25f * std::sin(angle);
         camera.target[0] = c[0];
         camera.target[1] = c[1];
         camera.target[2] = c[2];
     }},
    {"walk across the hills",
     [](float t, const float c[3], float extent, mcpp::gpu::Camera& camera) {
         camera.eye[0] = c[0];
         camera.eye[1] = c[1] + 3.0f;
         camera.eye[2] = c[2] + extent * 0.35f - extent * 0.7f * t;
         camera.target[0] = camera.eye[0] + 3.0f * std::sin(6.2831853f * t);
         camera.target[1] = camera.eye[1] - 0.5f;
         camera.target[2] = camera.eye[2] - 10.0f;
     }},
    {"climb to the clouds",
     [](float t, const float c[3], float extent, mcpp::gpu::Camera& camera) {
         camera.eye[0] = c[0] - extent * 0.3f;
         camera.eye[1] = c[1] + 5.0f + 120.0f * t;
         camera.eye[2] = c[2] - extent * 0.3f;
         camera.target[0] = c[0] + extent * 0.3f;
         camera.target[1] = c[1];
         camera.target[2] = c[2] + extent * 0.3f;
     }},
};

} // namespace

int world_bench(const std::string& target, size_t chunk_limit, size_t frames,
                const std::vector<int32_t>& distances) {
    mcpp::world::BlockTable table;
    mcpp::world::Atlas atlas;
    const auto protocol = mcpp::protocol::kTargetProtocol;
    if (!table.open(mcpp::world::BlockTable::default_path(protocol)) ||
        !atlas.open(mcpp::world::Atlas::default_path(protocol))) {
        std::cerr << "block table or atlas missing\n";
        return 1;
    }
    std::vector<mcpp::world::Chunk> chunks;
    mcpp::SessionOptions options;
    if (target.starts_with("replay:")) {
        options.replay = target.substr(7);
    } else {
        const size_t colon = target.rfind(':');
        options.host = target.substr(0, colon);
        options.port = colon == std::string::npos ? 25565 : static_cast<uint16_t>(std::stoi(target.substr(colon + 1)));
    }
    options.username = "PlasmaBench";
    mcpp::SessionEvents events;
    events.world.on_chunk = [&](const mcpp::world::Chunk& chunk) {
        if (chunks.size() < chunk_limit) chunks.push_back(chunk);
    };
    const auto load_started = std::chrono::steady_clock::now();
    mcpp::Session session(options, events);
    session.connect();
    while (chunks.size() < chunk_limit) {
        try {
            if (!session.pump()) break;
        } catch (const mcpp::TimeoutError&) {
            break;
        }
    }
    const double receive_ms = std::chrono::duration<double, std::milli>(
                                  std::chrono::steady_clock::now() - load_started).count();
    if (chunks.empty()) return 1;
    double mesh_ms = 0.0;
    const auto mesh = mesh_world(chunks, table, &mesh_ms);

    int32_t min_cx = chunks.front().x, max_cx = min_cx, min_cz = chunks.front().z, max_cz = min_cz;
    for (const auto& chunk : chunks) {
        min_cx = std::min(min_cx, chunk.x);
        max_cx = std::max(max_cx, chunk.x);
        min_cz = std::min(min_cz, chunk.z);
        max_cz = std::max(max_cz, chunk.z);
    }
    const float extent = static_cast<float>(std::max(max_cx - min_cx + 1, max_cz - min_cz + 1)) * 16.0f;
    const float centre[3] = {(min_cx + max_cx + 1) * 8.0f, 72.0f, (min_cz + max_cz + 1) * 8.0f};

    mcpp::gpu::Device device;
    device.create({});
    mcpp::gpu::WorldRenderer renderer(device, 1920, 1080, atlas);
    const auto upload_started = std::chrono::steady_clock::now();
    renderer.upload(mesh);
    const double upload_ms = std::chrono::duration<double, std::milli>(
                                 std::chrono::steady_clock::now() - upload_started).count();
    std::optional<mcpp::gpu::Sky> sky;
    const auto sky_path = mcpp::gpu::Sky::default_path(protocol);
    if (std::filesystem::exists(sky_path)) {
        sky.emplace(device, renderer.colour_format(), renderer.depth_format(), sky_path);
        renderer.set_sky(&*sky);
    }

    std::cout << std::format("{}: {} chunks, received in {:.0f} ms, meshed in {:.0f} ms "
                             "({:.2f} ms a chunk), uploaded in {:.0f} ms; {:.0f} MB resident\n",
                             target, chunks.size(), receive_ms, mesh_ms,
                             mesh_ms / static_cast<double>(chunks.size()), upload_ms, resident_mb());
    std::cout << "flight                       rd   avg fps  1% low  0.1% low  p99 ms   cpu ms  gpu ms\n";
    double sky_total = 0.0;
    int flights_run = 0;
    for (const int32_t distance : distances) {
        for (const auto& flight : kFlights) {
            std::vector<double> frame_ms;
            std::vector<double> cpu_ms;
            std::vector<double> gpu_ms;
            std::vector<double> sky_ms;
            for (size_t frame = 0; frame < frames + 10; ++frame) {
                mcpp::gpu::Camera camera{};
                flight.place(static_cast<float>(frame) / static_cast<float>(frames + 10), centre, extent,
                             camera);
                const auto started = std::chrono::steady_clock::now();
                const auto atmosphere =
                    world_shot_atmosphere(session, chunks, camera, std::format("t=6000,rd={}", distance));
                renderer.set_atmosphere(atmosphere);
                sky_ms.push_back(std::chrono::duration<double, std::milli>(
                                     std::chrono::steady_clock::now() - started).count());
                const double gpu = renderer.draw(camera);
                const double wall = std::chrono::duration<double, std::milli>(
                                        std::chrono::steady_clock::now() - started).count();
                if (frame < 10) continue;
                frame_ms.push_back(wall);
                gpu_ms.push_back(gpu);
                cpu_ms.push_back(std::max(0.0, wall - gpu));
            }
            const auto mean = [](const std::vector<double>& v) {
                return std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(v.size());
            };
            auto sorted = frame_ms;
            std::ranges::sort(sorted);
            const auto slowest = [&](double fraction) {
                const size_t take = std::max<size_t>(1, static_cast<size_t>(fraction * sorted.size()));
                return std::accumulate(sorted.end() - static_cast<long>(take), sorted.end(), 0.0) /
                       static_cast<double>(take);
            };
            const double p99 = sorted[std::min(sorted.size() - 1,
                                               static_cast<size_t>(0.99 * static_cast<double>(sorted.size())))];
            std::cout << std::format("{:<28} {:>3} {:>9.0f} {:>7.0f} {:>9.0f} {:>7.3f} {:>8.3f} {:>7.3f}\n",
                                     flight.name, distance, 1000.0 / mean(frame_ms),
                                     1000.0 / slowest(0.01), 1000.0 / slowest(0.001), p99, mean(cpu_ms),
                                     mean(gpu_ms));
            sky_total += mean(sky_ms);
            ++flights_run;
        }
    }
    std::cout << std::format("sky, fog and lightmap worked out from scratch each frame: {:.4f} ms\n",
                             sky_total / std::max(flights_run, 1));
    std::cout << std::format("resident after the flights: {:.0f} MB\n", resident_mb());
    session.close();
    return 0;
}

} // namespace client
