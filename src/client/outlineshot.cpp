#include "client/outlineshot.hpp"

#include "mcpp/byte_buffer.hpp"
#include "mcpp/gpu/device.hpp"
#include "mcpp/gpu/outline.hpp"
#include "mcpp/gpu/world_renderer.hpp"
#include "mcpp/image_png.hpp"
#include "mcpp/protocol_versions.hpp"
#include "mcpp/world/atlas.hpp"
#include "mcpp/world/block_table.hpp"
#include "mcpp/world/chunk.hpp"
#include "mcpp/world/mesher.hpp"
#include "mcpp/world/render_table.hpp"
#include "mcpp/world/shape_edges.hpp"
#include "mcpp/world/shapes.hpp"

#include <filesystem>
#include <format>
#include <iostream>
#include <string_view>
#include <vector>

namespace client {
namespace {

mcpp::world::ChunkSection section_of(const std::vector<int32_t>& states) {
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

} // namespace

// The outline the game draws around the block you are looking at, on the
// blocks whose shape is not a full cube, from in front and from above.
int outlineshot(const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cerr << "usage: outlineshot <out-dir> [block[:property=value,...] ...]\n";
        return 2;
    }
    const std::filesystem::path out_dir = args[0];
    std::filesystem::create_directories(out_dir);
    const auto protocol = mcpp::protocol::kTargetProtocol;
    const auto& shapes = mcpp::world::BlockShapes::game();
    mcpp::world::BlockTable table;
    mcpp::world::Atlas atlas;
    if (!shapes.loaded() || !table.open(mcpp::world::BlockTable::default_path(protocol)) ||
        !atlas.open(mcpp::world::Atlas::default_path(protocol))) {
        std::cerr << "block table, shapes or atlas missing; bake with tools/bake_all.py\n";
        return 1;
    }

    // `--through=<block>`: a wall of that block between the camera and the
    // aimed one, which is how the pass the outline belongs to becomes visible.
    std::string through;
    std::vector<std::string> wanted;
    for (const auto& arg : std::vector<std::string>(args.begin() + 1, args.end())) {
        if (arg.starts_with("--through=")) {
            through = arg.substr(std::string_view("--through=").size());
        } else {
            wanted.push_back(arg);
        }
    }
    mcpp::world::RenderTable render;
    render.open(mcpp::world::RenderTable::default_path(protocol));
    if (wanted.empty()) {
        wanted = {"minecraft:snow:layers=1",
                  "minecraft:snow:layers=4",
                  "minecraft:snow:layers=8",
                  "minecraft:oak_slab:type=bottom,waterlogged=false",
                  "minecraft:oak_stairs:facing=east,half=bottom,shape=straight,waterlogged=false",
                  "minecraft:oak_fence:north=true,south=true,east=false,west=false,waterlogged=false",
                  "minecraft:cobblestone_wall:north=low,south=low,east=none,west=none,up=true,waterlogged=false",
                  "minecraft:white_carpet",
                  "minecraft:glass_pane:north=true,south=true,east=false,west=false,waterlogged=false",
                  "minecraft:oak_trapdoor:facing=north,half=bottom,open=false,powered=false,waterlogged=false"};
    }

    mcpp::gpu::Device device;
    mcpp::gpu::Device::Options gpu_options;
    device.create(gpu_options);
    constexpr uint32_t kWidth = 800;
    constexpr uint32_t kHeight = 600;
    mcpp::gpu::WorldRenderer renderer(device, kWidth, kHeight, atlas);
    mcpp::gpu::Outline outline(device, renderer.colour_format());
    renderer.set_outline(&outline);

