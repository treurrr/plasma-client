#include "mcpp/crypto.hpp"
#include "mcpp/byte_buffer.hpp"
#include "mcpp/json.hpp"
#include "mcpp/nbt.hpp"
#include "mcpp/proto/packet_ids.hpp"
#include "mcpp/session.hpp"
#include "mcpp/world/block_table.hpp"
#include "mcpp/world/chunk.hpp"
#include "mcpp/gpu/span_allocator.hpp"
#include "mcpp/world/light.hpp"
#include "mcpp/world/mesher.hpp"
#include "mcpp/world/physics.hpp"

#include <cmath>
#include <numbers>
#include <cassert>
#include <filesystem>
#include <format>
#include <fstream>
#include <initializer_list>
#include <map>
#include <span>
#include <iostream>
#include <string>
#include <tuple>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, std::string_view what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << '\n';
        ++failures;
    }
}

std::string hex(const std::vector<uint8_t>& bytes) {
    std::string out;
    for (uint8_t b : bytes) out += std::format("{:02x}", b);
    return out;
}

void test_varint_encoding() {
    // Canonical vectors from the protocol documentation.
    const struct {
        int32_t value;
        const char* encoded;
    } cases[] = {
        {0, "00"},          {1, "01"},           {2, "02"},
        {127, "7f"},        {128, "8001"},       {255, "ff01"},
        {25565, "ddc701"},  {2097151, "ffff7f"}, {2147483647, "ffffffff07"},
        {-1, "ffffffff0f"}, {-2147483648, "8080808008"},
    };

    for (const auto& c : cases) {
        mcpp::ByteBuffer buffer;
        buffer.write_varint(c.value);
        check(hex(buffer.data()) == c.encoded,
              std::format("varint {} encodes to {} (got {})", c.value, c.encoded,
                          hex(buffer.data())));

        mcpp::ByteBuffer reader(buffer.data());
        check(reader.read_varint() == c.value, std::format("varint {} round trips", c.value));
        check(reader.remaining() == 0, std::format("varint {} consumed fully", c.value));
        check(mcpp::ByteBuffer::varint_size(c.value) == buffer.data().size(),
              std::format("varint_size({}) matches", c.value));
    }
}

void test_varlong_and_numbers() {
    const int64_t values[] = {0, 1, -1, 127, 128, 2147483647, -2147483648LL, 9223372036854775807LL};
    for (int64_t v : values) {
        mcpp::ByteBuffer buffer;
        buffer.write_varlong(v);
        mcpp::ByteBuffer reader(buffer.data());
        check(reader.read_varlong() == v, std::format("varlong {} round trips", v));
    }

    mcpp::ByteBuffer buffer;
    buffer.write_u16(25565);
    buffer.write_i32(-123456);
    buffer.write_i64(-1234567890123LL);
    buffer.write_f32(0.5f);
    buffer.write_f64(-3.25);
    buffer.write_bool(true);

    mcpp::ByteBuffer reader(buffer.data());
    check(reader.read_u16() == 25565, "u16 round trips");
    check(reader.read_i32() == -123456, "i32 round trips");
    check(reader.read_i64() == -1234567890123LL, "i64 round trips");
    check(reader.read_f32() == 0.5f, "f32 round trips");
    check(reader.read_f64() == -3.25, "f64 round trips");
    check(reader.read_bool(), "bool round trips");
}

void test_strings_and_underflow() {
    mcpp::ByteBuffer buffer;
    buffer.write_string("Bradge local test server");
    buffer.write_string("");
    mcpp::ByteBuffer reader(buffer.data());
    check(reader.read_string() == "Bradge local test server", "string round trips");
    check(reader.read_string().empty(), "empty string round trips");

    bool threw = false;
    try {
        reader.read_u8();
    } catch (const mcpp::ProtocolError&) {
        threw = true;
    }
    check(threw, "reading past the end throws ProtocolError");

    threw = false;
    try {
        mcpp::ByteBuffer bad(std::vector<uint8_t>{0x80, 0x80, 0x80, 0x80, 0x80, 0x80});
        bad.read_varint();
    } catch (const mcpp::ProtocolError&) {
        threw = true;
    }
    check(threw, "over-long varint throws ProtocolError");
}

