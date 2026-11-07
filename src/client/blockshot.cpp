#include "client/blockshot.hpp"

#include "client/common.hpp"

#include "mcpp/byte_buffer.hpp"
#include "mcpp/entity/assets.hpp"
#include "mcpp/entity/block_entities.hpp"
#include "mcpp/gpu/device.hpp"
#include "mcpp/gpu/entity_renderer.hpp"
#include "mcpp/gpu/world_renderer.hpp"
#include "mcpp/image_png.hpp"
#include "mcpp/protocol_versions.hpp"
#include "mcpp/session.hpp"
#include "mcpp/world/atlas.hpp"
#include "mcpp/world/block_table.hpp"
#include "mcpp/world/chunk.hpp"
#include "mcpp/world/mesher.hpp"
#include "mcpp/world/shapes.hpp"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <format>
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <vector>

namespace client {
namespace {

constexpr int32_t kProtocol = mcpp::protocol::kTargetProtocol;
constexpr uint32_t kWidth = 640;
constexpr uint32_t kHeight = 480;

using mcpp::nbt::Compound;
using mcpp::nbt::List;
using mcpp::nbt::Value;

Value text(std::string line) { return Value(Value::Storage(std::move(line))); }

Value compound(Compound fields) { return Value(Value::Storage(std::move(fields))); }

Value list(List items) { return Value(Value::Storage(std::move(items))); }

// A sign's one face, the way the server sends it: four components and the ink.
Value sign_face(std::vector<std::string> lines, std::string colour, bool glowing) {
    List messages;
    for (size_t i = 0; i < 4; ++i) {
        messages.push_back(text(i < lines.size() ? lines[i] : ""));
    }
    Compound face;
    face.emplace("messages", list(std::move(messages)));
    face.emplace("color", text(std::move(colour)));
    face.emplace("has_glowing_text", Value(Value::Storage(static_cast<int8_t>(glowing ? 1 : 0))));
    return compound(std::move(face));
}

Value banner_patterns(std::vector<std::pair<std::string, std::string>> layers) {
    List out;
    for (auto& [pattern, colour] : layers) {
        Compound one;
        one.emplace("pattern", text(pattern));
        one.emplace("color", text(colour));
        out.push_back(compound(std::move(one)));
    }
    return list(std::move(out));
}

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

// "minecraft:chest:facing=south,type=single,waterlogged=false" to a block state.
int32_t state_of(const mcpp::world::BlockShapes& shapes, const std::string& request) {
    const size_t colon = request.find(':', request.find(':') + 1);
    const std::string block = request.substr(0, colon);
    int32_t state = shapes.find(block);
    if (colon == std::string::npos) return state;
    std::string rest = request.substr(colon + 1);
    size_t at = 0;
    while (at <= rest.size()) {
        const size_t comma = rest.find(',', at);
        const std::string pair = rest.substr(at, comma - at);
        const size_t equals = pair.find('=');
        if (equals != std::string::npos) {
            const int32_t next = shapes.with(state, pair.substr(0, equals),
                                             pair.substr(equals + 1));
            if (next >= 0) state = next;
        }
        if (comma == std::string::npos) break;
        at = comma + 1;
    }
    return state;
}

struct Placed {
    int32_t x, y, z;
    int32_t state;
    Value data;
};

// One case: a name for the file, the blocks it puts down, and how open its
// lids are.
struct Case {
    std::string name;
    std::vector<Placed> blocks;
    int viewers = 0;
    int ticks = 0;
};

std::vector<Case> default_cases(const mcpp::world::BlockShapes& shapes) {
    const int32_t bx = 8, by = 1, bz = 8;
    const auto one = [&](std::string name, const std::string& request, Value data = {}) {
        return Case{std::move(name), {{bx, by, bz, state_of(shapes, request), std::move(data)}}};
    };
    std::vector<Case> cases;
    cases.push_back(one("chest", "minecraft:chest:facing=south,type=single,waterlogged=false"));
    Case open = one("chest-open", "minecraft:chest:facing=south,type=single,waterlogged=false");
    open.viewers = 1;
    open.ticks = 10;
    cases.push_back(std::move(open));
    Case half = one("chest-opening", "minecraft:chest:facing=south,type=single,waterlogged=false");
    half.viewers = 1;
    half.ticks = 4;
    cases.push_back(std::move(half));
    cases.push_back(one("ender-chest", "minecraft:ender_chest:facing=south,waterlogged=false"));
    cases.push_back(one("copper-chest", "minecraft:oxidized_copper_chest:facing=south,waterlogged=false"));

    Compound sign;
    sign.emplace("front_text", sign_face({"Plasma", "block entities", "chest, sign", "banner"},
                                         "black", false));
    sign.emplace("back_text", sign_face({"the other", "side"}, "red", true));
    cases.push_back(one("sign", "minecraft:oak_sign:rotation=0,waterlogged=false", compound(std::move(sign))));

    Compound hanging;
    hanging.emplace("front_text", sign_face({"hanging", "sign"}, "white", false));
    cases.push_back(one("hanging-sign", "minecraft:oak_hanging_sign:rotation=0,attached=false,waterlogged=false",
                        compound(std::move(hanging))));

    Case bed{"bed", {}};
    bed.blocks.push_back({bx, by, bz, state_of(shapes, "minecraft:red_bed:facing=south,part=foot,occupied=false"),
                          {}});
    bed.blocks.push_back({bx, by, bz - 1,
                          state_of(shapes, "minecraft:red_bed:facing=south,part=head,occupied=false"), {}});
    cases.push_back(std::move(bed));

    Compound banner;
    banner.emplace("patterns", banner_patterns({{"minecraft:stripe_bottom", "yellow"},
                                                {"minecraft:creeper", "lime"}}));
    cases.push_back(one("banner", "minecraft:white_banner:rotation=0",
                        compound(std::move(banner))));
    cases.push_back(one("shulker-box", "minecraft:purple_shulker_box:facing=up"));
    Case lid = one("shulker-box-open", "minecraft:purple_shulker_box:facing=up");
    lid.viewers = 1;
    lid.ticks = 10;
    cases.push_back(std::move(lid));
    cases.push_back(one("skull", "minecraft:skeleton_skull:rotation=0,powered=false"));
    cases.push_back(one("player-head", "minecraft:player_head:rotation=4,powered=false"));
    cases.push_back(one("dragon-head", "minecraft:dragon_head:rotation=0,powered=false"));
    cases.push_back(one("bell", "minecraft:bell:attachment=floor,facing=south,powered=false"));
    cases.push_back(one("conduit", "minecraft:conduit:waterlogged=false"));
    cases.push_back(one("decorated-pot", "minecraft:decorated_pot:facing=south,cracked=false,waterlogged=false"));
    cases.push_back(one("enchanting-table", "minecraft:enchanting_table"));
    cases.push_back(one("lectern", "minecraft:lectern:facing=south,has_book=true,powered=false"));
    cases.push_back(one("copper-golem-statue",
                        "minecraft:copper_golem_statue:facing=south,copper_golem_pose=standing,waterlogged=false"));
    return cases;
}

struct View {
    const char* what;
    float eye[3];
    float target[3];
};

} // namespace

int blockshot(const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cerr << "usage: blockshot <out-dir> [block[:property=value,...] ...]\n";
        return 2;
    }
    const std::filesystem::path out_dir = args[0];
    std::filesystem::create_directories(out_dir);

