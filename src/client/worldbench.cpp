// The world renderer's benchmark, measured the way docs/METHOD.md says: fixed
// flights through the same recorded world at several render distances, every
// frame recorded, reported as average fps, 1% and 0.1% lows, p99 frame time,
// cpu and gpu ms per frame with that cpu time split the way a frame spends it,
// chunk load time and resident memory.
//
// A figure from here gets quoted next to other clients' benchmarks, so the run
// prints what it holds and what it leaves out, and every row carries the
// chunks and triangles it was drawn with: a render distance that the recorded
// world cannot fill is a render distance this machine never drew.
#include "client/game.hpp"

#include "mcpp/entity/assets.hpp"
#include "mcpp/entity/scene.hpp"
#include "mcpp/gpu/entity_renderer.hpp"
#include "mcpp/gpu/ui.hpp"
#include "mcpp/jmath.hpp"
#include "mcpp/options.hpp"
#include "mcpp/protocol_versions.hpp"
#include "mcpp/skins.hpp"
#include "mcpp/translations.hpp"
#include "mcpp/ui/hud/hud.hpp"
#include "mcpp/ui/items.hpp"
#include "mcpp/ui/screen.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iostream>
#include <map>
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

// This thread's own cpu time. The frame waits for the gpu on a fence, and that
// wait is a sleep, so it stays out of this the way Horizon's "cpu ms" keeps the
// gpu, display and limiter waits out of theirs. It is a system call, so it is
// read twice a frame and no more.
double cpu_ms_now() {
    timespec now{};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now);
    return static_cast<double>(now.tv_sec) * 1e3 + static_cast<double>(now.tv_nsec) / 1e6;
}

// The stopwatch the parts of a frame are measured with: none of them waits, so
// it says what the cpu clock would, without the system call.
double mark_ms() {
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<double>(now.tv_sec) * 1e3 + static_cast<double>(now.tv_nsec) / 1e6;
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
         camera.eye[1] = c[1] - 6.0f;
         camera.eye[2] = c[2] - extent * 0.2f;
         camera.target[0] = camera.eye[0] + 10.0f;
         camera.target[1] = camera.eye[1] - 3.0f;
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

// Where one frame's cpu time went, in the four parts Horizon's table splits:
// the world's own update, the game's tick, getting the frame ready, and the
// renderer.
struct Split {
    double world = 0.0;
    double tick = 0.0;
    double prepare = 0.0;
    double renderer = 0.0;

    void add(const Split& other) {
        world += other.world;
        tick += other.tick;
        prepare += other.prepare;
        renderer += other.renderer;
    }
    void divide(double by) {
        world /= by;
        tick /= by;
        prepare /= by;
        renderer /= by;
    }
};

struct Frame {
    double wall = 0.0;
    double cpu = 0.0;
    double gpu = 0.0;
    Split split;
    mcpp::gpu::WorldRenderer::FrameCost cost;
};

double mean(const std::vector<double>& values) {
    if (values.empty()) return 0.0;
    return std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
}

// The client's own sky clock: the attributes at the camera once a tick, the
// flicker's random walk, and the frame between two ticks drawn from both.
struct SkyClock {
    mcpp::world::AttributeProbe probe;
    mcpp::world::RainFog rain_fog;
    int64_t day_time = 6000;
    int64_t game_time = 0;
    double ticks = 0.0;
    float partial = 0.0f;
    float flicker = 0.0f;
    uint64_t flicker_seed = 0x5DEECE66Dull;
    bool probed = false;

    void flick() {
        mcpp::jmath::LegacyRandom random(static_cast<int64_t>(flicker_seed));
        flicker += (random.next_float() - random.next_float()) * random.next_float() *
                   random.next_float() * 0.1f;
        flicker *= 0.9f;
        flicker_seed = static_cast<uint64_t>(random.next_long());
    }
};

} // namespace

