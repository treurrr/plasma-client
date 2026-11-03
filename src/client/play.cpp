#include "client/game.hpp"

#include "mcpp/protocol_versions.hpp"

#include <algorithm>
#include <atomic>
#include <csignal>
#include <format>
#include <iostream>
#include <ranges>

namespace client {
namespace {

// A clean exit matters: it is what writes the pipeline cache to disk, which is
// most of the next start-up.
std::atomic<bool> g_stop{false};

void stop_on_signal(int) { g_stop.store(true); }

} // namespace

int play(const std::string& target, size_t chunk_limit) {
    Game game(chunk_limit);
    game.address = parse_address(target);
    if (!game.table.open(mcpp::world::BlockTable::default_path(mcpp::protocol::kTargetProtocol))) {
        std::cerr << "no block table; bake one with tools/bake_block_table.py\n";
        return 1;
    }

    game.options.host = game.address.host;
    game.options.port = game.address.port;
    std::string username = "PlasmaDev";
    game.options.profile = signed_in_profile(username);
    game.options.username = username;

    mcpp::SessionEvents events;
    events.on_status = [](std::string_view line) { std::cout << "- " << line << '\n'; };
    world_bind(game, events);
    entities_bind(game, events);
    player_bind(game, events);
    screens_bind(game, events);
    sound_bind(game, events);
    hud_bind(game, events);

    auto& session = game.session.emplace(game.options, events);
    session.connect();
    while (!game.world.enough.load()) {
        try {
            session.pump();
        } catch (const mcpp::TimeoutError&) {
            break;
        }
    }
    std::cout << std::format("- {} chunks fetched\n", game.world.chunks.size());
    if (game.world.chunks.empty()) {
        session.close();
        return 1;
    }

    world_load(game);

    mcpp::gpu::Device::Options gpu_options;
    gpu_options.instance_extensions = mcpp::gpu::Window::required_instance_extensions();
    gpu_options.swapchain = true;
    game.device.create(gpu_options);

    if (!game.atlas.open(mcpp::world::Atlas::default_path(mcpp::protocol::kTargetProtocol))) {
        std::cerr << "no atlas; bake one with tools/bake_atlas.py\n";
        return 1;
    }

    auto& window = game.window.emplace(game.device, 1600, 900, "Plasma Client");
    auto& renderer =
        game.renderer.emplace(game.device, window.width(), window.height(), game.atlas);

    world_start(game);
    std::cout << std::format("- health {:.1f}, food {}, level {}\n", session.vitals().health,
                             session.vitals().food, session.vitals().level);
    std::cout << std::format("- {:.0f} MB reserved on the gpu, {:.1f} MB used by the first {} chunks\n",
                             renderer.vertex_capacity() / 1e6, game.world.vertex_bytes / 1e6,
                             game.world.chunks.size());
    entities_start(game);
    player_start(game);
    screens_start(game);
    sound_start(game);
    hud_start(game);

    std::cout << "- window open: WASD to move, mouse to look, shift and space for down and up,\n"
                 "  left control to sprint, f to fly, t to chat, m for the feature menu,\n"
                 "  escape to leave\n";

    auto previous = std::chrono::steady_clock::now();
    size_t frames = 0;
    std::vector<double> frame_times;
    frame_times.reserve(20000);
    float tick_accumulator = 0.0f;
    double gpu_total = 0.0;

    window.capture_cursor(true);
    std::signal(SIGINT, stop_on_signal);
    std::signal(SIGTERM, stop_on_signal);

    // One frame, always in this order. Keys first; then the player moves on
    // what the world was; then the server's packets land; then everything
    // that acts on the world as it now is; then the game tick's position;
    // then the picture.
    while (window.open() && !g_stop.load()) {
        game.input = window.poll();
        screens_input(game);
        player_input(game);
        world_input(game);
        entities_input(game);
        sound_input(game);

        game.now = std::chrono::steady_clock::now();
        game.dt = std::chrono::duration<float>(game.now - previous).count();
        previous = game.now;

        player_move(game);

        // Drain whatever the server sent, then report where we are. Standing
        // still in the socket is how a client gets timed out. A bundle is read
        // to its end even if the rest of it is still on the wire, so no frame
        // is drawn with half of it applied.
        while (session.has_data() || session.in_bundle()) {
            try {
                if (!session.pump()) break;
            } catch (const std::exception&) {
                break;
            }
        }

        tick_accumulator += game.dt;
        game.tick_due = tick_accumulator >= 0.05f;
        if (game.tick_due) tick_accumulator = 0.0f;

        player_tick(game);
        screens_tick(game);
        world_tick(game);
        entities_tick(game);
        sound_tick(game);
        hud_tick(game);
        if (game.tick_due && !player_send(game)) break;

        // The numbers are measured here, not estimated: fps from the wall clock,
        // gpu from the timestamps the frame itself wrote.
        game.fps = game.fps == 0.0 ? 1.0 / game.dt : game.fps * 0.92 + (1.0 / game.dt) * 0.08;

        // A tiling compositor sizes the window after it opens.
        if (window.width() != 0 && window.height() != 0 &&
            (window.width() != renderer.width() || window.height() != renderer.height())) {
            renderer.resize(window.width(), window.height());
        }
        world_draw(game);
        entities_draw(game);
        player_draw(game);
        sound_draw(game);
        screens_draw(game);

        window.present(renderer, game.camera,
                       [&](VkCommandBuffer command, uint32_t w, uint32_t h) {
                           screens_record(game, command, w, h);
                       });
        gpu_total += renderer.last_gpu_ms();
        if (frames > 30) frame_times.push_back(window.last_frame_ms());
        ++frames;
    }

    world_stop(game);
    entities_stop(game);
    player_stop(game);
    screens_stop(game);
    sound_stop(game);
    hud_stop(game);
    session.close();
    if (frames > 0) {
        std::cout << std::format("- {} frames, {:.3f} ms gpu on average, {} positions sent, "
                                 "{} chunks streamed in\n",
                                 frames, gpu_total / static_cast<double>(frames),
                                 game.player.ticks_sent, game.world.streamed);
        if (frame_times.size() > 50) {
            // The mean hides hitches, so report what the worst frames cost.
            std::ranges::sort(frame_times);
            const auto at = [&](double fraction) {
                return frame_times[static_cast<size_t>(
                    static_cast<double>(frame_times.size() - 1) * fraction)];
            };
            std::cout << std::format(
                "- frame time: median {:.2f} ms, 1% low {:.2f} ms, 0.1% low {:.2f} ms, "
                "worst {:.2f} ms\n",
                at(0.5), at(0.99), at(0.999), frame_times.back());
        }
    }
    game.device.save_pipeline_cache();
    return 0;
}

} // namespace client