    const auto& shapes = mcpp::world::BlockShapes::game();
    mcpp::world::BlockTable table;
    mcpp::world::Atlas atlas;
    mcpp::entity::Assets assets;
    mcpp::entity::BlockEntityTable rows;
    if (!shapes.loaded() || !table.open(mcpp::world::BlockTable::default_path(kProtocol)) ||
        !atlas.open(mcpp::world::Atlas::default_path(kProtocol)) ||
        !assets.open(mcpp::entity::Assets::default_path(kProtocol)) ||
        !rows.open(mcpp::entity::BlockEntityTable::default_path(kProtocol))) {
        std::cerr << "block table, shapes, atlas, entity data or the block entity table is "
                     "missing; bake with tools/bake_all.py\n";
        return 1;
    }

    std::vector<Case> cases;
    if (args.size() > 1) {
        for (size_t i = 1; i < args.size(); ++i) {
            cases.push_back({args[i], {{8, 1, 8, state_of(shapes, args[i]), {}}}, 0, 0});
            for (char& c : cases.back().name) {
                if (c == ':' || c == ',' || c == '=') c = '-';
            }
        }
    } else {
        cases = default_cases(shapes);
    }

    mcpp::gpu::Device device;
    device.create({});
    mcpp::gpu::WorldRenderer world(device, kWidth, kHeight, atlas);
    mcpp::gpu::EntityRenderer renderer(device, world.colour_format());
    renderer.load(assets);
    world.set_in_world([&](VkCommandBuffer command, const float* view_projection, uint32_t width,
                           uint32_t height, const float* fog, const mcpp::gpu::Camera& camera) {
        const double eye[3] = {camera.eye[0], camera.eye[1], camera.eye[2]};
        renderer.record(command, view_projection, eye, width, height, fog, 0.0f, 0.0f);
    });
    const int32_t stone = shapes.find("minecraft:stone");

