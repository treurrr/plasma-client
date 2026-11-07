#include "mcpp/ui/server_list.hpp"

#include "mcpp/byte_buffer.hpp"
#include "mcpp/nbt.hpp"

#include <cstdlib>
#include <algorithm>
#include <fstream>
#include <iterator>
#include <type_traits>
#include <variant>

namespace mcpp::ui {
namespace {

constexpr uint8_t kEnd = 0;
constexpr uint8_t kByte = 1;
constexpr uint8_t kShort = 2;
constexpr uint8_t kInt = 3;
constexpr uint8_t kLong = 4;
constexpr uint8_t kFloat = 5;
constexpr uint8_t kDouble = 6;
constexpr uint8_t kByteArray = 7;
constexpr uint8_t kString = 8;
constexpr uint8_t kList = 9;
constexpr uint8_t kCompound = 10;
constexpr uint8_t kIntArray = 11;
constexpr uint8_t kLongArray = 12;

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

// The tag number of a value as it was read. An empty list lost the type of
// the elements it never had, so it goes back as a list of nothing, which is
// how the game writes an empty list itself.
uint8_t tag_of(const nbt::Value& value) {
    return std::visit(
        [](const auto& held) -> uint8_t {
            using T = std::decay_t<decltype(held)>;
            if constexpr (std::is_same_v<T, std::monostate>) return kEnd;
            else if constexpr (std::is_same_v<T, int8_t>) return kByte;
            else if constexpr (std::is_same_v<T, int16_t>) return kShort;
            else if constexpr (std::is_same_v<T, int32_t>) return kInt;
            else if constexpr (std::is_same_v<T, int64_t>) return kLong;
            else if constexpr (std::is_same_v<T, float>) return kFloat;
            else if constexpr (std::is_same_v<T, double>) return kDouble;
            else if constexpr (std::is_same_v<T, std::string>) return kString;
            else if constexpr (std::is_same_v<T, nbt::List>) return kList;
            else if constexpr (std::is_same_v<T, nbt::Compound>) return kCompound;
            else if constexpr (std::is_same_v<T, std::vector<uint8_t>>) return kByteArray;
            else if constexpr (std::is_same_v<T, std::vector<int32_t>>) return kIntArray;
            else return kLongArray;
        },
        value.storage());
}

void write_payload(ByteBuffer& out, const nbt::Value& value);

void write_field(ByteBuffer& out, std::string_view key, const nbt::Value& value) {
    out.write_u8(tag_of(value));
    name(out, key);
    write_payload(out, value);
}

void write_payload(ByteBuffer& out, const nbt::Value& value) {
    std::visit(
        [&out](const auto& held) {
            using T = std::decay_t<decltype(held)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return;
            } else if constexpr (std::is_same_v<T, int8_t>) {
                out.write_u8(static_cast<uint8_t>(held));
            } else if constexpr (std::is_same_v<T, int16_t>) {
                out.write_u16(static_cast<uint16_t>(held));
            } else if constexpr (std::is_same_v<T, int32_t>) {
                out.write_i32(held);
            } else if constexpr (std::is_same_v<T, int64_t>) {
                out.write_i64(held);
            } else if constexpr (std::is_same_v<T, float>) {
                out.write_f32(held);
            } else if constexpr (std::is_same_v<T, double>) {
                out.write_f64(held);
            } else if constexpr (std::is_same_v<T, std::string>) {
                name(out, held);
            } else if constexpr (std::is_same_v<T, nbt::List>) {
                out.write_u8(held.empty() ? kEnd : tag_of(held.front()));
                out.write_i32(static_cast<int32_t>(held.size()));
                for (const auto& item : held) write_payload(out, item);
            } else if constexpr (std::is_same_v<T, nbt::Compound>) {
                for (const auto& [key, field] : held) write_field(out, key, field);
                out.write_u8(kEnd);
            } else if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
                out.write_i32(static_cast<int32_t>(held.size()));
                out.write_bytes(std::span(held.data(), held.size()));
            } else if constexpr (std::is_same_v<T, std::vector<int32_t>>) {
                out.write_i32(static_cast<int32_t>(held.size()));
                for (const int32_t item : held) out.write_i32(item);
            } else {
                out.write_i32(static_cast<int32_t>(held.size()));
                for (const int64_t item : held) out.write_i64(item);
            }
        },
        value.storage());
}

} // namespace