void test_json() {
    const std::string status = R"({
        "version": {"name": "Paper 1.21.11", "protocol": 774},
        "players": {"max": 20, "online": 2, "sample": [{"name": "treur"}, {"name": "kinate"}]},
        "description": {"text": "Bradge ", "extra": [{"text": "local"}, " test"]},
        "enforcesSecureChat": false,
        "escaped": "a\"b\\cé😀"
    })";

    auto root = mcpp::json::parse(status);
    check(root.find("version")->find("name")->string_or("") == "Paper 1.21.11", "json nested string");
    check(root.find("version")->find("protocol")->number_or(0) == 774, "json nested number");
    check(root.find("players")->find("sample")->at(1)->find("name")->string_or("") == "kinate",
          "json array index");
    check(root.find("enforcesSecureChat")->as_bool() == false, "json bool");
    check(mcpp::json::flatten_component(*root.find("description")) == "Bradge local test",
          "chat component flattens");
    check(root.find("escaped")->string_or("") == "a\"b\\c\xc3\xa9\xf0\x9f\x98\x80",
          "json escapes incl. surrogate pair");
    check(root.find("nope") == nullptr, "missing key yields nullptr");

    check(mcpp::json::strip_formatting("\xc2\xa7""aBradge\xc2\xa7r \xc3\xa9") == "Bradge \xc3\xa9",
          "strip_formatting removes colour codes");

    for (std::string_view bad : {"{", "{\"a\":}", "[1,]", "{\"a\":1}x", "tru"}) {
        bool threw = false;
        try {
            mcpp::json::parse(bad);
        } catch (const mcpp::ProtocolError&) {
            threw = true;
        }
        check(threw, std::format("malformed JSON rejected: {}", bad));
    }
}

void test_nbt() {
    // A compound holding a string, a wrapped number and a nested list.
    mcpp::ByteBuffer buffer;
    buffer.write_u8(10);                                   // root compound
    buffer.write_u8(8);                                    // string
    buffer.write_u16(4); buffer.write_bytes(std::span<const uint8_t>{
        reinterpret_cast<const uint8_t*>("text"), 4});
    buffer.write_u16(2); buffer.write_bytes(std::span<const uint8_t>{
        reinterpret_cast<const uint8_t*>("hi"), 2});
    buffer.write_u8(0);                                    // end

    auto value = mcpp::nbt::read_network_tag(buffer);
    check(value.find("text") != nullptr, "nbt compound holds its field");
    check(mcpp::nbt::component_to_text(value) == "hi", "component text is read");

    mcpp::ByteBuffer truncated;
    truncated.write_u8(10);
    truncated.write_u8(8);
    bool threw = false;
    try {
        mcpp::nbt::read_network_tag(truncated);
    } catch (const mcpp::ProtocolError&) {
        threw = true;
    }
    check(threw, "truncated nbt is rejected");
}

void test_translation_patterns() {
    const std::map<std::string, std::string, std::less<>> table{
        {"a.ordered", "%s joined as %s"},
        {"a.positional", "%2$s before %1$s"},
        {"a.percent", "100%% sure"},
    };
    auto lookup = [&](std::string_view key) -> const std::string* {
        auto it = table.find(key);
        return it == table.end() ? nullptr : &it->second;
    };

    // {translate: key, with: ["one", {"": 2}]}
    auto component = [](std::string_view key) {
        mcpp::nbt::Compound fields;
        fields.emplace("translate", mcpp::nbt::Value{std::string(key)});
        mcpp::nbt::Compound wrapped;
        wrapped.emplace("", mcpp::nbt::Value{int32_t{2}});
        fields.emplace("with", mcpp::nbt::Value{mcpp::nbt::List{
            mcpp::nbt::Value{std::string("one")}, mcpp::nbt::Value{std::move(wrapped)}}});
        return mcpp::nbt::Value{std::move(fields)};
    };

    check(mcpp::nbt::component_to_text(component("a.ordered"), lookup) == "one joined as 2",
          "ordered %s arguments");
    check(mcpp::nbt::component_to_text(component("a.positional"), lookup) == "2 before one",
          "positional %N$s arguments");
    check(mcpp::nbt::component_to_text(component("a.percent"), lookup) == "100% sure",
          "escaped percent");
    check(mcpp::nbt::component_to_text(component("a.missing"), lookup) == "a.missing",
          "unknown key falls back to itself");
}