    for (const Case& one : cases) {
        std::vector<int32_t> states(4096, 0);
        const auto index = [](int32_t x, int32_t y, int32_t z) {
            return static_cast<size_t>(y) * 256 + static_cast<size_t>(z) * 16 +
                   static_cast<size_t>(x);
        };
        for (int32_t z = 0; z < 16; ++z) {
            for (int32_t x = 0; x < 16; ++x) states[index(x, 0, z)] = stone;
        }
        bool known = true;
        for (const Placed& block : one.blocks) {
            if (block.state <= 0) known = false;
            states[index(block.x, block.y, block.z)] = block.state;
        }
        if (!known) {
            std::cerr << std::format("no block for {}\n", one.name);
            continue;
        }
        mcpp::world::Chunk chunk;
        chunk.min_y = 0;
        chunk.sections.push_back(section_of(states));
        const mcpp::world::ChunkBlocks unpacked(chunk);
        world.upload(mcpp::world::mesh_chunk(chunk, unpacked, table, {}));

        mcpp::entity::BlockEntityScene scene;
        scene.load(assets, rows);
        for (const Placed& block : one.blocks) {
            scene.put(block.x, block.y, block.z, block.data);
            scene.event(block.x, block.y, block.z, 1, static_cast<uint8_t>(one.viewers));
        }
        for (int t = 0; t < one.ticks; ++t) scene.tick();

        const auto block_state = [&](int32_t x, int32_t y, int32_t z) -> int32_t {
            if (x < 0 || x > 15 || z < 0 || z > 15 || y < 0 || y > 15) return 0;
            return states[index(x, y, z)];
        };
        const auto light = [](int32_t, int32_t, int32_t) -> std::array<uint8_t, 2> {
            return {15, 0};
        };

        const float fx = 8.5f, fy = 1.0f, fz = 8.5f;
        const View views[] = {
            {"front", {fx, fy + 0.9f, fz + 2.6f}, {fx, fy + 0.5f, fz}},
            {"angle", {fx + 2.1f, fy + 1.9f, fz + 2.1f}, {fx, fy + 0.5f, fz}},
            {"back", {fx, fy + 0.8f, fz - 2.6f}, {fx, fy + 0.5f, fz}},
            {"side", {fx + 2.7f, fy + 0.7f, fz}, {fx, fy + 0.5f, fz}},
        };
        for (const View& view : views) {
            mcpp::gpu::Camera camera{};
            for (int axis = 0; axis < 3; ++axis) {
                camera.eye[axis] = view.eye[axis];
                camera.target[axis] = view.target[axis];
            }
            camera.fov_degrees = 50.0f;
            camera.near_plane = 0.05f;
            renderer.begin();
            mcpp::entity::View where;
            for (int axis = 0; axis < 3; ++axis) where.eye[axis] = camera.eye[axis];
            where.partial = 1.0f;
            scene.draw(renderer, where, block_state, light, 0);
            const auto pixels = world.render(camera);
            const auto path = out_dir / std::format("{}-{}.png", one.name, view.what);
            mcpp::write_png(path, kWidth, kHeight, pixels);
        }
        const auto& stats = scene.stats();
        std::cout << std::format("{}: {} block entities, {} models, {} parts, {:.3f} ms to place\n",
                                 one.name, stats.drawn, stats.models, stats.parts, stats.pose_ms);
    }
    device.save_pipeline_cache();
    return 0;
}

