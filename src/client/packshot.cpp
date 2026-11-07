#include "client/packshot.hpp"

#include "client/common.hpp"

#include "mcpp/byte_buffer.hpp"
#include "mcpp/gpu/device.hpp"
#include "mcpp/gpu/ui.hpp"
#include "mcpp/gpu/world_renderer.hpp"
#include "mcpp/image_png.hpp"
#include "mcpp/packs/pack_system.hpp"
#include "mcpp/protocol_versions.hpp"
#include "mcpp/session.hpp"
#include "mcpp/ui/gui_graphics.hpp"
#include "mcpp/ui/items.hpp"
#include "mcpp/world/atlas.hpp"
#include "mcpp/world/block_table.hpp"
#include "mcpp/world/chunk.hpp"
#include "mcpp/world/mesher.hpp"
#include "mcpp/world/shapes.hpp"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <vector>
#include <zlib.h>

namespace client {
namespace {

constexpr int32_t kProtocol = mcpp::protocol::kTargetProtocol;
constexpr uint32_t kWidth = 640;
constexpr uint32_t kHeight = 480;
using Clock = std::chrono::steady_clock;

// A zip written by hand. The demo pack is built here rather than committed,
// so the proof needs no asset in the tree and nothing from the game's jar.
class ZipWriter {
public:
    void add(const std::string& name, const std::vector<uint8_t>& bytes) {
        Entry entry;
        entry.name = name;
        entry.crc = static_cast<uint32_t>(crc32(0, bytes.data(),
                                                static_cast<unsigned>(bytes.size())));
        entry.size = bytes.size();
        entry.offset = body_.size();
        put32(body_, 0x04034b50);
        put16(body_, 20);
        put16(body_, 0);
        put16(body_, 0);
        put16(body_, 0);
        put16(body_, 0);
        put32(body_, entry.crc);
        put32(body_, static_cast<uint32_t>(entry.size));
        put32(body_, static_cast<uint32_t>(entry.size));
        put16(body_, static_cast<uint16_t>(name.size()));
        put16(body_, 0);
        body_.insert(body_.end(), name.begin(), name.end());
        body_.insert(body_.end(), bytes.begin(), bytes.end());
        entries_.push_back(std::move(entry));
    }

    void add_text(const std::string& name, std::string_view text) {
        add(name, std::vector<uint8_t>(text.begin(), text.end()));
    }

    std::vector<uint8_t> finish() const {
        std::vector<uint8_t> out = body_;
        const size_t directory_at = out.size();
        for (const Entry& entry : entries_) {
            put32(out, 0x02014b50);
            put16(out, 20);
            put16(out, 20);
            put16(out, 0);
            put16(out, 0);
            put16(out, 0);
            put16(out, 0);
            put32(out, entry.crc);
            put32(out, static_cast<uint32_t>(entry.size));
            put32(out, static_cast<uint32_t>(entry.size));
            put16(out, static_cast<uint16_t>(entry.name.size()));
            put16(out, 0);
            put16(out, 0);
            put16(out, 0);
            put16(out, 0);
            put32(out, 0);
            put32(out, static_cast<uint32_t>(entry.offset));
            out.insert(out.end(), entry.name.begin(), entry.name.end());
        }
        put32(out, 0x06054b50);
        put16(out, 0);
        put16(out, 0);
        put16(out, static_cast<uint16_t>(entries_.size()));
        put16(out, static_cast<uint16_t>(entries_.size()));
        put32(out, static_cast<uint32_t>(out.size() - directory_at));
        put32(out, static_cast<uint32_t>(directory_at));
        put16(out, 0);
        return out;
    }

private:
    struct Entry {
        std::string name;
        uint32_t crc = 0;
        size_t size = 0;
        size_t offset = 0;
    };
    static void put16(std::vector<uint8_t>& out, uint16_t value) {
        out.push_back(static_cast<uint8_t>(value & 0xFF));
        out.push_back(static_cast<uint8_t>(value >> 8));
    }
    static void put32(std::vector<uint8_t>& out, uint32_t value) {
        for (int shift = 0; shift < 32; shift += 8) {
            out.push_back(static_cast<uint8_t>((value >> shift) & 0xFF));
        }
    }
    std::vector<uint8_t> body_;
    std::vector<Entry> entries_;
};

std::vector<uint8_t> png_bytes(uint32_t width, uint32_t height,
                               const std::vector<uint8_t>& rgba) {
    const auto path = std::filesystem::temp_directory_path() /
                      std::format("plasma-packshot-{}x{}-{}.png", width, height,
                                  Clock::now().time_since_epoch().count());
    mcpp::write_png(path, width, height, rgba);
    std::ifstream input(path, std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)),
                               std::istreambuf_iterator<char>());
    std::error_code code;
    std::filesystem::remove(path, code);
    return bytes;
}