void test_paletted_container() {
    // 24 entries of 5 bits: 12 fit in a long, the top 4 bits stay padding.
    constexpr uint8_t kBits = 5;
    constexpr size_t kEntries = 24;
    constexpr size_t kPerLong = 64 / kBits;

    mcpp::ByteBuffer buffer;
    buffer.write_u8(kBits);
    buffer.write_varint(8);
    for (int32_t i = 0; i < 8; ++i) buffer.write_varint(100 + i);

    uint64_t packed[2] = {0, 0};
    for (size_t i = 0; i < kEntries; ++i) {
        const uint64_t index = i % 8;
        packed[i / kPerLong] |= index << (i % kPerLong * kBits);
    }
    buffer.write_i64(static_cast<int64_t>(packed[0]));
    buffer.write_i64(static_cast<int64_t>(packed[1]));

    auto container = mcpp::world::PalettedContainer::read(buffer, kEntries, 8);
    check(buffer.remaining() == 0, "paletted container consumes exactly its bytes");
    check(container.bits() == kBits, "bits per entry are read");

    bool all = true;
    for (size_t i = 0; i < kEntries; ++i) {
        if (container.at(i) != static_cast<int32_t>(100 + i % 8)) all = false;
    }
    check(all, "every entry unpacks to its palette value");

    mcpp::ByteBuffer uniform;
    uniform.write_u8(0);
    uniform.write_varint(1234);
    auto single = mcpp::world::PalettedContainer::read(uniform, 4096, 8);
    check(single.uniform() && single.at(0) == 1234 && single.at(4095) == 1234,
          "a uniform section needs no data array");

    bool threw = false;
    try {
        container.at(kEntries);
    } catch (const mcpp::ProtocolError&) {
        threw = true;
    }
    check(threw, "reading past the end is rejected");

    // Writing. A block change has to survive every shape the container takes,
    // because a section that is all stone and a section with two hundred kinds
    // of block are stored completely differently.
    single.set(7, 99, 8);
    check(single.at(7) == 99 && single.at(6) == 1234 && single.at(4095) == 1234,
          "a uniform section splits when one block in it changes");

    container.set(3, 777, 8);
    check(container.at(3) == 777, "a new value joins the palette");
    bool untouched = true;
    for (size_t i = 0; i < kEntries; ++i) {
        if (i != 3 && container.at(i) != static_cast<int32_t>(100 + i % 8)) untouched = false;
    }
    check(untouched, "and leaves every other entry where it was");

    // Past what five bits can index, the container has to widen on its own.
    for (int32_t i = 0; i < 40; ++i) container.set(static_cast<size_t>(i % kEntries), 500 + i, 8);
    bool kept = true;
    for (int32_t i = 16; i < 40; ++i) {
        const size_t at = static_cast<size_t>(i % kEntries);
        int32_t last = 0;
        for (int32_t j = i; j < 40; ++j) {
            if (static_cast<size_t>(j % kEntries) == at) last = 500 + j;
        }
        if (container.at(at) != last) kept = false;
    }
    check(kept, std::format("a palette that outgrows its width is rewritten ({} bits now)",
                            container.bits()));

    // And past what a palette is worth at all, every entry stores its own
    // state, which is what the server would have sent in the first place.
    mcpp::ByteBuffer small;
    small.write_u8(0);
    small.write_varint(5);
    auto wide = mcpp::world::PalettedContainer::read(small, 64, 4);
    for (int32_t i = 0; i < 64; ++i) wide.set(static_cast<size_t>(i), 1000 + i, 4);
    bool direct = true;
    for (int32_t i = 0; i < 64; ++i) {
        if (wide.at(static_cast<size_t>(i)) != 1000 + i) direct = false;
    }
    check(direct && wide.palette().empty(),
          "a section with more kinds of block than a palette holds goes direct");
}