namespace {

using Clock = std::chrono::steady_clock;

} // namespace

// What a chunk with a lot of block entities costs in one frame: the time to
// place every model on the cpu, and the gpu time of the same frame with them
// and without.
int blockbench(const std::vector<std::string>& args) {
    const std::filesystem::path out_dir = args.empty() ? "." : args[0];
    std::filesystem::create_directories(out_dir);
    const auto& shapes = mcpp::world::BlockShapes::game();
    mcpp::world::BlockTable table;
    mcpp::world::Atlas atlas;
    mcpp::entity::Assets assets;
    mcpp::entity::BlockEntityTable rows;
    if (!shapes.loaded() || !table.open(mcpp::world::BlockTable::default_path(kProtocol)) ||
        !atlas.open(mcpp::world::Atlas::default_path(kProtocol)) ||
        !assets.open(mcpp::entity::Assets::default_path(kProtocol)) ||
        !rows.open(mcpp::entity::BlockEntityTable::default_path(kProtocol))) {
        std::cerr << "bake the client's data with tools/bake_all.py first\n";
        return 1;
    }
    const int32_t stone = shapes.find("minecraft:stone");
    // A mix, because a banner costs five models and a chest one.
    const int32_t kinds[] = {
        state_of(shapes, "minecraft:chest:facing=south,type=single,waterlogged=false"),
        state_of(shapes, "minecraft:oak_sign:rotation=0,waterlogged=false"),
        state_of(shapes, "minecraft:white_banner:rotation=0"),
        state_of(shapes, "minecraft:purple_shulker_box:facing=up"),
        state_of(shapes, "minecraft:skeleton_skull:rotation=0,powered=false"),
        state_of(shapes, "minecraft:decorated_pot:facing=south,cracked=false,waterlogged=false"),
    };

    mcpp::gpu::Device device;
    device.create({});
    mcpp::gpu::WorldRenderer world(device, kWidth, kHeight, atlas);
    mcpp::gpu::EntityRenderer renderer(device, world.colour_format());
    renderer.load(assets);
    bool draw_them = true;
    mcpp::entity::BlockEntityScene scene;
    scene.load(assets, rows);

    std::vector<int32_t> states(4096, 0);
    const auto index = [](int32_t x, int32_t y, int32_t z) {
        return static_cast<size_t>(y) * 256 + static_cast<size_t>(z) * 16 +
               static_cast<size_t>(x);
    };
    for (int32_t z = 0; z < 16; ++z) {
        for (int32_t x = 0; x < 16; ++x) states[index(x, 0, z)] = stone;
    }
    size_t placed = 0;
    Compound sign;
    sign.emplace("front_text", sign_face({"a chunk", "full of", "block", "entities"}, "black",
                                         false));
    const Value sign_data = compound(std::move(sign));
    for (int32_t layer = 0; layer < 3; ++layer) {
        for (int32_t z = 0; z < 16; z += 2) {
            for (int32_t x = 0; x < 16; x += 2) {
                const int32_t y = 1 + layer * 2;
                const int32_t state = kinds[placed % std::size(kinds)];
                states[index(x, y, z)] = state;
                scene.put(x, y, z, state == kinds[1] ? sign_data : Value{});
                ++placed;
            }
        }
    }
    mcpp::world::Chunk chunk;
    chunk.min_y = 0;
    chunk.sections.push_back(section_of(states));
    const mcpp::world::ChunkBlocks unpacked(chunk);
    world.upload(mcpp::world::mesh_chunk(chunk, unpacked, table, {}));

    const auto block_state = [&](int32_t x, int32_t y, int32_t z) -> int32_t {
        if (x < 0 || x > 15 || z < 0 || z > 15 || y < 0 || y > 15) return 0;
        return states[index(x, y, z)];
    };
    const auto light = [](int32_t, int32_t, int32_t) -> std::array<uint8_t, 2> {
        return {15, 0};
    };
    mcpp::gpu::Camera camera{};
    camera.eye[0] = 8.0;
    camera.eye[1] = 9.0;
    camera.eye[2] = 26.0;
    camera.target[0] = 8.0;
    camera.target[1] = 4.0;
    camera.target[2] = 8.0;
    camera.fov_degrees = 70.0f;
    camera.near_plane = 0.05f;
    mcpp::entity::View seen;
    for (int axis = 0; axis < 3; ++axis) seen.eye[axis] = camera.eye[axis];
    seen.partial = 0.5f;
    world.set_in_world([&](VkCommandBuffer command, const float* view_projection, uint32_t width,
                           uint32_t height, const float* fog, const mcpp::gpu::Camera& cam) {
        if (!draw_them) return;
        const double eye[3] = {cam.eye[0], cam.eye[1], cam.eye[2]};
        renderer.record(command, view_projection, eye, width, height, fog, 0.0f, 0.0f);
    });

    constexpr int kFrames = 200;
    double cpu = 0.0;
    for (int frame = 0; frame < kFrames; ++frame) {
        renderer.begin();
        const auto started = Clock::now();
        scene.draw(renderer, seen, block_state, light, frame);
        cpu += std::chrono::duration<double, std::milli>(Clock::now() - started).count();
    }
    const auto& stats = scene.stats();
    double with = 0.0;
    double without = 0.0;
    for (int pass = 0; pass < 16; ++pass) {
        renderer.begin();
        scene.draw(renderer, seen, block_state, light, pass);
        draw_them = true;
        world.render(camera);
        with += world.last_gpu_ms();
        draw_them = false;
        world.render(camera);
        without += world.last_gpu_ms();
    }
    draw_them = true;
    renderer.begin();
    scene.draw(renderer, seen, block_state, light, 0);
    const auto pixels = world.render(camera);
    const auto path = out_dir / "chunk-of-block-entities.png";
    mcpp::write_png(path, kWidth, kHeight, pixels);
    std::cout << std::format(
        "{} block entities in one chunk: {} models, {} parts, {:.3f} ms on the cpu to place\n",
        stats.drawn, stats.models, stats.parts, cpu / kFrames);
    std::cout << std::format("  gpu {:.3f} ms with them, {:.3f} ms without, {:.3f} ms for them\n",
                             with / 16.0, without / 16.0, (with - without) / 16.0);
    std::cout << std::format("{}\n", path.string());
    device.save_pipeline_cache();
    return 0;
}

