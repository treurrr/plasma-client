#include "mcpp/byte_buffer.hpp"
#include "mcpp/json.hpp"
#include "mcpp/nbt.hpp"
#include "mcpp/proto/packet_ids.hpp"
#include "mcpp/world/chunk.hpp"

#include <cassert>
#include <format>
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
    test_packet_tables();

    if (failures == 0) {
        std::cout << "all protocol tests passed\n";
        return 0;
    }
    std::cerr << std::format("{} check(s) failed\n", failures);
    return 1;
}