// The reader indexes a mapping in place, so its section alignment has to match
// the generator's byte for byte; build a minimal table and read it back.
void test_block_table() {
    using mcpp::world::BakedQuad;

    const std::string block_name = "minecraft:test";
    const std::string texture_name = "block/test";
    std::vector<char> strings;
    const uint32_t block_offset = 0;
    strings.insert(strings.end(), block_name.begin(), block_name.end());
    strings.push_back('\0');
    const auto texture_offset = static_cast<uint32_t>(strings.size());
    strings.insert(strings.end(), texture_name.begin(), texture_name.end());
    strings.push_back('\0');

    std::vector<uint8_t> file;
    const auto append = [&](const void* data, size_t bytes) {
        const auto* src = static_cast<const uint8_t*>(data);
        file.insert(file.end(), src, src + bytes);
        while (file.size() % 8 != 0) file.push_back(0);
    };
    const auto append_u32s = [&](std::initializer_list<uint32_t> values) {
        append(std::data(values), values.size() * sizeof(uint32_t));
    };

    const char magic[4] = {'P', 'L', 'B', 'K'};
    file.insert(file.end(), magic, magic + 4);
    append_u32s({1, 774, 2, 1, 1, 1, static_cast<uint32_t>(strings.size())});

    append_u32s({0, 1, 1});                       // quad offsets for two states

    BakedQuad quad{};
    quad.from[0] = 0; quad.from[1] = 0; quad.from[2] = 0;
    quad.to[0] = 16 * mcpp::world::kUnitsPerModelUnit;
    quad.to[1] = 16 * mcpp::world::kUnitsPerModelUnit;
    quad.to[2] = 16 * mcpp::world::kUnitsPerModelUnit;
    quad.texture = 0;
    quad.face = 1;
    quad.cullface = 1;
    quad.tint = -1;
    append(&quad, sizeof(quad));

    append_u32s({texture_offset});
    append_u32s({block_offset});
    append_u32s({0, 0});                          // both states belong to block 0
    const uint8_t flags[2] = {mcpp::world::BlockTable::kHasGeometry, 0};
    append(flags, sizeof(flags));
    append(strings.data(), strings.size());

    const auto path = std::filesystem::temp_directory_path() / "plasma-test-blocks.plbk";
    {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(file.data()),
                  static_cast<std::streamsize>(file.size()));
    }

    mcpp::world::BlockTable table;
    check(table.open(path), "the generated table opens");
    check(table.protocol() == 774, "protocol survives the round trip");
    check(table.state_count() == 2, "state count survives");
    check(table.block_name(0) == "minecraft:test", "block names resolve through the string blob");
    check(table.texture_name(0) == "block/test", "texture names resolve");
    check(table.quads(0).size() == 1 && table.quads(1).empty(),
          "the offset table slices quads per state");
    check(table.quads(0)[0].to[1] == 16 * mcpp::world::kUnitsPerModelUnit,
          "fixed-point coordinates survive");
    check(table.quads(0).empty() == false && table.quads(99).empty(),
          "an out-of-range state yields nothing instead of reading past the map");

    std::filesystem::remove(path);
}