void put(std::vector<uint8_t>& rgba, uint32_t side, uint32_t x, uint32_t y, uint32_t colour) {
    const size_t at = (static_cast<size_t>(y) * side + x) * 4;
    rgba[at] = static_cast<uint8_t>(colour >> 24);
    rgba[at + 1] = static_cast<uint8_t>(colour >> 16);
    rgba[at + 2] = static_cast<uint8_t>(colour >> 8);
    rgba[at + 3] = static_cast<uint8_t>(colour);
}

// Loud on purpose: what matters in the proof is whether a replaced texture is
// there at all and whether its texels are the pack's, not whether it is
// pretty.
std::vector<uint8_t> checker(uint32_t side, uint32_t a, uint32_t b, uint32_t cell) {
    std::vector<uint8_t> rgba(static_cast<size_t>(side) * side * 4);
    for (uint32_t y = 0; y < side; ++y) {
        for (uint32_t x = 0; x < side; ++x) {
            put(rgba, side, x, y, ((x / cell) + (y / cell)) % 2 == 0 ? a : b);
        }
    }
    return rgba;
}

std::vector<uint8_t> stripes(uint32_t side, uint32_t a, uint32_t b, uint32_t width) {
    std::vector<uint8_t> rgba(static_cast<size_t>(side) * side * 4);
    for (uint32_t y = 0; y < side; ++y) {
        for (uint32_t x = 0; x < side; ++x) put(rgba, side, x, y, (x / width) % 2 == 0 ? a : b);
    }
    return rgba;
}

std::vector<uint8_t> framed_cross(uint32_t side, uint32_t fill, uint32_t ink) {
    std::vector<uint8_t> rgba(static_cast<size_t>(side) * side * 4);
    for (uint32_t y = 0; y < side; ++y) {
        for (uint32_t x = 0; x < side; ++x) {
            const bool edge = x == 0 || y == 0 || x == side - 1 || y == side - 1;
            const bool cross = x == y || x + y == side - 1;
            put(rgba, side, x, y, edge || cross ? ink : fill);
        }
    }
    return rgba;
}

// A sprite with alpha, so the cutout path and the icon's own transparency are
// in the picture too: a diagonal blade on nothing.
std::vector<uint8_t> blade(uint32_t side, uint32_t edge, uint32_t core) {
    std::vector<uint8_t> rgba(static_cast<size_t>(side) * side * 4);
    for (uint32_t y = 0; y < side; ++y) {
        for (uint32_t x = 0; x < side; ++x) {
            const int32_t dx = static_cast<int32_t>(x) - static_cast<int32_t>(side - 1 - y);
            if (dx >= -1 && dx <= 1) put(rgba, side, x, y, dx == 0 ? core : edge);
        }
    }
    return rgba;
}

std::vector<uint8_t> bar(uint32_t width, uint32_t height, uint32_t a, uint32_t b) {
    std::vector<uint8_t> rgba(static_cast<size_t>(width) * height * 4);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const size_t at = (static_cast<size_t>(y) * width + x) * 4;
            const uint32_t colour = ((x / 10) % 2 == 0) ? a : b;
            rgba[at] = static_cast<uint8_t>(colour >> 24);
            rgba[at + 1] = static_cast<uint8_t>(colour >> 16);
            rgba[at + 2] = static_cast<uint8_t>(colour >> 8);
            rgba[at + 3] = static_cast<uint8_t>(colour);
        }
    }
    return rgba;
}