std::vector<size_t> ServerList::visible() const {
    std::vector<size_t> out;
    for (size_t i = 0; i < entries.size(); ++i) {
        if (!entries[i].hidden) out.push_back(i);
    }
    return out;
}

// `ServerList.load`, `ServerData.read`: each entry's name, ip, icon, pack status and hidden flag, and every other field kept as it was.
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
        if (const auto* fields = root.as_compound()) list.root_extra = *fields;
        list.root_extra.erase("servers");
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
            // Every field a newer game may have written, kept whole; the five
            // this client knows are written from its own members instead.
            if (const auto* fields = item.as_compound()) {
                nbt::Compound rest = *fields;
                for (const char* known : {"name", "ip", "icon", "acceptTextures", "hidden"}) {
                    rest.erase(known);
                }
                entry.extra = nbt::Value{std::move(rest)};
            }
            list.entries.push_back(std::move(entry));
        }
    } catch (const std::exception&) {
        return std::nullopt;
    }
    return list;
}

// `ServerList.save`, `ServerData.write`: name, ip, the icon when there is one, the pack status when it is not Prompt, the hidden flag, and the fields this client does not know, back where they were.
std::vector<uint8_t> ServerList::encode() const {
    ByteBuffer out;
    out.write_u8(kCompound);
    name(out, "");
    for (const auto& [key, field] : root_extra) write_field(out, key, field);
    out.write_u8(kList);
    name(out, "servers");
    out.write_u8(kCompound);
    out.write_i32(static_cast<int32_t>(entries.size()));
    for (const auto& entry : entries) {
        nbt::Compound fields;
        if (const auto* rest = entry.extra.as_compound()) fields = *rest;
        for (const char* known : {"name", "ip", "icon", "acceptTextures", "hidden"}) {
            fields.erase(known);
        }
        fields.emplace("name", nbt::Value{entry.name});
        fields.emplace("ip", nbt::Value{entry.ip});
        if (!entry.icon.empty()) fields.emplace("icon", nbt::Value{entry.icon});
        if (entry.packs != ServerEntry::Packs::Prompt) {
            fields.emplace("acceptTextures",
                           nbt::Value{static_cast<int8_t>(
                               entry.packs == ServerEntry::Packs::Enabled ? 1 : 0)});
        }
        fields.emplace("hidden", nbt::Value{static_cast<int8_t>(entry.hidden ? 1 : 0)});
        for (const auto& [key, field] : fields) write_field(out, key, field);
        out.write_u8(kEnd);
    }
    out.write_u8(kEnd);
    return out.data();
}

ServerEntry* ServerList::find(std::string_view ip) {
    for (auto& entry : entries) {
        if (entry.ip == ip) return &entry;
    }
    return nullptr;
}

// `ServerList.unhide`: a hidden entry with that address becomes a listed one.
ServerEntry* ServerList::unhide(std::string_view ip) {
    for (auto& entry : entries) {
        if (entry.hidden && entry.ip == ip) {
            entry.hidden = false;
            return &entry;
        }
    }
    return nullptr;
}

// `ServerList.add`: a listed entry goes last, a hidden one first, and no more
// than sixteen hidden ones are kept.
void ServerList::add(ServerEntry entry, bool hide) {
    entry.hidden = hide;
    if (!hide) {
        entries.push_back(std::move(entry));
        return;
    }
    entries.insert(entries.begin(), std::move(entry));
    size_t hidden_count = 0;
    for (size_t i = 0; i < entries.size();) {
        if (!entries[i].hidden) {
            ++i;
            continue;
        }
        ++hidden_count;
        if (hidden_count > 16) {
            entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        ++i;
    }
}

void ServerList::remove(size_t index) {
    if (index < entries.size()) entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(index));
}

// `ServerList.swap`: two listed entries trade places.
void ServerList::swap(size_t a, size_t b) {
    const auto shown = visible();
    if (a >= shown.size() || b >= shown.size()) return;
    std::swap(entries[shown[a]], entries[shown[b]]);
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