// A table with one block: state 1 is an opaque full cube, state 0 is air.
// Both the mesher and the physics need one, and neither needs more.
std::filesystem::path write_cube_table(const char* filename) {
    std::vector<uint8_t> file;
    const auto append = [&](const void* data, size_t bytes) {
        const auto* src = static_cast<const uint8_t*>(data);
        file.insert(file.end(), src, src + bytes);
        while (file.size() % 8 != 0) file.push_back(0);
    };
    const auto append_u32s = [&](std::initializer_list<uint32_t> values) {
        append(std::data(values), values.size() * sizeof(uint32_t));
    };

    const std::string name = "minecraft:solid";
    std::vector<char> strings(name.begin(), name.end());
    strings.push_back('\0');

    const char magic[4] = {'P', 'L', 'B', 'K'};
    file.insert(file.end(), magic, magic + 4);
    append_u32s({1, 774, 2, 6, 1, 1, static_cast<uint32_t>(strings.size())});
    append_u32s({0, 0, 6});                       // state 0 empty, state 1 holds six quads

    std::vector<mcpp::world::BakedQuad> quads(6);
    for (int face = 0; face < 6; ++face) {
        auto& quad = quads[static_cast<size_t>(face)];
        quad.from[0] = quad.from[1] = quad.from[2] = 0;
        quad.to[0] = quad.to[1] = quad.to[2] = 16 * mcpp::world::kUnitsPerModelUnit;
        quad.face = static_cast<uint8_t>(face);
        quad.cullface = static_cast<int8_t>(face);
        quad.tint = -1;
    }
    append(quads.data(), quads.size() * sizeof(mcpp::world::BakedQuad));
    append_u32s({0});                             // one texture, one block name
    append_u32s({0});
    append_u32s({0, 0});
    const uint8_t flags[2] = {0, static_cast<uint8_t>(mcpp::world::BlockTable::kHasGeometry |
                                                      mcpp::world::BlockTable::kFullCube |
                                                      mcpp::world::BlockTable::kOccludes)};
    append(flags, sizeof(flags));
    append(strings.data(), strings.size());

    const auto path = std::filesystem::temp_directory_path() / filename;
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(file.data()),
              static_cast<std::streamsize>(file.size()));
    return path;
}

// One section of solid full cubes: every interior face must go, and exactly the
// shell must stay. Chunk edges count as open, so all four sides are kept.
void test_mesher_culls_interiors() {
    const auto path = write_cube_table("plasma-test-mesher.plbk");

    mcpp::world::BlockTable table;
    check(table.open(path), "the mesher's table opens");
    check(table.full_cube(1) && table.occludes(1), "state 1 is an opaque full cube");

    mcpp::ByteBuffer uniform;
    uniform.write_u8(0);
    uniform.write_varint(1);
    mcpp::ByteBuffer biomes;
    biomes.write_u8(0);
    biomes.write_varint(0);

    mcpp::world::Chunk chunk;
    mcpp::world::ChunkSection section;
    section.non_air_count = 4096;
    section.blocks = mcpp::world::PalettedContainer::read(uniform, 4096, 8);
    section.biomes = mcpp::world::PalettedContainer::read(biomes, 64, 3);
    chunk.sections.push_back(std::move(section));

    const mcpp::world::ChunkBlocks unpacked(chunk);
    const auto mesh = mcpp::world::mesh_chunk(chunk, unpacked, table);

    // Six faces of a 16x16 shell, with nothing above or below the one section.
    check(mesh.quads_emitted == 6 * 16 * 16,
          std::format("only the shell survives: {} of {}", mesh.quads_emitted, 6 * 16 * 16));
    check(mesh.quads_culled == 4096 * 6 - 6 * 16 * 16, "every interior face is dropped");
    check(mesh.triangles() == mesh.quads_emitted * 2, "each quad becomes two triangles");
    check(mesh.vertices.size() == mesh.quads_emitted * 4, "each quad has four vertices");

    std::filesystem::remove(path);
}

void test_server_hash() {
    // Mojang's own published examples. The third is the one that matters: its
    // digest has the top bit set, so the answer is a negative hex number, and
    // getting that wrong means every online-mode join is refused.
    check(mcpp::crypto::server_hash("Notch", {}, {}) ==
              "4ed1f46bbe04bc756bcb17c0c7ce3e4632f06a48",
          "server hash of Notch");
    check(mcpp::crypto::server_hash("jeb_", {}, {}) ==
              "-7c9d5b0044c130109a5d7b5fb5c317c02b4e28c1",
          "server hash of jeb_ is negative");
    check(mcpp::crypto::server_hash("simon", {}, {}) ==
              "88e16a1019277b15d58faf0541e11910eb756f6",
          "server hash of simon drops the leading zero");
}