constexpr std::string_view kPackMeta =
    R"({"pack":{"description":"Plasma pack test","min_format":[75,0],)"
    R"("max_format":[75,2147483647]}})";

std::vector<uint8_t> demo_pack() {
    ZipWriter zip;
    zip.add_text("pack.mcmeta", kPackMeta);
    zip.add("assets/minecraft/textures/block/stone.png",
            png_bytes(16, 16, checker(16, 0xFF00FFFFu, 0x101010FFu, 4)));
    zip.add("assets/minecraft/textures/block/diamond_block.png",
            png_bytes(16, 16, framed_cross(16, 0xFF8000FFu, 0x000000FFu)));
    zip.add("assets/minecraft/textures/block/oak_planks.png",
            png_bytes(16, 16, stripes(16, 0x0040FFFFu, 0x002080FFu, 2)));
    zip.add("assets/minecraft/textures/item/diamond_sword.png",
            png_bytes(16, 16, blade(16, 0x000000FFu, 0xFFFF00FFu)));
    zip.add("assets/minecraft/textures/item/apple.png",
            png_bytes(16, 16, checker(16, 0x00FFFFFFu, 0x004040FFu, 2)));
    zip.add("assets/minecraft/textures/gui/sprites/hud/hotbar.png",
            png_bytes(182, 22, bar(182, 22, 0xFF0000FFu, 0x00FF00FFu)));
    // What a world a server generates is actually made of, so the pack is
    // visible on terrain nobody had to build.
    zip.add("assets/minecraft/textures/block/dirt.png",
            png_bytes(16, 16, checker(16, 0xFF0000FFu, 0x400000FFu, 4)));
    zip.add("assets/minecraft/textures/block/grass_block_top.png",
            png_bytes(16, 16, checker(16, 0xFFFFFFFFu, 0x303030FFu, 2)));
    zip.add("assets/minecraft/textures/block/grass_block_side.png",
            png_bytes(16, 16, stripes(16, 0xFFFF00FFu, 0x000000FFu, 2)));
    zip.add("assets/minecraft/textures/block/sand.png",
            png_bytes(16, 16, framed_cross(16, 0x00FF00FFu, 0x000000FFu)));
    zip.add("assets/minecraft/textures/block/oak_log.png",
            png_bytes(16, 16, stripes(16, 0xFF00FFFFu, 0x000000FFu, 1)));
    zip.add("assets/minecraft/textures/block/oak_log_top.png",
            png_bytes(16, 16, framed_cross(16, 0xFF00FFFFu, 0xFFFFFFFFu)));
    zip.add("assets/minecraft/textures/block/water_still.png",
            png_bytes(16, 16, checker(16, 0xFF8000FFu, 0x804000FFu, 4)));
    return zip.finish();
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

struct View {
    const char* what;
    float eye[3];
    float target[3];
};

size_t pixels_differing(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    if (a.size() != b.size()) return a.size();
    size_t count = 0;
    for (size_t i = 0; i + 3 < a.size(); i += 4) {
        if (a[i] != b[i] || a[i + 1] != b[i + 1] || a[i + 2] != b[i + 2]) ++count;
    }
    return count;
}

// The interface half of the picture: the icons the pack replaces, blown up so
// a texel can be looked at, and the sprite it replaces behind them.
void draw_ui(mcpp::gpu::UiRenderer& quads, const mcpp::ui::ItemTable& items) {
    quads.begin();
    quads.fill(0.0f, 0.0f, static_cast<float>(kWidth), static_cast<float>(kHeight), 0x202020FFu);
    quads.sprite("hud/hotbar", 40.0f, 30.0f, 182.0f * 3.0f, 22.0f * 3.0f);
    const char* names[] = {"diamond_sword", "apple", "golden_apple", "stone", "oak_planks",
                           "diamond_block"};
    float x = 40.0f;
    for (const char* name : names) {
        const int32_t id = items.find(name);
        if (id >= 0) quads.icon(id, x, 150.0f, 80.0f);
        x += 96.0f;
    }
    // One icon at eight screen pixels per texel, for a close look.
    const int32_t sword = items.find("diamond_sword");
    if (sword >= 0) quads.icon(sword, 40.0f, 260.0f, 192.0f);
    const int32_t apple = items.find("apple");
    if (apple >= 0) quads.icon(apple, 260.0f, 260.0f, 192.0f);
}

} // namespace