    const int32_t stone = shapes.find("minecraft:stone");
    for (const auto& request : wanted) {
        const auto colon = request.find(':', request.find(':') + 1);
        const std::string block = request.substr(0, colon);
        std::vector<std::pair<std::string, std::string>> properties;
        if (colon != std::string::npos) {
            std::string rest = request.substr(colon + 1);
            size_t at = 0;
            while (at <= rest.size()) {
                const size_t comma = rest.find(',', at);
                const std::string pair = rest.substr(at, comma - at);
                const size_t equals = pair.find('=');
                if (equals != std::string::npos) {
                    properties.emplace_back(pair.substr(0, equals), pair.substr(equals + 1));
                }
                if (comma == std::string::npos) break;
                at = comma + 1;
            }
        }
        int32_t state = shapes.find(block);
        for (const auto& [key, value] : properties) {
            const int32_t next = shapes.with(state, key, value);
            if (next >= 0) state = next;
        }
        if (state <= 0) {
            std::cerr << std::format("no block called {}\n", block);
            continue;
        }

        // A stone floor, the block on top of it in the middle.
        constexpr int32_t bx = 8;
        constexpr int32_t by = 1;
        constexpr int32_t bz = 8;
        std::vector<int32_t> states(4096, 0);
        const auto index = [](int32_t x, int32_t y, int32_t z) {
            return static_cast<size_t>(y) * 256 + static_cast<size_t>(z) * 16 +
                   static_cast<size_t>(x);
        };
        for (int32_t z = 0; z < 16; ++z) {
            for (int32_t x = 0; x < 16; ++x) states[index(x, 0, z)] = stone;
        }
        states[index(bx, by, bz)] = state;
        if (!through.empty()) {
            const int32_t wall = shapes.find(through);
            if (wall > 0) {
                for (int32_t x = bx - 1; x <= bx + 1; ++x) {
                    for (int32_t y = by; y <= by + 1; ++y) states[index(x, y, bz + 2)] = wall;
                }
            }
        }
        mcpp::world::Chunk chunk;
        chunk.min_y = 0;
        chunk.sections.push_back(section_of(states));
        const mcpp::world::ChunkBlocks unpacked(chunk);
        renderer.upload(mcpp::world::mesh_chunk(chunk, unpacked, table, {}));

        std::vector<float> edges;
        const uint16_t shape = shapes.state(state).outline;
        mcpp::world::for_all_edges(shapes, shape,
                                   [&](double x0, double y0, double z0, double x1, double y1, double z1) {
                                       for (const double v : {x0, y0, z0, x1, y1, z1}) {
                                           edges.push_back(static_cast<float>(v));
                                       }
                                   });
        const auto range = outline.add(shape, edges);
        const int32_t block_pos[3] = {bx, by, bz};
        const mcpp::world::RenderLayer layer = render.at(state).layer;
        renderer.set_outlined_block(block_pos, range.count != 0,
                                    layer == mcpp::world::RenderLayer::Translucent ||
                                        layer == mcpp::world::RenderLayer::Tripwire);

        const std::string name(shapes.name(state));
        const auto tidy = [&] {
            std::string out = name.substr(name.find(':') + 1);
            for (const auto& [key, value] : properties) out += "-" + value;
            return out;
        }();
        struct View {
            const char* what;
            float eye[3];
            float target[3];
            float fov;
        };
        const View views[] = {
            {"front", {bx + 0.5f, by + 0.7f, bz + 3.6f}, {bx + 0.5f, by + 0.4f, bz + 0.5f}, 45.0f},
            {"above", {bx + 2.3f, by + 2.6f, bz + 2.7f}, {bx + 0.5f, by + 0.3f, bz + 0.5f}, 45.0f},
            {"side", {bx + 3.5f, by + 0.9f, bz + 0.6f}, {bx + 0.5f, by + 0.4f, bz + 0.5f}, 45.0f},
        };
        for (const auto& view : views) {
            mcpp::gpu::Camera camera{};
            for (int i = 0; i < 3; ++i) {
                camera.eye[i] = view.eye[i];
                camera.target[i] = view.target[i];
            }
            camera.fov_degrees = view.fov;
            const double corner[3] = {static_cast<double>(bx) - camera.eye[0],
                                      static_cast<double>(by) - camera.eye[1],
                                      static_cast<double>(bz) - camera.eye[2]};
            outline.aim(range, corner, camera.eye, kWidth, kHeight);
            const auto pixels = renderer.render(camera);
            const auto path = out_dir / std::format("{}-{}.png", tidy, view.what);
            mcpp::write_png(path, kWidth, kHeight, pixels);
            std::cout << std::format("{}  state {}, {} boxes, {} edges\n", path.string(), state,
                                     shapes.boxes(shape).size(), range.count);
        }
    }
    device.save_pipeline_cache();
    return 0;
}

} // namespace client