void test_stream_cipher() {
    const std::vector<uint8_t> secret(16, 0x42);
    const std::string message = "a packet, byte by byte, carried across the wire";

    std::vector<uint8_t> wire(message.begin(), message.end());
    mcpp::crypto::Stream out;
    out.start(secret, true);
    out.apply(wire.data(), wire.size());
    check(!std::equal(wire.begin(), wire.end(), message.begin()), "cipher changed the bytes");

    // Split the read, because CFB8 carries state between calls and a reader
    // takes the length byte before the rest of the frame.
    mcpp::crypto::Stream in;
    in.start(secret, false);
    in.apply(wire.data(), 1);
    in.apply(wire.data() + 1, wire.size() - 1);
    check(std::equal(wire.begin(), wire.end(), message.begin()),
          "a split read decrypts the same as a whole one");
}

void test_packet_tables() {
    const auto* v774 = mcpp::proto::ids_for(774);
    const auto* v776 = mcpp::proto::ids_for(776);
    check(v774 != nullptr && v776 != nullptr, "both generated tables are present");
    check(mcpp::proto::ids_for(47) == nullptr, "1.8.9 has no table yet");

    check(v774->login_sb.hello == v776->login_sb.hello, "login ids match across the two versions");
    check(v774->play_cb.keep_alive != v776->play_cb.keep_alive,
          "play ids do not, which is why they are per version");
    check(v774->play_sb.accept_teleportation == 0, "accept_teleportation is the first play id");
}

} // namespace

// A hole dug under open sky has to fill with sunlight, a block placed in the
// open has to throw a shadow, and taking it away again has to give the light
// back. The server sends none of this; it is the client's to work out.
void test_light_engine() {
    const auto path = write_cube_table("plasma-test-light.plbk");
    mcpp::world::BlockTable table;
    check(table.open(path), "the light test's table opens");

    // One section, stone in the bottom eight layers and air above, lit the
    // way the server lights it: full sky over the stone, dark inside it.
    mcpp::ByteBuffer air;
    air.write_u8(0);
    air.write_varint(0);
    mcpp::ByteBuffer biomes;
    biomes.write_u8(0);
    biomes.write_varint(0);
    mcpp::world::ChunkSection section;
    section.blocks = mcpp::world::PalettedContainer::read(air, 4096, 8);
    section.biomes = mcpp::world::PalettedContainer::read(biomes, 64, 3);

    mcpp::world::ChunkMap world;
    auto& chunk = world[{0, 0}];
    chunk.sections.push_back(std::move(section));
    chunk.sky_light.sections.assign(3, std::vector<uint8_t>(2048, 0));
    chunk.block_light.sections.assign(3, std::vector<uint8_t>(2048, 0));
    for (int32_t y = 0; y < 16; ++y) {
        for (int32_t z = 0; z < 16; ++z) {
            for (int32_t x = 0; x < 16; ++x) {
                if (y < 8) chunk.set_block_state(x, y, z, 1);
                chunk.sky_light.set(1, x, y, z, y < 8 ? 0 : 15);
            }
        }
    }
    const auto sky = [&](int32_t y) { return chunk.sky_light_at(8, y + 64, 8); };

    mcpp::world::LightEngine light(table);

    // Dig the top stone. Sunlight falls into it at full strength.
    chunk.set_block_state(8, 7, 8, 0);
    const auto relit = light.relight(world, 8, 7 - 64, 8);
    check(sky(7 - 64) == 15, std::format("a hole dug under open sky is lit to {}", sky(7 - 64)));
    check(!relit.empty(), "and the chunk it is in is marked to be meshed again");

    // Dig the one below. The light keeps falling without fading.
    chunk.set_block_state(8, 6, 8, 0);
    light.relight(world, 8, 6 - 64, 8);
    check(sky(6 - 64) == 15, std::format("and keeps falling down a shaft: {}", sky(6 - 64)));

    // Put a block in the open air above the shaft. Everything under it loses
    // the sun and is lit from the side instead, one level down.
    chunk.set_block_state(8, 12, 8, 1);
    light.relight(world, 8, 12 - 64, 8);
    check(sky(12 - 64) == 0, "a placed block is dark inside");
    check(sky(11 - 64) == 14,
          std::format("the air right under it is lit from the side: {}", sky(11 - 64)));
    check(sky(8 - 64) == 14, std::format("as is all the open air down to the stone: {}",
                                         sky(8 - 64)));
    // Inside the dug shaft the sides are stone, so the light only comes from
    // above and fades a level per block.
    check(sky(7 - 64) == 13 && sky(6 - 64) == 12,
          std::format("and the shaft in the stone fades a level a block: {} then {}",
                      sky(7 - 64), sky(6 - 64)));

    // Take it away and the sun comes straight back down.
    chunk.set_block_state(8, 12, 8, 0);
    light.relight(world, 8, 12 - 64, 8);
    check(sky(11 - 64) == 15 && sky(6 - 64) == 15,
          std::format("removing it gives the light back: {} and {}", sky(11 - 64), sky(6 - 64)));

    std::filesystem::remove(path);
}

