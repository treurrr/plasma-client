#include "mcpp/entity/assets.hpp"

#include <cstdlib>
#include <cstring>
#include <format>
#include <fstream>

#include <zlib.h>

namespace mcpp::entity {
namespace {

struct Cursor {
    const std::vector<uint8_t>& bytes;
    size_t at = 0;

    bool has(size_t count) const { return at + count <= bytes.size(); }
    uint32_t u32() {
        uint32_t v = 0;
        std::memcpy(&v, bytes.data() + at, 4);
        at += 4;
        return v;
    }
    uint16_t u16() {
        uint16_t v = 0;
        std::memcpy(&v, bytes.data() + at, 2);
        at += 2;
        return v;
    }
    uint8_t u8() { return bytes[at++]; }
};

} // namespace

float Font::width(std::string_view text) const {
    float total = 0.0f;
    for (const char c : text) total += advance[static_cast<uint8_t>(c)];
    return total;
}

std::filesystem::path Assets::default_path(int32_t protocol) {
    std::filesystem::path base;
    if (const char* data_home = std::getenv("XDG_DATA_HOME")) {
        base = data_home;
    } else if (const char* home = std::getenv("HOME")) {
        base = std::filesystem::path(home) / ".local" / "share";
    }
    return base / "plasma-client" / "data" / std::format("entities-{}.plen", protocol);
}

bool Assets::open(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    char magic[4]{};
    uint32_t version = 0;
    uint32_t size = 0;
    input.read(magic, 4);
    input.read(reinterpret_cast<char*>(&version), 4);
    input.read(reinterpret_cast<char*>(&size), 4);
    if (!input || std::memcmp(magic, "PLEN", 4) != 0 || version != 1) return false;
    const std::vector<uint8_t> packed((std::istreambuf_iterator<char>(input)),
                                      std::istreambuf_iterator<char>());
    std::vector<uint8_t> bytes(size);
    uLongf length = size;
    if (uncompress(bytes.data(), &length, packed.data(), static_cast<uLong>(packed.size())) !=
            Z_OK ||
        length != size) {
        return false;
    }

    Cursor in{bytes};
    if (!in.has(4)) return false;
    const uint32_t text_size = in.u32();
    if (!in.has(text_size)) return false;
    models_ = ModelSet{};
    models_.parse(std::string_view(reinterpret_cast<const char*>(bytes.data() + in.at), text_size));
    in.at += text_size;

    if (!in.has(4)) return false;
    const uint32_t count = in.u32();
    for (uint32_t i = 0; i < count; ++i) {
        if (!in.has(2)) return false;
        const uint16_t name_size = in.u16();
        if (!in.has(name_size + 4u)) return false;
        std::string name(reinterpret_cast<const char*>(bytes.data() + in.at), name_size);
        in.at += name_size;
        Texture texture;
        texture.width = in.u16();
        texture.height = in.u16();
        const size_t pixels = size_t{texture.width} * texture.height * 4;
        if (!in.has(pixels)) return false;
        texture.rgba.assign(bytes.begin() + static_cast<std::ptrdiff_t>(in.at),
                            bytes.begin() + static_cast<std::ptrdiff_t>(in.at + pixels));
        in.at += pixels;
        textures_.insert_or_assign(std::move(name), std::move(texture));
    }

    if (!in.has(7 + 256)) return false;
    font_.sheet.width = in.u16();
    font_.sheet.height = in.u16();
    font_.cell_width = in.u8();
    font_.cell_height = in.u8();
    font_.ascent = in.u8();
    std::memcpy(font_.advance, bytes.data() + in.at, 256);
    in.at += 256;
    const size_t font_pixels = size_t{font_.sheet.width} * font_.sheet.height * 4;
    if (!in.has(font_pixels)) return false;
    font_.sheet.rgba.assign(bytes.begin() + static_cast<std::ptrdiff_t>(in.at),
                            bytes.begin() + static_cast<std::ptrdiff_t>(in.at + font_pixels));
    return true;
}

void Assets::use_models(std::string_view text) {
    models_ = ModelSet{};
    models_.parse(text);
}

const Texture* Assets::texture(std::string_view name) const {
    const auto found = textures_.find(name);
    return found == textures_.end() ? nullptr : &found->second;
}

} // namespace mcpp::entity
