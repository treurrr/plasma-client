// Offscreen pictures of a block being broken, and what the particles cost.
// No server and no window: one section of blocks built here, meshed, and drawn
// through the same renderers the window uses, with the crack over a block at a
// given stage and a frame of the particles a break throws.
#include "client/breakshot.hpp"

#include "mcpp/byte_buffer.hpp"
#include "mcpp/gpu/particles.hpp"
#include "mcpp/gpu/world_renderer.hpp"
#include "mcpp/image_png.hpp"
#include "mcpp/protocol_versions.hpp"
#include "mcpp/world/atlas.hpp"
#include "mcpp/world/atmosphere.hpp"
#include "mcpp/world/block_table.hpp"
#include "mcpp/world/destruction.hpp"
#include "mcpp/world/mesher.hpp"
#include "mcpp/world/particles.hpp"

#include <chrono>
#include <cmath>
#include <format>
#include <iostream>
#include <vector>

namespace client {
namespace {

namespace w = mcpp::world;

w::ChunkSection section_of(const std::vector<int32_t>& states) {
    constexpr uint8_t kBits = 15;
    constexpr size_t kPerLong = 64 / kBits;
    mcpp::ByteBuffer buffer;
    buffer.write_u8(kBits);
    for (size_t index = 0; index * kPerLong < states.size(); ++index) {
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
    w::ChunkSection section;
    section.non_air_count = 4096;
    section.blocks = w::PalettedContainer::read(buffer, 4096, 8);
    section.biomes = w::PalettedContainer::read(biomes, 64, 3);
    return section;
}

// The game's yaw and pitch for a camera looking from its eye at its target.
void angles_of(const mcpp::gpu::Camera& camera, float& yaw, float& pitch) {
    const float dx = camera.target[0] - camera.eye[0];
    const float dy = camera.target[1] - camera.eye[1];
    const float dz = camera.target[2] - camera.eye[2];
    const auto flat = static_cast<float>(std::sqrt(static_cast<double>(dx * dx + dz * dz)));
    yaw = static_cast<float>(std::atan2(-dx, dz) * 180.0 / 3.141592653589793);
    pitch = static_cast<float>(std::atan2(-dy, flat) * 180.0 / 3.141592653589793);
}

w::Atmosphere noon() {
    w::Atmosphere sky;
    sky.fog_colour[0] = 0.7529f;
    sky.fog_colour[1] = 0.8471f;
    sky.fog_colour[2] = 1.0f;
    sky.fog_colour[3] = 1.0f;
    sky.sky_colour = 0xFF78A7FFu;
    sky.render_fog_start = 1000.0f;
    sky.render_fog_end = 2000.0f;
    w::fill_lightmap(sky);
    return sky;
}

} // namespace

int breakshot(const std::string& out_prefix) {
    const auto protocol = mcpp::protocol::kTargetProtocol;
    w::BlockTable table;
    w::Atlas atlas;
    w::ParticleTable particles;
    if (!table.open(w::BlockTable::default_path(protocol)) || !atlas.open(w::Atlas::default_path(protocol))) {
        std::cerr << "block table or atlas missing; bake them with tools/bake_all.py\n";
        return 1;
    }
    if (!particles.open(w::ParticleTable::default_path(protocol), table)) {
        std::cerr << "no particle table; bake one with tools/bake_particles.py\n";
        return 1;
    }
    const auto& shapes = w::BlockShapes::game();

    // A stone floor with four blocks standing on it: stone, a slab, a stair
    // and a fence, so the crack is seen over a full cube and over three
    // shapes that are not one.
    const auto index = [](int32_t x, int32_t y, int32_t z) {
        return static_cast<size_t>(y) * 256 + static_cast<size_t>(z) * 16 + static_cast<size_t>(x);
    };
    const int32_t stone = shapes.find("minecraft:stone");
    const int32_t slab = shapes.find("minecraft:oak_slab", {{"type", "bottom"}, {"waterlogged", "false"}});
    const int32_t stair = shapes.find("minecraft:oak_stairs",
                                      {{"facing", "south"}, {"half", "bottom"}, {"shape", "straight"}, {"waterlogged", "false"}});
    const int32_t fence = shapes.find("minecraft:oak_fence", {{"waterlogged", "false"}});
    const int32_t standing[4] = {stone, slab, stair, fence};
    std::vector<int32_t> states(4096, 0);
    for (int32_t z = 0; z < 16; ++z) {
        for (int32_t x = 0; x < 16; ++x) states[index(x, 0, z)] = stone;
    }
    const int32_t block_x[4] = {5, 7, 9, 11};
    for (int i = 0; i < 4; ++i) states[index(block_x[i], 1, 8)] = standing[i];

    w::Chunk chunk;
    chunk.min_y = 0;
    chunk.sections.push_back(section_of(states));
    const w::ChunkBlocks unpacked(chunk);
    const auto mesh = w::mesh_chunk(chunk, unpacked, table, {});

    const w::BlockAt at = [&](int32_t x, int32_t y, int32_t z) -> int32_t {
        if (x < 0 || x > 15 || z < 0 || z > 15 || y < 0 || y > 15) return 0;
        return states[index(x, y, z)];
    };
    const w::ParticleLevel level{at, shapes, particles,
                                 [](int32_t, int32_t, int32_t) { return 15 << 4 | 15 << 20; },
                                 [](w::ParticleTable::Colour, int32_t, int32_t, int32_t) { return 0x91BD59u; }, 0};

    mcpp::gpu::Device device;
    mcpp::gpu::Device::Options options;
    options.validation = true;
    device.create(options);
    constexpr uint32_t kWidth = 1280;
    constexpr uint32_t kHeight = 720;
    mcpp::gpu::WorldRenderer renderer(device, kWidth, kHeight, atlas);
    renderer.upload(mesh);
    renderer.set_atmosphere(noon());
    mcpp::gpu::ParticleRenderer drawer(device, renderer, w::ParticleTable::cracks_path(protocol));
    if (!drawer.has_cracks()) {
        std::cerr << "no crack textures; bake them with tools/bake_particles.py\n";
        return 1;
    }
    renderer.set_after_entities([&](VkCommandBuffer command, const float* view_projection, uint32_t, uint32_t,
                                    const float*, const mcpp::gpu::Camera& camera) {
        drawer.record_cracks(command, view_projection, camera);
    });
    renderer.set_after_translucent([&](VkCommandBuffer command, const float* view_projection, uint32_t, uint32_t,
                                       const float*, const mcpp::gpu::Camera& camera) {
        drawer.record_particles(command, view_projection, camera);
    });

    mcpp::gpu::Camera camera{};
    camera.eye[0] = 8.0f;
    camera.eye[1] = 2.6f;
    camera.eye[2] = 13.5f;
    camera.target[0] = 8.0f;
    camera.target[1] = 1.7f;
    camera.target[2] = 8.5f;
    camera.fov_degrees = 50.0f;
    const double eye[3] = {camera.eye[0], camera.eye[1], camera.eye[2]};

    w::BlockDestruction destruction;
    std::vector<w::CrackVertex> cracks;
    std::vector<w::BlockDestruction::Shown> shown;
    const auto crack_frame = [&](int32_t stage) {
        destruction.clear();
        for (int i = 0; i < 4; ++i) {
            const int32_t pos[3] = {block_x[i], 1, 8};
            destruction.progress(100 + i, pos, stage);
        }
        destruction.shown(eye, shown);
        cracks.clear();
        for (const auto& s : shown) {
            static constexpr int kStep[6][3] = {{0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}};
            const int32_t state = at(s.pos[0], s.pos[1], s.pos[2]);
            bool hidden[6];
            for (int f = 0; f < 6; ++f) {
                const int32_t n = at(s.pos[0] + kStep[f][0], s.pos[1] + kStep[f][1], s.pos[2] + kStep[f][2]);
                hidden[f] = n > 0 && table.occludes(n);
            }
            const double offset[3] = {0.0, 0.0, 0.0};
            w::crack_quads(table, state, s.pos, offset, hidden, eye, static_cast<uint32_t>(s.stage), cracks);
        }
    };

    for (const int32_t stage : {3, 6, 9}) {
        crack_frame(stage);
        drawer.set_frame({}, cracks, eye);
        const auto pixels = renderer.render(camera);
        const auto path = std::format("{}-stage{}.png", out_prefix, stage);
        mcpp::write_png(path, kWidth, kHeight, pixels);
        std::cout << std::format("{}: {} crack corners over {} blocks\n", path, cracks.size(), shown.size());
    }

    // A close look at one stone block cracked through, filling the frame.
    camera.eye[0] = 5.5f;
    camera.eye[1] = 1.9f;
    camera.eye[2] = 10.2f;
    camera.target[0] = 5.5f;
    camera.target[1] = 1.6f;
    camera.target[2] = 8.5f;
    camera.fov_degrees = 30.0f;
    const double close[3] = {camera.eye[0], camera.eye[1], camera.eye[2]};
    for (const int32_t stage : {3, 6, 9}) {
        destruction.clear();
        const int32_t pos[3] = {block_x[0], 1, 8};
        destruction.progress(1, pos, stage);
        destruction.shown(close, shown);
        cracks.clear();
        bool hidden[6] = {true, false, false, false, false, false};
        const double offset[3] = {0.0, 0.0, 0.0};
        w::crack_quads(table, at(pos[0], pos[1], pos[2]), pos, offset, hidden, close,
                       static_cast<uint32_t>(stage), cracks);
        drawer.set_frame({}, cracks, close);
        const auto pixels = renderer.render(camera);
        const auto path = std::format("{}-close-stage{}.png", out_prefix, stage);
        mcpp::write_png(path, kWidth, kHeight, pixels);
        std::cout << std::format("{}: {} crack corners\n", path, cracks.size());
    }

    // The particles a break throws, with the blocks gone the way they are
    // when the bits appear, three ticks on so they have spread and some are
    // already bouncing off the floor.
    camera.eye[0] = 8.0f;
    camera.eye[1] = 2.6f;
    camera.eye[2] = 13.5f;
    camera.target[0] = 8.0f;
    camera.target[1] = 1.7f;
    camera.target[2] = 8.5f;
    camera.fov_degrees = 50.0f;
    w::ParticleEngine engine;
    int64_t seed = 0x5DEECE66Dull;
    const auto broken = [&] {
        auto gone = states;
        for (const int32_t x : block_x) gone[index(x, 1, 8)] = 0;
        w::Chunk empty;
        empty.min_y = 0;
        empty.sections.push_back(section_of(gone));
        const w::ChunkBlocks unpacked_empty(empty);
        return w::mesh_chunk(empty, unpacked_empty, table, {});
    }();
    engine.next_seed = [&] { return seed += 0x9E3779B97F4A7C15ull; };
    for (int i = 0; i < 4; ++i) {
        const int32_t pos[3] = {block_x[i], 1, 8};
        engine.destroy(level, pos, at(pos[0], pos[1], pos[2]));
    }
    for (int i = 0; i < 4; ++i) engine.tick(level);
    renderer.upload(broken);
    for (const int32_t x : block_x) states[index(x, 1, 8)] = 0;
    w::ParticleView view{};
    std::copy(eye, eye + 3, view.camera);
    float shot_yaw = 0.0f;
    float shot_pitch = 0.0f;
    angles_of(camera, shot_yaw, shot_pitch);
    w::camera_rotation(shot_yaw, shot_pitch, view.rotation);
    for (int i = 0; i < 3; ++i) view.apex[i] = eye[i];
    // Everything in front of the camera; the pictures are of the particles,
    // not of the culling, which the oracle and the timings cover.
    view.planes[0][1] = 1.0f;
    view.planes[0][3] = 1000.0f;
    view.planes[1][1] = -1.0f;
    view.planes[1][3] = 1000.0f;
    view.planes[2][0] = 1.0f;
    view.planes[2][3] = 1000.0f;
    view.planes[3][0] = -1.0f;
    view.planes[3][3] = 1000.0f;
    view.planes[4][2] = -1.0f;
    view.planes[4][3] = 1000.0f;
    std::vector<w::ParticleQuad> quads;
    engine.extract(view, level, quads);
    destruction.clear();
    drawer.set_frame(quads, {}, eye);
    {
        const auto pixels = renderer.render(camera);
        const auto path = std::format("{}-particles.png", out_prefix);
        mcpp::write_png(path, kWidth, kHeight, pixels);
        std::cout << std::format("{}: {} particles alive, {} quads drawn\n", path, engine.count(), quads.size());
    }

    // The same particles from above and to the side: a quad that still faces
    // the camera is facing it for the right reason.
    {
        mcpp::gpu::Camera over = camera;
        over.eye[0] = 12.5f;
        over.eye[1] = 6.0f;
        over.eye[2] = 13.0f;
        over.target[0] = 8.0f;
        over.target[1] = 1.2f;
        over.target[2] = 8.5f;
        const double from[3] = {over.eye[0], over.eye[1], over.eye[2]};
        std::copy(from, from + 3, view.camera);
        std::copy(from, from + 3, view.apex);
        angles_of(over, shot_yaw, shot_pitch);
        w::camera_rotation(shot_yaw, shot_pitch, view.rotation);
        engine.extract(view, level, quads);
        drawer.set_frame(quads, {}, from);
        const auto pixels = renderer.render(over);
        const auto path = std::format("{}-particles-over.png", out_prefix);
        mcpp::write_png(path, kWidth, kHeight, pixels);
        std::cout << std::format("{}: {} quads from above\n", path, quads.size());
        std::copy(eye, eye + 3, view.camera);
        std::copy(eye, eye + 3, view.apex);
        angles_of(camera, shot_yaw, shot_pitch);
        w::camera_rotation(shot_yaw, shot_pitch, view.rotation);
    }

    // What the particles cost, on the cpu and on the gpu, at a thousand and at
    // ten thousand: a tick of all of them, the extract that makes the quads,
    // and the frame that draws them.
    for (const size_t wanted : {size_t{1000}, size_t{10000}}) {
        engine.clear();
        seed = 0x5DEECE66Dull;
        // The same particles a break throws, but living long enough to be
        // timed: the pool is what costs, not how it was filled.
        for (size_t made = 0; made < wanted; ++made) {
            const int32_t pos[3] = {block_x[made % 4], 1, 8};
            const double fx = static_cast<double>(made % 7) / 7.0;
            const double fz = static_cast<double>(made % 11) / 11.0;
            auto p = w::terrain_particle(level, seed += 0x9E3779B97F4A7C15ull, pos[0] + fx, pos[1] + 0.5,
                                         pos[2] + fz, fx - 0.5, 0.5, fz - 0.5, at(pos[0], pos[1], pos[2]), pos);
            p.lifetime = 1000000;
            engine.add(p);
        }
        engine.tick(level);
        constexpr int kRuns = 60;
        auto started = std::chrono::steady_clock::now();
        for (int i = 0; i < kRuns; ++i) engine.tick(level);
        const double tick_ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count() / kRuns;
        const size_t alive = engine.count();
        started = std::chrono::steady_clock::now();
        for (int i = 0; i < kRuns; ++i) engine.extract(view, level, quads);
        const double extract_ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count() / kRuns;
        drawer.set_frame(quads, {}, eye);
        double gpu_ms = 0.0;
        double frame_ms = 0.0;
        for (int i = 0; i < kRuns; ++i) {
            started = std::chrono::steady_clock::now();
            renderer.draw(camera);
            frame_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
            renderer.read_timestamps();
            gpu_ms += renderer.last_gpu_ms();
        }
        std::cout << std::format(
            "- {:>5} particles ({} quads): tick {:.3f} ms, extract {:.3f} ms, frame {:.3f} ms cpu, "
            "{:.3f} ms gpu\n",
            alive, quads.size(), tick_ms, extract_ms, frame_ms / kRuns, gpu_ms / kRuns);
    }

    // The same frame without a particle in it, so the lines above can be read
    // against what the world alone costs.
    drawer.set_frame({}, {}, eye);
    double empty_gpu = 0.0;
    for (int i = 0; i < 60; ++i) {
        renderer.draw(camera);
        renderer.read_timestamps();
        empty_gpu += renderer.last_gpu_ms();
    }
    std::cout << std::format("-     0 particles: {:.3f} ms gpu\n", empty_gpu / 60.0);
    device.save_pipeline_cache();
    return 0;
}

} // namespace client