int packshot(const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cerr << "usage: packshot <out-dir> [pack.zip]\n"
                     "       without a pack one is written to <out-dir>/plasma-test-pack.zip\n";
        return 2;
    }
    const std::filesystem::path out_dir = args[0];
    std::filesystem::create_directories(out_dir);

    std::filesystem::path pack_path = out_dir / "plasma-test-pack.zip";
    if (args.size() > 1) {
        pack_path = args[1];
    } else {
        const auto bytes = demo_pack();
        std::ofstream output(pack_path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        std::cout << std::format("{}: {} bytes\n", pack_path.string(), bytes.size());
    }

    const auto& shapes = mcpp::world::BlockShapes::game();
    mcpp::world::BlockTable table;
    mcpp::world::Atlas atlas;
    if (!shapes.loaded() || !table.open(mcpp::world::BlockTable::default_path(kProtocol)) ||
        !atlas.open(mcpp::world::Atlas::default_path(kProtocol))) {
        std::cerr << "bake the client's data with tools/bake_all.py first\n";
        return 1;
    }
    mcpp::ui::ItemTable items;
    items.load(mcpp::ui::ItemTable::default_path(kProtocol));

    mcpp::gpu::Device device;
    device.create({});
    mcpp::gpu::WorldRenderer world(device, kWidth, kHeight, atlas);
    mcpp::gpu::UiRenderer quads(device, world.colour_format(),
                                mcpp::gpu::UiRenderer::default_items_path(kProtocol),
                                mcpp::gpu::UiRenderer::default_gui_path(kProtocol));

    // A floor of stone, a wall of planks and a pillar of diamond: the three
    // block textures the pack replaces, each big enough to look at.
    const int32_t stone = shapes.find("minecraft:stone");
    const int32_t planks = shapes.find("minecraft:oak_planks");
    const int32_t diamond = shapes.find("minecraft:diamond_block");
    std::vector<int32_t> states(4096, 0);
    const auto index = [](int32_t x, int32_t y, int32_t z) {
        return static_cast<size_t>(y) * 256 + static_cast<size_t>(z) * 16 +
               static_cast<size_t>(x);
    };
    for (int32_t z = 0; z < 16; ++z) {
        for (int32_t x = 0; x < 16; ++x) states[index(x, 0, z)] = stone;
    }
    for (int32_t y = 1; y < 5; ++y) {
        for (int32_t x = 4; x < 12; ++x) states[index(x, y, 4)] = planks;
    }
    for (int32_t y = 1; y < 4; ++y) states[index(8, y, 9)] = diamond;

    mcpp::world::Chunk chunk;
    chunk.min_y = 0;
    chunk.sections.push_back(section_of(states));
    const mcpp::world::ChunkBlocks unpacked(chunk);
    world.upload(mcpp::world::mesh_chunk(chunk, unpacked, table, {}));

    const View views[] = {
        {"wall", {8.5f, 2.5f, 9.5f}, {8.5f, 2.5f, 4.0f}},
        {"close", {8.5f, 2.0f, 6.0f}, {8.5f, 2.0f, 4.0f}},
        {"floor", {8.5f, 3.0f, 13.0f}, {8.5f, 0.5f, 8.0f}},
        {"diamond", {8.5f, 2.2f, 11.2f}, {8.5f, 2.0f, 9.0f}},
    };

    const auto shoot = [&](const char* stage) {
        std::map<std::string, std::vector<uint8_t>> taken;
        for (const View& view : views) {
            mcpp::gpu::Camera camera{};
            for (int axis = 0; axis < 3; ++axis) {
                camera.eye[axis] = view.eye[axis];
                camera.target[axis] = view.target[axis];
            }
            camera.fov_degrees = 60.0f;
            camera.near_plane = 0.05f;
            auto pixels = world.render(camera);
            mcpp::write_png(out_dir / std::format("world-{}-{}.png", view.what, stage), kWidth,
                            kHeight, pixels);
            taken.emplace(view.what, std::move(pixels));
        }
        draw_ui(quads, items);
        mcpp::gpu::Camera above{};
        above.eye[1] = 10000.0f;
        above.target[0] = 1.0f;
        above.target[1] = 10000.0f;
        auto ui = world.render(above, [&](VkCommandBuffer command, uint32_t w, uint32_t h) {
            quads.record(command, w, h);
        });
        mcpp::write_png(out_dir / std::format("ui-{}.png", stage), kWidth, kHeight, ui);
        taken.emplace("ui", std::move(ui));
        return taken;
    };

    const auto before = shoot("before");

    mcpp::packs::GpuPackTarget target(&world, &quads);
    mcpp::packs::PackSystem::Options options;
    options.protocol = kProtocol;
    options.prompt = mcpp::packs::PromptStatus::Allowed;
    mcpp::packs::PackSystem system(std::move(options), target);
    if (!system.ready()) {
        std::cerr << system.error() << '\n';
        return 1;
    }
    system.status = [](std::string_view line) { std::cout << "- " << line << '\n'; };

    mcpp::packs::PackId id{};
    id[0] = 0x9a;
    const auto apply_started = Clock::now();
    system.push_local(id, pack_path);
    system.tick();
    const double apply_ms =
        std::chrono::duration<double, std::milli>(Clock::now() - apply_started).count();
    if (system.applied_packs() == 0) {
        std::cerr << "the pack was not applied: " << system.error() << '\n';
        return 1;
    }
    const auto& applied = system.last_apply();

    const auto after = shoot("after");

    const auto pop_started = Clock::now();
    system.pop(mcpp::packs::Pop{});
    system.tick();
    const double pop_ms =
        std::chrono::duration<double, std::milli>(Clock::now() - pop_started).count();
    const auto restored = shoot("restored");

    std::cout << std::format(
        "\napply: {} block textures over {} atlas layers, {} item icons, {} interface sprites, "
        "{} skipped\n"
        "  {:.2f} ms in all ({:.2f} read and unpack, {:.2f} mip chains, {:.2f} upload), "
        "{:.2f} ms end to end\n"
        "pop:   {} atlas layers, {} icons, {} sprites put back in {:.2f} ms ({:.2f} end to end)\n",
        applied.block_textures, applied.atlas_layers, applied.item_icons, applied.gui_sprites,
        applied.skipped, applied.total_ms, applied.read_ms, applied.mip_ms, applied.upload_ms,
        apply_ms, system.last_revert().atlas_layers, system.last_revert().item_icons,
        system.last_revert().gui_sprites, system.last_revert().total_ms, pop_ms);

    int wrong = 0;
    for (const auto& [what, pixels] : before) {
        const size_t changed = pixels_differing(pixels, after.at(what));
        const size_t back = pixels_differing(pixels, restored.at(what));
        const double share = 100.0 * static_cast<double>(changed) /
                             static_cast<double>(kWidth * kHeight);
        std::cout << std::format("{:>8}: {} pixels changed ({:.1f}%), {} still differ after the "
                                 "pop\n", what, changed, share, back);
        if (changed == 0) {
            std::cerr << std::format("FAIL: {} looks the same with the pack on\n", what);
            ++wrong;
        }
        if (back != 0) {
            std::cerr << std::format("FAIL: {} did not go back to the baked look\n", what);
            ++wrong;
        }
    }

    // What a frame costs with the pack on, so the cost of having one is in the
    // report next to the cost of applying it.
    double gpu = 0.0;
    constexpr int kFrames = 30;
    for (int i = 0; i < kFrames; ++i) {
        mcpp::gpu::Camera camera{};
        camera.eye[0] = 8.5f;
        camera.eye[1] = 2.5f;
        camera.eye[2] = 9.5f;
        camera.target[0] = 8.5f;
        camera.target[1] = 2.5f;
        camera.target[2] = 4.0f;
        camera.fov_degrees = 60.0f;
        world.render(camera);
        gpu += world.last_gpu_ms();
    }
    std::cout << std::format("frame: {:.3f} ms gpu over {} frames with the pack off\n",
                             gpu / kFrames, kFrames);

    device.save_pipeline_cache();
    return wrong == 0 ? 0 : 1;
}

