#include "mcpp/ui/server_list.hpp"

#include "mcpp/byte_buffer.hpp"
#include "mcpp/nbt.hpp"

#include <cstdlib>
#include <fstream>
#include <iterator>

namespace mcpp::ui {
namespace {

constexpr uint8_t kByte = 1;
constexpr uint8_t kString = 8;
constexpr uint8_t kList = 9;
constexpr uint8_t kCompound = 10;

void name(ByteBuffer& out, std::string_view text) {
    out.write_u16(static_cast<uint16_t>(text.size()));
    out.write_bytes(std::span(reinterpret_cast<const uint8_t*>(text.data()), text.size()));
}

void string_field(ByteBuffer& out, std::string_view key, std::string_view value) {
    out.write_u8(kString);
    name(out, key);
    name(out, value);
}

void byte_field(ByteBuffer& out, std::string_view key, uint8_t value) {
    out.write_u8(kByte);
    name(out, key);
    out.write_u8(value);
}

std::filesystem::path data_home() {
    if (const char* data = std::getenv("XDG_DATA_HOME")) return data;
    if (const char* home = std::getenv("HOME")) {
        return std::filesystem::path(home) / ".local" / "share";
    }
    return {};
}

constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

} // namespace

std::vector<size_t> ServerList::visible() const {
    std::vector<size_t> out;
    for (size_t i = 0; i < entries.size(); ++i) {
        if (!entries[i].hidden) out.push_back(i);
    }
    return out;
}

// `ServerList.load`, `ServerData.read`: each entry's name, ip, icon, pack status and hidden flag.
std::optional<ServerList> ServerList::decode(std::span<const uint8_t> bytes) {
    // The file's root is named, where the network form is not; dropping the
    // name turns one into the other.
    if (bytes.size() < 3 || bytes[0] != kCompound) return std::nullopt;
    const size_t name_length = (static_cast<size_t>(bytes[1]) << 8) | bytes[2];
    if (3 + name_length > bytes.size()) return std::nullopt;
    std::vector<uint8_t> network;
    network.push_back(kCompound);
    network.insert(network.end(), bytes.begin() + static_cast<std::ptrdiff_t>(3 + name_length),
                   bytes.end());
    ServerList list;
    try {
        ByteBuffer in(std::move(network));
        const auto root = nbt::read_network_tag(in);
        const auto* servers = root.find("servers");
        if (servers == nullptr || servers->as_list() == nullptr) return list;
        for (const auto& item : *servers->as_list()) {
            ServerEntry entry;
            if (const auto* v = item.find("name")) entry.name = v->string_or("");
            if (const auto* v = item.find("ip")) entry.ip = v->string_or("");
            if (const auto* v = item.find("icon")) entry.icon = v->string_or("");
            if (const auto* v = item.find("acceptTextures")) {
                entry.packs = v->as_int().value_or(0) != 0 ? ServerEntry::Packs::Enabled
                                                           : ServerEntry::Packs::Disabled;
            }
            if (const auto* v = item.find("hidden")) entry.hidden = v->as_int().value_or(0) != 0;
            list.entries.push_back(std::move(entry));
        }
    } catch (const std::exception&) {
        return std::nullopt;
    }
    return list;
}

std::vector<uint8_t> ServerList::encode() const {
    ByteBuffer out;
    out.write_u8(kCompound);
    name(out, "");
    out.write_u8(kList);
    name(out, "servers");
    out.write_u8(kCompound);
    out.write_i32(static_cast<int32_t>(entries.size()));
    for (const auto& entry : entries) {
        byte_field(out, "hidden", entry.hidden ? 1 : 0);
        string_field(out, "ip", entry.ip);
        string_field(out, "name", entry.name);
        if (!entry.icon.empty()) string_field(out, "icon", entry.icon);
        if (entry.packs != ServerEntry::Packs::Prompt) {
            byte_field(out, "acceptTextures", entry.packs == ServerEntry::Packs::Enabled ? 1 : 0);
        }
        out.write_u8(0);
    }
    out.write_u8(0);
    return out.data();
}

bool ServerList::load(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                                     std::istreambuf_iterator<char>());
    auto decoded = decode(bytes);
    if (!decoded) return false;
    entries = std::move(decoded->entries);
    return true;
}

bool ServerList::save(const std::filesystem::path& path) const {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    const auto bytes = encode();
    const auto temporary = path.string() + ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) return false;
        file.write(reinterpret_cast<const char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
        if (!file) return false;
    }
    std::filesystem::rename(temporary, path, error);
    return !error;
}

std::filesystem::path ServerList::default_path() {
    return data_home() / "plasma-client" / "servers.dat";
}

std::optional<std::filesystem::path> ServerList::vanilla_path() {
    std::vector<std::filesystem::path> roots;
    if (const char* chosen = std::getenv("PLASMA_MINECRAFT")) roots.emplace_back(chosen);
    if (const char* home = std::getenv("HOME")) {
        roots.push_back(std::filesystem::path(home) / ".minecraft");
        roots.push_back(std::filesystem::path(home) / ".var/app/com.mojang.Minecraft/.minecraft");
    }
    for (const auto& root : roots) {
        std::error_code error;
        if (std::filesystem::is_regular_file(root / "servers.dat", error)) {
            return root / "servers.dat";
        }
    }
    return std::nullopt;
}

std::vector<uint8_t> base64_decode(std::string_view text) {
    std::vector<uint8_t> out;
    uint32_t buffer = 0;
    int bits = 0;
    for (const char c : text) {
        int value = -1;
        if (c >= 'A' && c <= 'Z') value = c - 'A';
        else if (c >= 'a' && c <= 'z') value = c - 'a' + 26;
        else if (c >= '0' && c <= '9') value = c - '0' + 52;
        else if (c == '+' || c == '-') value = 62;
        else if (c == '/' || c == '_') value = 63;
        else if (c == '=') break;
        else continue;
        buffer = (buffer << 6) | static_cast<uint32_t>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((buffer >> bits) & 0xFFu));
        }
    }
    return out;
}

std::string base64_encode(std::span<const uint8_t> bytes) {
    std::string out;
    size_t i = 0;
    for (; i + 2 < bytes.size(); i += 3) {
        const uint32_t v = (uint32_t{bytes[i]} << 16) | (uint32_t{bytes[i + 1]} << 8) | bytes[i + 2];
        out += kAlphabet[(v >> 18) & 63];
        out += kAlphabet[(v >> 12) & 63];
        out += kAlphabet[(v >> 6) & 63];
        out += kAlphabet[v & 63];
    }
    if (i + 1 == bytes.size()) {
        const uint32_t v = uint32_t{bytes[i]} << 16;
        out += kAlphabet[(v >> 18) & 63];
        out += kAlphabet[(v >> 12) & 63];
        out += "==";
    } else if (i + 2 == bytes.size()) {
        const uint32_t v = (uint32_t{bytes[i]} << 16) | (uint32_t{bytes[i + 1]} << 8);
        out += kAlphabet[(v >> 18) & 63];
        out += kAlphabet[(v >> 12) & 63];
        out += kAlphabet[(v >> 6) & 63];
        out += '=';
    }
    return out;
}

} // namespace mcpp::ui