// Everything the server says is there, drawn from where it says we stand: the
// chunks it sends meshed, the block entities it sends placed by their own
// renderers.
int blockserver(const std::vector<std::string>& args) {
    if (args.size() < 4) {
        std::cerr << "usage: blockserver <host[:port]> <name> <x,y,z> <out-dir> [seconds]\n";
        return 2;
    }
    const auto address = parse_address(args[0]);
    std::string username = args[1];
    double aim[3] = {0.0, 0.0, 0.0};
    {
        std::string rest = args[2];
        for (int axis = 0; axis < 3; ++axis) {
            const size_t comma = rest.find(',');
            aim[axis] = std::stod(rest.substr(0, comma));
            rest = comma == std::string::npos ? "" : rest.substr(comma + 1);
        }
    }
    const std::filesystem::path out_dir = args[3];
    std::filesystem::create_directories(out_dir);
    const double seconds = args.size() > 4 ? std::stod(args[4]) : 12.0;

    const auto& shapes = mcpp::world::BlockShapes::game();
    mcpp::world::BlockTable table;
    mcpp::world::Atlas atlas;
    mcpp::entity::Assets assets;
    mcpp::entity::BlockEntityTable rows;
    if (!shapes.loaded() || !table.open(mcpp::world::BlockTable::default_path(kProtocol)) ||
        !atlas.open(mcpp::world::Atlas::default_path(kProtocol)) ||
        !assets.open(mcpp::entity::Assets::default_path(kProtocol)) ||
        !rows.open(mcpp::entity::BlockEntityTable::default_path(kProtocol))) {
        std::cerr << "bake the client's data with tools/bake_all.py first\n";
        return 1;
    }

    mcpp::entity::BlockEntityScene scene;
    scene.load(assets, rows);
    std::map<std::pair<int32_t, int32_t>, mcpp::world::Chunk> chunks;
    double where[3] = {0.0, 0.0, 0.0};
    bool placed = false;

    mcpp::SessionOptions options;
    options.host = address.host;
    options.port = address.port;
    options.profile = signed_in_profile(username);
    options.username = username;
    mcpp::SessionEvents events;
    events.on_status = [](std::string_view line) { std::cout << "- " << line << '\n'; };
    events.player.on_position = [&](double x, double y, double z, float, float) {
        where[0] = x;
        where[1] = y;
        where[2] = z;
        placed = true;
    };
    events.world.on_chunk = [&](const mcpp::world::Chunk& chunk) {
        chunks[{chunk.x, chunk.z}] = chunk;
        mcpp::entity::scan_chunk(chunk, rows, scene);
        for (const mcpp::world::BlockEntity& one : chunk.block_entities) {
            scene.put(one.x, one.y, one.z, one.data);
        }
    };
    events.world.on_block_entity = [&](int32_t x, int32_t y, int32_t z, int32_t,
                                       const mcpp::nbt::Value& data) {
        scene.put(x, y, z, data);
    };
    events.world.on_block_change = [&](int32_t x, int32_t y, int32_t z, int32_t state) {
        const auto found = chunks.find({x >> 4, z >> 4});
        if (found == chunks.end()) return;
        found->second.set_block_state(x & 15, found->second.index_of(y), z & 15, state);
        if (rows.find(state) == nullptr) scene.removed(x, y, z);
        else scene.appear(x, y, z);
    };
    events.world.on_block_event = [&](int32_t x, int32_t y, int32_t z, uint8_t action,
                                      uint8_t parameter) {
        scene.event(x, y, z, action, parameter);
    };

    mcpp::Session session(options, events);
    session.connect();
    const auto joined = Clock::now();
    while (Clock::now() - joined < std::chrono::duration<double>(seconds)) {
        while (session.has_data()) {
            if (!session.pump()) break;
        }
        if (placed) session.send_position(where[0], where[1], where[2], 0.0f, 0.0f, true);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        scene.tick();
    }
    session.close();
    std::cout << std::format("- {} chunks, {} block entities from the server\n", chunks.size(),
                             scene.size());
    if (scene.size() == 0) {
        std::cerr << "the server sent no block entities\n";
        return 1;
    }

    mcpp::gpu::Device device;
    device.create({});
    mcpp::gpu::WorldRenderer world(device, kWidth, kHeight, atlas);
    mcpp::gpu::EntityRenderer renderer(device, world.colour_format());
    renderer.load(assets);
    world.set_in_world([&](VkCommandBuffer command, const float* view_projection, uint32_t width,
                           uint32_t height, const float* fog, const mcpp::gpu::Camera& camera) {
        const double eye[3] = {camera.eye[0], camera.eye[1], camera.eye[2]};
        renderer.record(command, view_projection, eye, width, height, fog, 0.0f, 0.0f);
    });
    // Only what the camera can see: a whole render distance of chunks is more
    // than this one picture needs on the gpu.
    const int32_t middle_x = static_cast<int32_t>(std::floor(aim[0])) >> 4;
    const int32_t middle_z = static_cast<int32_t>(std::floor(aim[2])) >> 4;
    std::erase_if(chunks, [&](const auto& entry) {
        return std::abs(entry.first.first - middle_x) > 4 ||
               std::abs(entry.first.second - middle_z) > 4;
    });
    std::map<std::pair<int32_t, int32_t>, mcpp::world::ChunkBlocks> unpacked;
    for (const auto& [key, chunk] : chunks) unpacked.emplace(key, mcpp::world::ChunkBlocks(chunk));
    // Section by section into one reservation, the way play streams them: the
    // merged form replaces whatever was there, which is one chunk at most.
    world.reserve(192u << 20, 80u << 20);
    for (const auto& [key, chunk] : chunks) {
        mcpp::world::Neighbours around;
        const auto at = [&](int32_t x, int32_t z) -> const mcpp::world::ChunkBlocks* {
            const auto found = unpacked.find({x, z});
            return found == unpacked.end() ? nullptr : &found->second;
        };
        around.west = at(key.first - 1, key.second);
        around.east = at(key.first + 1, key.second);
        around.north = at(key.first, key.second - 1);
        around.south = at(key.first, key.second + 1);
        if (!world.append(mcpp::world::mesh_sections(chunk, unpacked.at(key), table, around))) {
            std::cerr << "out of room for chunk meshes\n";
            break;
        }
    }

    std::cout << std::format("- {} chunks round the camera meshed\n", chunks.size());
    const auto block_state = [&](int32_t x, int32_t y, int32_t z) -> int32_t {
        const auto found = chunks.find({x >> 4, z >> 4});
        if (found == chunks.end()) return 0;
        return found->second.block_state(x & 15, found->second.index_of(y), z & 15);
    };
    // What the server said is there, close enough to be in the picture.
    scene.each([&](const mcpp::entity::BlockEntity& one) {
        if (std::abs(one.x - aim[0]) > 20.0 || std::abs(one.z - aim[2]) > 20.0) return;
        const mcpp::entity::BlockEntityRow* row = rows.find(block_state(one.x, one.y, one.z));
        std::cout << std::format("  {} {} {}  state {}  {}  {}\n", one.x, one.y, one.z,
                                 block_state(one.x, one.y, one.z),
                                 row == nullptr ? "no renderer" : rows.layer(row->model),
                                 one.data.is_null() ? "no data from the server"
                                                    : "data from the server");
    });
    const auto light = [&](int32_t x, int32_t y, int32_t z) -> std::array<uint8_t, 2> {
        const auto found = chunks.find({x >> 4, z >> 4});
        if (found == chunks.end()) return {15, 0};
        const int32_t index = found->second.index_of(y);
        return {found->second.sky_light_at(x & 15, index, z & 15),
                found->second.block_light_at(x & 15, index, z & 15)};
    };

    const auto fx = static_cast<float>(aim[0]);
    const auto fy = static_cast<float>(aim[1]);
    const auto fz = static_cast<float>(aim[2]);
    const View views[] = {
        {"front", {fx, fy + 1.6f, fz + 6.0f}, {fx, fy + 1.0f, fz}},
        {"angle", {fx + 5.0f, fy + 3.5f, fz + 5.0f}, {fx, fy + 1.0f, fz}},
        {"close", {fx, fy + 1.2f, fz + 2.2f}, {fx, fy + 0.8f, fz}},
    };
    double total = 0.0;
    for (const View& view : views) {
        mcpp::gpu::Camera camera{};
        for (int axis = 0; axis < 3; ++axis) {
            camera.eye[axis] = view.eye[axis];
            camera.target[axis] = view.target[axis];
        }
        camera.fov_degrees = 70.0f;
        camera.near_plane = 0.05f;
        renderer.begin();
        mcpp::entity::View seen;
        for (int axis = 0; axis < 3; ++axis) seen.eye[axis] = camera.eye[axis];
        seen.partial = 1.0f;
        scene.draw(renderer, seen, block_state, light, 0);
        const auto pixels = world.render(camera);
        const auto path = out_dir / std::format("server-{}.png", view.what);
        mcpp::write_png(path, kWidth, kHeight, pixels);
        const auto& stats = scene.stats();
        total += stats.pose_ms;
        std::cout << std::format("{}: {} of {} block entities drawn, {} models, {} parts, "
                                 "{:.3f} ms to place, {:.3f} ms gpu\n",
                                 path.string(), stats.drawn, stats.tracked, stats.models,
                                 stats.parts, stats.pose_ms, world.last_gpu_ms());
    }
    std::cout << std::format("- {:.3f} ms on the cpu to place them, averaged over {} frames\n",
                             total / 3.0, 3);
    device.save_pipeline_cache();
    return 0;
}

} // namespace client