int world_bench(const std::string& target, size_t chunk_limit, size_t frames,
                const std::vector<int32_t>& distances, int64_t day_time) {
    mcpp::world::BlockTable table;
    mcpp::world::Atlas atlas;
    const auto protocol = mcpp::protocol::kTargetProtocol;
    if (!table.open(mcpp::world::BlockTable::default_path(protocol)) ||
        !atlas.open(mcpp::world::Atlas::default_path(protocol))) {
        std::cerr << "block table or atlas missing\n";
        return 1;
    }
    mcpp::entity::Assets entity_assets;
    const bool entities_baked = entity_assets.open(mcpp::entity::Assets::default_path(protocol));
    mcpp::skins::Cache skins;
    mcpp::entity::Scene scene(
        mcpp::entity::load_entity_types(mcpp::entity::default_registries_path(protocol)), &skins);

    // One chunk per position: a recording made by walking a square sends a
    // chunk again every time it comes back into view.
    std::map<ChunkKey, mcpp::world::Chunk> by_position;
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
        if (by_position.size() < chunk_limit || by_position.contains({chunk.x, chunk.z})) {
            by_position.insert_or_assign(ChunkKey{chunk.x, chunk.z}, chunk);
        }
    };
    events.entities.on_add = [&](const mcpp::Entity& entity) { scene.add(entity); };
    events.entities.on_moved = [&](const mcpp::EntityMove& move) { scene.moved(move); };
    events.entities.on_head = [&](int32_t id, float yaw) { scene.head(id, yaw); };
    events.entities.on_remove = [&](int32_t id) { scene.remove(id); };
    events.entities.on_animate = [&](int32_t id, uint8_t action) { scene.animate(id, action); };
    events.entities.on_hurt = [&](int32_t id, float) { scene.hurt(id); };
    events.entities.on_damage = [&](int32_t id) { scene.damaged(id); };
    events.entities.on_event = [&](int32_t id, uint8_t status) { scene.event(id, status); };
    events.entities.on_player_info = [&](const mcpp::PlayerInfo& info) { scene.player_info(info); };
    events.entities.on_player_remove = [&](std::string_view uuid) { scene.player_gone(uuid); };
    const auto load_started = std::chrono::steady_clock::now();
    mcpp::Session session(options, events);
    session.connect();
    while (by_position.size() < chunk_limit) {
        try {
            if (!session.pump()) break;
        } catch (const mcpp::TimeoutError&) {
            break;
        }
    }
    const double receive_ms = std::chrono::duration<double, std::milli>(
                                  std::chrono::steady_clock::now() - load_started).count();
    if (by_position.empty()) return 1;

    int32_t min_cx = by_position.begin()->first.first, max_cx = min_cx;
    int32_t min_cz = by_position.begin()->first.second, max_cz = min_cz;
    for (const auto& [key, chunk] : by_position) {
        min_cx = std::min(min_cx, key.first);
        max_cx = std::max(max_cx, key.first);
        min_cz = std::min(min_cz, key.second);
        max_cz = std::max(max_cz, key.second);
    }
    // The square of chunks a client holds at render distance d is the one
    // around the camera, so each distance takes its own square out of what was
    // recorded. The middle of the recording is not where that square belongs:
    // a recording made by walking somewhere else first has a tail of chunks
    // over there, and the middle of the two is a hole. The flights start from
    // the fullest square instead.
    const size_t grid_width = static_cast<size_t>(max_cx - min_cx + 1);
    const size_t grid_depth = static_cast<size_t>(max_cz - min_cz + 1);
    std::vector<int32_t> filled((grid_width + 1) * (grid_depth + 1), 0);
    const auto cell = [&](size_t x, size_t z) -> int32_t& {
        return filled[z * (grid_width + 1) + x];
    };
    for (const auto& [key, chunk] : by_position) {
        cell(static_cast<size_t>(key.first - min_cx) + 1,
             static_cast<size_t>(key.second - min_cz) + 1) = 1;
    }
    for (size_t z = 1; z <= grid_depth; ++z) {
        for (size_t x = 1; x <= grid_width; ++x) {
            cell(x, z) += cell(x - 1, z) + cell(x, z - 1) - cell(x - 1, z - 1);
        }
    }
    constexpr int32_t kCentreReach = 16;
    int32_t centre_cx = (min_cx + max_cx) / 2;
    int32_t centre_cz = (min_cz + max_cz) / 2;
    int32_t fullest = -1;
    for (size_t z = 1; z <= grid_depth; ++z) {
        for (size_t x = 1; x <= grid_width; ++x) {
            const auto clamp = [](long value, long low, long high) {
                return static_cast<size_t>(std::max(low, std::min(high, value)));
            };
            const size_t x0 = clamp(static_cast<long>(x) - kCentreReach - 1, 0,
                                    static_cast<long>(grid_width));
            const size_t x1 = clamp(static_cast<long>(x) + kCentreReach, 0,
                                    static_cast<long>(grid_width));
            const size_t z0 = clamp(static_cast<long>(z) - kCentreReach - 1, 0,
                                    static_cast<long>(grid_depth));
            const size_t z1 = clamp(static_cast<long>(z) + kCentreReach, 0,
                                    static_cast<long>(grid_depth));
            const int32_t held = cell(x1, z1) - cell(x0, z1) - cell(x1, z0) + cell(x0, z0);
            if (held > fullest) {
                fullest = held;
                centre_cx = min_cx + static_cast<int32_t>(x) - 1;
                centre_cz = min_cz + static_cast<int32_t>(z) - 1;
            }
        }
    }
    std::vector<mcpp::world::Chunk> chunks;
    chunks.reserve(by_position.size());
    for (auto& [key, chunk] : by_position) chunks.push_back(std::move(chunk));
    by_position.clear();
    const auto ring = [&](const mcpp::world::Chunk& chunk) {
        return std::max(std::abs(chunk.x - centre_cx), std::abs(chunk.z - centre_cz));
    };
    std::ranges::stable_sort(chunks, [&](const auto& a, const auto& b) { return ring(a) < ring(b); });

    const float centre_x = (centre_cx + 0.5f) * 16.0f;
    const float centre_z = (centre_cz + 0.5f) * 16.0f;
    const float ground = ground_near_centre(chunks, table, centre_x, centre_z);

    mcpp::gpu::Device device;
    device.create({});
    mcpp::gpu::WorldRenderer renderer(device, 1920, 1080, atlas);
    std::optional<mcpp::gpu::EntityRenderer> entity_renderer;
    if (entities_baked) {
        entity_renderer.emplace(device, renderer.colour_format());
        entity_renderer->load(entity_assets);
        scene.bind(*entity_renderer);
    }
    std::optional<mcpp::gpu::Sky> sky;
    const auto sky_path = mcpp::gpu::Sky::default_path(protocol);
    if (std::filesystem::exists(sky_path)) {
        sky.emplace(device, renderer.colour_format(), renderer.depth_format(), sky_path);
        renderer.set_sky(&*sky);
    }

    // The hud the window draws over every frame, built the same way here.
    mcpp::gpu::UiRenderer quads(device, renderer.colour_format(),
                                mcpp::gpu::UiRenderer::default_items_path(protocol),
                                mcpp::gpu::UiRenderer::default_gui_path(protocol));
    mcpp::Translations lang;
    lang.load(mcpp::Translations::default_path());
    mcpp::Options ui_options;
    mcpp::ui::ItemTable items;
    items.load(mcpp::ui::ItemTable::default_path(protocol));
    mcpp::ui::hud::Gui gui;
    mcpp::ui::hud::Player hud_player;
    mcpp::ui::hud::Frame hud_frame;
    mcpp::Scoreboard board;
    std::vector<mcpp::ui::hud::TabEntry> tab;
    {
        const char* names[9] = {"diamond_sword", "bow",          "cobblestone", "golden_apple",
                                "oak_planks",    "water_bucket", "torch",       "cooked_beef",
                                "ender_pearl"};
        const int counts[9] = {1, 1, 64, 3, 32, 1, 17, 12, 16};
        for (size_t i = 0; i < 9; ++i) {
            hud_player.hotbar[i] = {items.find(names[i]), counts[i]};
        }
        hud_player.health = 14.0f;
        hud_player.food = 16;
        hud_player.armour = 12;
        hud_player.experience = 0.45f;
        hud_player.level = 23;
        hud_frame.own_name = "PlasmaBench";
        hud_frame.scoreboard = &board;
        hud_frame.players = &tab;
        mcpp::ui::hud::TabEntry self;
        self.uuid = "bench";
        self.name = "PlasmaBench";
        tab.push_back(self);
    }
    const int gui_scale = mcpp::ui::gui_scale(1920, 1080, 0, false);
    const int gui_width = mcpp::ui::gui_extent(1920, gui_scale);
    const int gui_height = mcpp::ui::gui_extent(1080, gui_scale);

    const auto chunk_at = [&](int32_t cx, int32_t cz) -> const mcpp::world::Chunk* {
        const auto found = std::ranges::find_if(
            chunks, [&](const auto& chunk) { return chunk.x == cx && chunk.z == cz; });
        return found == chunks.end() ? nullptr : &*found;
    };
    const auto light_at = [&](int32_t x, int32_t y, int32_t z) -> std::array<uint8_t, 2> {
        const auto* chunk = chunk_at(x >> 4, z >> 4);
        if (chunk == nullptr) return {15, 0};
        const int32_t index = chunk->index_of(y);
        if (index < 0 || index >= static_cast<int32_t>(chunk->sections.size()) * 16) return {15, 0};
        return {chunk->sky_light_at(x & 15, index, z & 15),
                chunk->block_light_at(x & 15, index, z & 15)};
    };
    renderer.set_in_world([&](VkCommandBuffer command, const float* view_projection, uint32_t width,
                              uint32_t height, const float* fog, const mcpp::gpu::Camera& camera) {
        if (!entity_renderer) return;
        const double eye[3] = {camera.eye[0], camera.eye[1], camera.eye[2]};
        entity_renderer->record(command, view_projection, eye, width, height, fog, 0.0f, 0.0f);
    });

    std::cout << std::format(
        "{}: {} chunks received in {:.0f} ms, {} of them kept, {:.0f} MB resident\n", target,
        chunks.size(), receive_ms, chunks.size(), resident_mb());
    std::cout << std::format(
        "the recorded square is {} by {} chunks, centred on chunk {},{}, ground near {:.0f}\n",
        max_cx - min_cx + 1, max_cz - min_cz + 1, centre_cx, centre_cz, ground);
    std::cout << std::format(
        "{} entities and {} player entries came with it; the day starts at tick {}\n",
        scene.stats().tracked, tab.size() - 1, day_time);
    std::cout << "\nin every measured frame: the sky, fog and lightmap at the camera, the entity\n"
                 "scene's tick and pose, the hud's build, and the frame's recording and submit.\n"
                 "not in any of them: world generation, chunk streaming and meshing, the server's\n"
                 "packets, physics, block entities, particles, the hand and sound. The world is\n"
                 "meshed and uploaded before the flight, so a frame here is a frame of a client\n"
                 "that has nothing left to load - not a frame of a running game. cpu ms is this\n"
                 "thread's own cpu time, so the wait for the gpu is not in it; gpu ms comes from\n"
                 "timestamp queries around the frame.\n\n";

    std::cout << "flight                       rd  chunks  of want  drawn   ktris   avg fps  1% low  "
                 "0.1% low  p99 ms   cpu ms  gpu ms\n";
    std::vector<std::string> split_rows;
    std::vector<std::string> renderer_rows;
    double sky_total = 0.0;
    int flights_run = 0;
    for (const int32_t distance : distances) {
        // The chunks are sorted by their ring around the centre, so a render
        // distance is a prefix of them.
        const size_t held = static_cast<size_t>(std::ranges::count_if(chunks, [&](const auto& c) {
            return ring(c) <= distance;
        }));
        const size_t want = static_cast<size_t>(2 * distance + 1) * static_cast<size_t>(2 * distance + 1);
        if (held == 0) {
            std::cout << std::format("  -- render distance {}: no chunks around the centre\n",
                                     distance);
            continue;
        }
        double mesh_ms = 0.0;
        double unpack_ms = 0.0;
        {
            auto mesh = mesh_world(std::span(chunks).first(held), table, &mesh_ms, &unpack_ms);
            renderer.upload(mesh);
        }
        const double uploaded_mb = static_cast<double>(renderer.uploaded_bytes()) / 1e6;
        const float extent = static_cast<float>(2 * distance + 1) * 16.0f;
        const float centre[3] = {centre_x, ground, centre_z};
        for (const auto& flight : kFlights) {
            SkyClock clock;
            clock.day_time = day_time;
            std::vector<Frame> measured;
            measured.reserve(frames);
            std::vector<double> sky_ms;
            auto last = std::chrono::steady_clock::now();
            for (size_t frame = 0; frame < frames + 10; ++frame) {
                const auto frame_started = std::chrono::steady_clock::now();
                const double dt = std::chrono::duration<double>(frame_started - last).count();
                last = frame_started;
                const double cpu_started = cpu_ms_now();
                const double mark_started = mark_ms();
                Frame record;

                // Nothing streams in a replayed flight: the world is already
                // uploaded, and this stays zero on purpose.
                const double after_world = mark_ms();
                record.split.world = after_world - mark_started;

                mcpp::gpu::Camera camera{};
                flight.place(static_cast<float>(frame) / static_cast<float>(frames + 10), centre,
                             extent, camera);
                const double eye[3] = {camera.eye[0], camera.eye[1], camera.eye[2]};
                if (!clock.probed) {
                    clock.probe.reset(world_attributes_at(session, clock.day_time, eye, chunk_at,
                                                          session.weather()));
                    clock.probed = true;
                }
                clock.ticks += dt * 20.0;
                int ticked = 0;
                while (clock.ticks >= 1.0 && ticked < 10) {
                    clock.ticks -= 1.0;
                    ++ticked;
                    ++clock.game_time;
                    ++clock.day_time;
                    clock.probe.tick(world_attributes_at(session, clock.day_time, eye, chunk_at,
                                                         session.weather()));
                    clock.flick();
                    scene.tick();
                    gui.tick(hud_player, ui_options);
                }
                if (ticked == 10) clock.ticks = 0.0;
                clock.partial = static_cast<float>(clock.ticks);
                const double after_tick = mark_ms();
                record.split.tick = after_tick - after_world;

                const auto sky_started = mark_ms();
                auto in = world_atmosphere_inputs(session, camera, distance, session.weather());
                in.block_flicker = clock.flicker;
                in.rain_fog = clock.rain_fog.multiplier;
                renderer.set_atmosphere(mcpp::world::atmosphere(clock.probe, clock.partial, in));
                sky_ms.push_back(mark_ms() - sky_started);
                if (sky) sky->set_cloud_time(clock.game_time, clock.partial);
                renderer.set_texture_ticks(static_cast<uint64_t>(clock.game_time));
                if (entity_renderer) {
                    entity_renderer->begin();
                    mcpp::entity::View view;
                    for (int axis = 0; axis < 3; ++axis) {
                        view.eye[axis] = camera.eye[axis];
                        view.forward[axis] = camera.target[axis] - camera.eye[axis];
                    }
                    view.partial = clock.partial;
                    scene.draw(*entity_renderer, view, light_at);
                }
                quads.begin();
                {
                    mcpp::ui::GuiGraphics gfx(quads, gui_scale, gui_width, gui_height, &lang);
                    gui.render(gfx, hud_player, ui_options, hud_frame);
                }
                const double after_prepare = mark_ms();
                record.split.prepare = after_prepare - after_tick;

                record.gpu = renderer.draw(camera, [&](VkCommandBuffer command, uint32_t width,
                                                       uint32_t height) {
                    quads.record(command, width, height);
                });
                const double cpu_ended = cpu_ms_now();
                record.cost = renderer.last_frame_cost();
                record.cpu = cpu_ended - cpu_started;
                // The renderer is what the frame's cpu time has left once the
                // parts before it are taken off: it is the one part that waits,
                // and the wait must not count.
                record.split.renderer = std::max(0.0, record.cpu - record.split.world -
                                                          record.split.tick - record.split.prepare);
                record.wall = std::chrono::duration<double, std::milli>(
                                  std::chrono::steady_clock::now() - frame_started).count();
                if (frame >= 10) measured.push_back(record);
            }
            const size_t triangles = renderer.drawn_triangles();
            const size_t drawn = renderer.drawn_sections();
            std::ranges::sort(measured, [](const Frame& a, const Frame& b) { return a.wall < b.wall; });
            const auto walls = [&](size_t from, size_t to) {
                std::vector<double> out;
                for (size_t i = from; i < to; ++i) out.push_back(measured[i].wall);
                return out;
            };
            const auto slowest = [&](double fraction) {
                const size_t take = std::max<size_t>(1, static_cast<size_t>(fraction * measured.size()));
                return mean(walls(measured.size() - take, measured.size()));
            };
            const double p99 = measured[std::min(measured.size() - 1,
                                                 static_cast<size_t>(0.99 * static_cast<double>(measured.size())))]
                                   .wall;
            std::vector<double> cpu;
            std::vector<double> gpu;
            Split all;
            for (const auto& frame : measured) {
                cpu.push_back(frame.cpu);
                gpu.push_back(frame.gpu);
                all.add(frame.split);
            }
            all.divide(static_cast<double>(measured.size()));
            const size_t worst = std::max<size_t>(1, measured.size() / 100);
            Split tail;
            double tail_cpu = 0.0;
            for (size_t i = measured.size() - worst; i < measured.size(); ++i) {
                tail.add(measured[i].split);
                tail_cpu += measured[i].cpu;
            }
            tail.divide(static_cast<double>(worst));
            tail_cpu /= static_cast<double>(worst);

            std::vector<double> geometry;
            std::vector<double> submit;
            std::vector<double> sorting;
            std::vector<double> sort_upload;
            std::vector<double> sky_pass;
            std::vector<double> overlay_pass;
            for (const auto& frame : measured) {
                geometry.push_back(frame.cost.geometry_ms);
                submit.push_back(frame.cost.submit_ms + frame.cost.readback_ms);
                sorting.push_back(frame.cost.sort_ms);
                sort_upload.push_back(frame.cost.sort_upload_ms);
                sky_pass.push_back(frame.cost.sky_ms);
                overlay_pass.push_back(frame.cost.overlay_ms);
            }
            std::cout << std::format(
                "{:<28} {:>3} {:>7} {:>8} {:>6} {:>7} {:>9.0f} {:>7.0f} {:>9.0f} {:>7.3f} {:>8.3f} "
                "{:>7.3f}\n",
                flight.name, distance, held, want, drawn, triangles / 1000,
                1000.0 / mean(walls(0, measured.size())), 1000.0 / slowest(0.01),
                1000.0 / slowest(0.001), p99, mean(cpu), mean(gpu));
            renderer_rows.push_back(std::format(
                "{:<28} {:>3} {:>8.3f} {:>7.3f} {:>7.3f} {:>7.3f} {:>7.3f} {:>7.3f} {:>7.3f} "
                "{:>7.3f} {:>7}",
                flight.name, distance, all.renderer, mean(sorting), mean(sort_upload),
                mean(geometry), mean(sky_pass), mean(overlay_pass), mean(submit),
                all.renderer - mean(sorting) - mean(geometry) - mean(sky_pass) -
                    mean(overlay_pass) - mean(submit),
                drawn));
            split_rows.push_back(std::format(
                "{:<28} {:>3} {:>8.3f} {:>7.3f} {:>6.3f} {:>6.3f} {:>6.3f} | {:>8.3f} {:>7.3f} "
                "{:>6.3f} {:>6.3f} {:>6.3f}",
                flight.name, distance, mean(cpu), all.world, all.tick, all.prepare, all.renderer,
                tail_cpu, tail.world, tail.tick, tail.prepare, tail.renderer));
            sky_total += mean(sky_ms);
            ++flights_run;
        }
        std::cout << std::format(
            "  -- render distance {}: {} of the {} chunks a vanilla client holds, meshed in "
            "{:.0f} ms ({:.0f} ms of it unpacking, {:.2f} ms a chunk), {:.0f} MB uploaded\n",
            distance, held, want, mesh_ms, unpack_ms,
            mesh_ms / static_cast<double>(std::max<size_t>(held, 1)), uploaded_mb);
    }
    std::cout << "\nwhere that cpu time went, per frame, all frames then the slowest 1%:\n";
    std::cout << "flight                       rd   cpu ms   world   tick   prep    rend | "
                 "  cpu ms   world   tick   prep   rend\n";
    for (const auto& row : split_rows) std::cout << row << '\n';
    std::cout << "world is zero by construction: the bench uploads the world before the flight\n";
    std::cout << "\nand inside the renderer, per frame:\n";
    std::cout << "flight                       rd     rend    sort  of it:    geom     sky  "
                 "   hud  submit    rest   drawn\n";
    for (const auto& row : renderer_rows) std::cout << row << '\n';
    std::cout << "sort is the translucent quads re-sorted as the camera moves, and of it the\n"
                 "upload into the command buffer; geom the loop that culls the sections and records\n"
                 "a draw for each one; submit the queue submit and the timestamp readback; rest the\n"
                 "rest of the recording\n";
    std::cout << std::format("the sky, fog and lightmap at the camera: {:.4f} ms a frame\n",
                             sky_total / std::max(flights_run, 1));
    std::cout << std::format("resident after the flights: {:.0f} MB\n", resident_mb());
    session.close();
    return 0;
}

} // namespace client