int packserve(const std::vector<std::string>& args) {
    if (args.size() < 4) {
        std::cerr << "usage: packserve <host[:port]> <name> <x,y,z> <out-dir> [seconds]\n";
        return 2;
    }
    const auto address = parse_address(args[0]);
    std::string username = args[1];
    double aim[3] = {0.0, 0.0, 0.0};
    // "auto" aims at wherever the server put us, which is what a command like
    // spreadplayers leaves us at: on the ground, at a spot we chose.
    const bool aim_at_player = args[2] == "auto";
    if (!aim_at_player) {
        std::string rest = args[2];
        for (int axis = 0; axis < 3; ++axis) {
            const size_t comma = rest.find(',');
            aim[axis] = std::stod(rest.substr(0, comma));
            rest = comma == std::string::npos ? "" : rest.substr(comma + 1);
        }
    }
    const std::filesystem::path out_dir = args[3];
    std::filesystem::create_directories(out_dir);
    const double seconds = args.size() > 4 ? std::stod(args[4]) : 15.0;

    mcpp::world::BlockTable table;
    mcpp::world::Atlas atlas;
    if (!table.open(mcpp::world::BlockTable::default_path(kProtocol)) ||
        !atlas.open(mcpp::world::Atlas::default_path(kProtocol))) {
        std::cerr << "bake the client's data with tools/bake_all.py first\n";
        return 1;
    }
    mcpp::ui::ItemTable items;
    items.load(mcpp::ui::ItemTable::default_path(kProtocol));

    // The renderers come up before the connection, so the pack the server
    // pushes really does land on a running client's textures, between frames,
    // and the answer we send it is the truth.
    mcpp::gpu::Device device;
    device.create({});
    mcpp::gpu::WorldRenderer world(device, kWidth, kHeight, atlas);
    mcpp::gpu::UiRenderer quads(device, world.colour_format(),
                                mcpp::gpu::UiRenderer::default_items_path(kProtocol),
                                mcpp::gpu::UiRenderer::default_gui_path(kProtocol));

    mcpp::packs::GpuPackTarget target(&world, &quads);
    mcpp::packs::PackSystem::Options options;
    options.protocol = kProtocol;
    // `Options.serverResourcePacks` set to always: the proof accepts.
    options.prompt = mcpp::packs::PromptStatus::Allowed;
    options.identity.username = username;
    mcpp::packs::PackSystem system(std::move(options), target);
    if (!system.ready()) {
        std::cerr << system.error() << '\n';
        return 1;
    }

    std::map<std::pair<int32_t, int32_t>, mcpp::world::Chunk> chunks;
    double where[3] = {0.0, 0.0, 0.0};
    bool placed = false;

    mcpp::SessionOptions session_options;
    session_options.host = address.host;
    session_options.port = address.port;
    session_options.profile = signed_in_profile(username);
    session_options.username = username;
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
    };
    events.packs.on_push = [&](const mcpp::packs::Push& push) {
        std::cout << std::format("- the server pushes {} ({}), required {}\n", push.url,
                                 push.hash.empty() ? "no hash" : push.hash, push.required);
        system.push(push);
    };
    events.packs.on_pop = [&](const mcpp::packs::Pop& pop) { system.pop(pop); };

    mcpp::Session session(session_options, events);
    system.status = [](std::string_view line) { std::cout << "- " << line << '\n'; };
    system.respond = [&](const mcpp::packs::PackId& id, mcpp::packs::Action action) {
        session.send_resource_pack(id, action);
    };

    session.connect();
    const auto joined = Clock::now();
    while (Clock::now() - joined < std::chrono::duration<double>(seconds)) {
        while (session.has_data()) {
            if (!session.pump()) break;
        }
        system.tick();
        if (placed) session.send_position(where[0], where[1], where[2], 0.0f, 0.0f, true);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    session.close();

    if (system.applied_packs() == 0) {
        std::cerr << "the server pushed no pack we could apply\n";
        return 1;
    }
    const auto& applied = system.last_apply();
    std::cout << std::format(
        "- applied {} pack(s): {} block textures over {} atlas layers, {} item icons, "
        "{} interface sprites, {} skipped, {:.2f} ms "
        "(read {:.2f}, mips {:.2f}, upload {:.2f})\n",
        system.applied_packs(), applied.block_textures, applied.atlas_layers, applied.item_icons,
        applied.gui_sprites, applied.skipped, applied.total_ms, applied.read_ms, applied.mip_ms,
        applied.upload_ms);
    for (const auto& one : system.last_downloads()) {
        std::cout << std::format("- {} bytes, {:.1f} ms to fetch, {:.1f} ms to hash, {}\n",
                                 one.bytes, one.download_ms, one.hash_ms,
                                 one.from_cache ? "from the cache" : "downloaded");
    }

    if (aim_at_player) {
        for (int axis = 0; axis < 3; ++axis) aim[axis] = where[axis];
        std::cout << std::format("- aiming at the player: {:.1f} {:.1f} {:.1f}\n", aim[0], aim[1],
                                 aim[2]);
    }
    const int32_t middle_x = static_cast<int32_t>(std::floor(aim[0])) >> 4;
    const int32_t middle_z = static_cast<int32_t>(std::floor(aim[2])) >> 4;
    std::erase_if(chunks, [&](const auto& entry) {
        return std::abs(entry.first.first - middle_x) > 3 ||
               std::abs(entry.first.second - middle_z) > 3;
    });
    if (chunks.empty()) {
        std::cerr << "the server sent no chunks round the camera\n";
        return 1;
    }
    std::map<std::pair<int32_t, int32_t>, mcpp::world::ChunkBlocks> unpacked;
    for (const auto& [key, chunk] : chunks) unpacked.emplace(key, mcpp::world::ChunkBlocks(chunk));
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

    const auto fx = static_cast<float>(aim[0]);
    const auto fy = static_cast<float>(aim[1]);
    const auto fz = static_cast<float>(aim[2]);
    const View server_views[] = {
        {"front", {fx, fy + 1.6f, fz + 5.0f}, {fx, fy + 1.0f, fz}},
        {"down", {fx, fy + 3.0f, fz + 2.0f}, {fx, fy - 1.0f, fz}},
        {"close", {fx, fy + 1.2f, fz + 1.8f}, {fx, fy + 0.8f, fz}},
    };
    const auto shoot = [&](const char* stage) {
        std::map<std::string, std::vector<uint8_t>> taken;
        for (const View& view : server_views) {
            mcpp::gpu::Camera camera{};
            for (int axis = 0; axis < 3; ++axis) {
                camera.eye[axis] = view.eye[axis];
                camera.target[axis] = view.target[axis];
            }
            camera.fov_degrees = 70.0f;
            camera.near_plane = 0.05f;
            auto pixels = world.render(camera);
            mcpp::write_png(out_dir / std::format("server-{}-{}.png", view.what, stage), kWidth,
                            kHeight, pixels);
            taken.emplace(view.what, std::move(pixels));
        }
        draw_ui(quads, items);
        mcpp::gpu::Camera above{};
        above.eye[1] = 10000.0f;
        above.target[0] = 1.0f;
        above.target[1] = 10000.0f;
        auto ui = world.render(above, [&](VkCommandBuffer command, uint32_t w, uint32_t h) {
            quads.record(command, w, h);
        });
        mcpp::write_png(out_dir / std::format("server-ui-{}.png", stage), kWidth, kHeight, ui);
        taken.emplace("ui", std::move(ui));
        return taken;
    };

    // The pack is on now, because the session put it on. Off, on again: the
    // same scene three times, so the difference is the pack and nothing else.
    const auto with_pack = shoot("with-pack");
    system.leave();
    const auto without = shoot("without-pack");

    int wrong = 0;
    for (const auto& [what, pixels] : with_pack) {
        const size_t changed = pixels_differing(pixels, without.at(what));
        std::cout << std::format("{:>8}: {} pixels differ between the pack and the baked look\n",
                                 what, changed);
        if (changed == 0) {
            std::cerr << std::format("FAIL: {} looks the same either way\n", what);
            ++wrong;
        }
    }
    device.save_pipeline_cache();
    return wrong == 0 ? 0 : 1;
}

} // namespace client
