#include "mcpp/crypto.hpp"
#include "mcpp/byte_buffer.hpp"
#include "mcpp/json.hpp"
#include "mcpp/nbt.hpp"
#include "mcpp/proto/packet_ids.hpp"
#include "mcpp/world/block_table.hpp"
#include "mcpp/world/chunk.hpp"
#include "mcpp/world/mesher.hpp"
#include "mcpp/world/physics.hpp"

#include <cassert>
#include <filesystem>
#include <format>
#include <fstream>
#include <initializer_list>
#include <map>
#include <span>
#include <iostream>
#include <string>
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

// Ground at y below 64, open air above. Outside the columns we know about the
// world is unloaded, which physics must treat as solid.
void test_physics_stands_and_stops() {
    const auto path = write_cube_table("plasma-test-physics.plbk");
    mcpp::world::BlockTable table;
    check(table.open(path), "the physics table opens");

    const auto flat = [](int32_t, int32_t y, int32_t) -> int32_t { return y < 64 ? 1 : 0; };

    mcpp::world::Body body;
    body.x = 8.5;
    body.y = 70.0;
    body.z = 8.5;
    for (int i = 0; i < 200; ++i) {
        mcpp::world::step(body, {}, flat, table, 1.0 / 60.0);
    }
    check(std::abs(body.y - 64.0) < 0.01,
          std::format("a falling player lands on the floor, not through it: y={:.3f}", body.y));
    check(body.on_ground, "and knows it is standing");

    // A wall at x >= 10 stops a walk east without stopping the fall.
    const auto walled = [](int32_t x, int32_t y, int32_t) -> int32_t {
        if (y < 64) return 1;
        return x >= 10 ? 1 : 0;
    };
    body = {};
    body.x = 8.5;
    body.y = 64.0;
    body.z = 8.5;
    mcpp::world::Intent intent;
    intent.forward = 1.0;
    intent.yaw_degrees = 90.0;    // facing east
    for (int i = 0; i < 200; ++i) {
        mcpp::world::step(body, intent, walled, table, 1.0 / 60.0);
    }
    check(body.x < 10.0 && body.x > 9.0,
          std::format("a wall stops the walk at its face: x={:.3f}", body.x));
    check(std::abs(body.y - 64.0) < 0.01, "and the player is still standing on the floor");

    // Jumping leaves the ground and comes back to it.
    intent = {};
    intent.jump = true;
    mcpp::world::step(body, intent, walled, table, 1.0 / 60.0);
    check(!body.on_ground, "a jump leaves the ground");
    intent.jump = false;
    for (int i = 0; i < 200; ++i) {
        mcpp::world::step(body, intent, walled, table, 1.0 / 60.0);
    }
    check(body.on_ground && std::abs(body.y - 64.0) < 0.01, "and lands back on it");

    std::filesystem::remove(path);
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
    test_physics_stands_and_stops();
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