// The world's buffers are handed out and taken back for as long as the player
// moves. Freed room has to be found again and merged, or a long session ends
// with plenty of room in total and none of it in one piece.
void test_span_allocator() {
    mcpp::gpu::SpanAllocator spans;
    spans.reset(100);
    const auto a = spans.allocate(30);
    const auto b = spans.allocate(30);
    const auto c = spans.allocate(30);
    check(a == 0u && b == 30u && c == 60u, "spans are handed out front to back");
    check(!spans.allocate(20).has_value(), "and run out when only ten are left");

    spans.release(30, 30);
    check(spans.allocate(25) == 30u, "a freed span is found again");
    spans.release(30, 25);

    // Free both neighbours of the middle span, then the middle one: all three
    // must come back as a single span, not three that each fit nothing.
    spans.release(0, 30);
    spans.release(60, 30);
    check(spans.spans() == 1 && spans.largest_free() == 100,
          std::format("freed neighbours merge into one span ({} spans, largest {})",
                      spans.spans(), spans.largest_free()));
    check(spans.used() == 0, "and nothing is counted as used");
    check(spans.allocate(100) == 0u, "so the whole buffer fits again");
}

void test_attributes_and_tags() {
    // Speed II on a sprinting player: the base, the sprint's +30% of the
    // total, and the effect's +20% per level, also of the total.
    mcpp::Attribute speed;
    speed.base = 0.1;
    speed.modifiers = {{"minecraft:sprinting", 0.3, 2}, {"minecraft:effect.speed", 0.4, 2}};
    check(std::abs(speed.value() - 0.182) < 1e-9,
          std::format("multipliers on the total compound: {:.4f}", speed.value()));

    // Additions are summed before anything multiplies, and a multiple of the
    // base is a multiple of that sum, not of each other.
    mcpp::Attribute mixed;
    mixed.base = 4.0;
    mixed.modifiers = {{"a", 2.0, 0}, {"b", 0.5, 1}, {"c", 0.5, 1}, {"d", 1.0, 2}};
    check(std::abs(mixed.value() - 24.0) < 1e-9,
          std::format("add, then multiply the base, then the total: {:.2f}", mixed.value()));

    mcpp::Tags tags;
    tags.set("minecraft:block", {{"minecraft:mineable/pickaxe", {1, 2, 3}}});
    check(tags.contains("minecraft:block", "minecraft:mineable/pickaxe", 2),
          "a block in a tag is found in it");
    check(!tags.contains("minecraft:block", "minecraft:mineable/pickaxe", 9) &&
              !tags.contains("minecraft:item", "minecraft:mineable/pickaxe", 2),
          "and nothing else is");
}

int main() {
    test_varint_encoding();
    test_varlong_and_numbers();
    test_strings_and_underflow();
    test_json();
    test_nbt();
    test_translation_patterns();
    test_paletted_container();
    test_block_table();
    test_mesher_culls_interiors();
    test_light_engine();
    test_span_allocator();
    test_attributes_and_tags();
    test_packet_tables();
    test_server_hash();
    test_stream_cipher();

    if (failures == 0) {
        std::cout << "all protocol tests passed\n";
        return 0;
    }
    std::cerr << std::format("{} check(s) failed\n", failures);
    return 1;
}
