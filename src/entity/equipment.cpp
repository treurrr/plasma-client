#include "mcpp/entity/equipment.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <format>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include <zlib.h>

namespace mcpp::entity {
namespace {

constexpr std::string_view kLayerTypeNames[kLayerTypes] = {
    "humanoid", "humanoid_leggings", "wings", "wolf_body", "horse_body", "llama_body",
    "pig_saddle", "strider_saddle", "camel_saddle", "camel_husk_saddle", "horse_saddle",
    "donkey_saddle", "mule_saddle", "zombie_horse_saddle", "skeleton_horse_saddle",
    "happy_ghast_body", "nautilus_saddle", "nautilus_body"};

std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    return text;
}

std::string_view next_word(std::string_view& line) {
    const size_t gap = line.find(' ');
    const std::string_view word = line.substr(0, gap);
    line = gap == std::string_view::npos ? std::string_view{} : trim(line.substr(gap + 1));
    return word;
}

bool to_int(std::string_view text, int32_t& out, int base = 10) {
    const auto end = text.data() + text.size();
    const auto result = std::from_chars(text.data(), end, out, base);
    return result.ec == std::errc{} && result.ptr == end;
}

bool to_colour(std::string_view text, uint32_t& out) {
    const auto end = text.data() + text.size();
    const auto result = std::from_chars(text.data(), end, out, 16);
    return result.ec == std::errc{} && result.ptr == end;
}

std::filesystem::path data_directory() {
    std::filesystem::path base;
    if (const char* data_home = std::getenv("XDG_DATA_HOME")) {
        base = data_home;
    } else if (const char* home = std::getenv("HOME")) {
        base = std::filesystem::path(home) / ".local" / "share";
    }
    return base / "plasma-client" / "data";
}

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
};

bool inflate_body(std::ifstream& input, const char (&magic)[5], uint32_t wanted_version,
                  std::vector<uint8_t>& out) {
    char seen[4]{};
    uint32_t version = 0;
    uint32_t size = 0;
    input.read(seen, 4);
    input.read(reinterpret_cast<char*>(&version), 4);
    input.read(reinterpret_cast<char*>(&size), 4);
    if (!input || std::memcmp(seen, magic, 4) != 0 || version != wanted_version) return false;
    const std::vector<uint8_t> packed((std::istreambuf_iterator<char>(input)),
                                      std::istreambuf_iterator<char>());
    out.assign(size, 0);
    uLongf length = size;
    return uncompress(out.data(), &length, packed.data(), static_cast<uLong>(packed.size())) ==
               Z_OK &&
           length == size;
}

} // namespace

std::string_view layer_type_name(LayerType type) {
    const auto index = static_cast<size_t>(type);
    return index < kLayerTypes ? kLayerTypeNames[index] : std::string_view{};
}

// `EquipmentLayerRenderer.getColorForLayer`: a layer with no `dyeable` is drawn
// plain white, a dyeable one in the stack's own dye or else in the colour it
// names for being undyed - which may be none at all, and then it is not drawn.
uint32_t equipment_layer_colour(const EquipmentLayer& layer, uint32_t dyed) {
    if (!layer.dyeable) return 0xFFFFFFFFu;
    return dyed != 0 ? dyed : layer.colour_when_undyed;
}

std::filesystem::path EquipmentAssets::default_path(int32_t protocol) {
    return data_directory() / std::format("equipment-{}.txt", protocol);
}

bool EquipmentAssets::open(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) return false;
    std::stringstream text;
    text << input.rdbuf();
    parse(text.str());
    return true;
}

std::string_view EquipmentAssets::intern(std::string_view asset) {
    return assets_.try_emplace(std::string(asset)).first->first;
}

void EquipmentAssets::parse(std::string_view text) {
    Asset* current = nullptr;
    int32_t last_type = -1;
    size_t number = 0;
    while (!text.empty()) {
        ++number;
        const size_t end = text.find('\n');
        std::string_view line = trim(text.substr(0, end));
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        if (line.empty() || line.front() == '#') continue;

        if (line.front() == '[') {
            if (line.back() != ']') {
                throw std::runtime_error(std::format("equipment, line {}: unclosed section",
                                                     number));
            }
            line = trim(line.substr(1, line.size() - 2));
            const std::string_view kind = next_word(line);
            current = nullptr;
            last_type = -1;
            if (kind == "asset") {
                if (line.empty()) {
                    throw std::runtime_error(std::format("equipment, line {}: asset with no id",
                                                         number));
                }
                current = &assets_.try_emplace(std::string(line)).first->second;
            } else if (kind != "items") {
                throw std::runtime_error(
                    std::format("equipment, line {}: unknown section {}", number, kind));
            }
            continue;
        }

        const std::string_view keyword = next_word(line);
        if (keyword == "layer") {
            if (current == nullptr) {
                throw std::runtime_error(std::format("equipment, line {}: layer outside an asset",
                                                     number));
            }
            const std::string_view type_name = next_word(line);
            const auto found = std::find(kLayerTypeNames, kLayerTypeNames + kLayerTypes,
                                         type_name);
            const std::string_view texture = next_word(line);
            const std::string_view dyeable = next_word(line);
            const std::string_view colour = next_word(line);
            const std::string_view player = next_word(line);
            EquipmentLayer layer;
            layer.texture = texture;
            layer.dyeable = dyeable == "1";
            layer.use_player_texture = player == "1";
            if (found == kLayerTypeNames + kLayerTypes || texture.empty() ||
                !to_colour(colour, layer.colour_when_undyed)) {
                throw std::runtime_error(std::format("equipment, line {}: bad layer", number));
            }
            const auto type = static_cast<int32_t>(found - kLayerTypeNames);
            Range& range = current->by_type[type];
            if (type != last_type && range.count != 0) {
                throw std::runtime_error(
                    std::format("equipment, line {}: {} is split in two", number, type_name));
            }
            if (range.count == 0) range.at = static_cast<uint32_t>(layers_.size());
            ++range.count;
            last_type = type;
            layers_.push_back(std::move(layer));
        } else if (keyword == "item") {
            int32_t item = 0;
            int32_t slot = 0;
            const std::string_view id = next_word(line);
            const std::string_view asset = next_word(line);
            if (!to_int(id, item) || asset.empty() || !to_int(next_word(line), slot) ||
                slot < 0 || slot > 7) {
                throw std::runtime_error(std::format("equipment, line {}: bad item", number));
            }
            equippable_[item] = Equippable{intern(asset), static_cast<uint8_t>(slot)};
        } else {
            throw std::runtime_error(
                std::format("equipment, line {}: unknown row {}", number, keyword));
        }
    }
}

std::span<const EquipmentLayer> EquipmentAssets::layers(std::string_view asset,
                                                        LayerType type) const {
    const auto found = assets_.find(asset);
    const auto index = static_cast<size_t>(type);
    if (found == assets_.end() || index >= kLayerTypes) return {};
    const Range& range = found->second.by_type[index];
    return std::span(layers_).subspan(range.at, range.count);
}

const EquipmentAssets::Equippable* EquipmentAssets::equippable(int32_t item) const {
    const auto found = equippable_.find(item);
    return found == equippable_.end() ? nullptr : &found->second;
}

std::filesystem::path EquipmentTrims::default_path(int32_t protocol) {
    return data_directory() / std::format("equipment_trims-{}.plat", protocol);
}

// `ArmorTrim.layerAssetId`: the pattern's own asset id, under the layer type's
// trim prefix, with the material's suffix on it.
std::string EquipmentTrims::sprite_id(LayerType type, std::string_view pattern,
                                      std::string_view material) {
    return std::format("trims/entity/{}/{}_{}", layer_type_name(type), pattern, material);
}

bool EquipmentTrims::open(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    std::vector<uint8_t> bytes;
    if (!inflate_body(input, "PLTR", 1, bytes)) return false;

    Cursor in{bytes};
    if (!in.has(4)) return false;
    const uint32_t count = in.u32();
    for (uint32_t i = 0; i < count; ++i) {
        if (!in.has(2)) return false;
        const uint16_t name_size = in.u16();
        if (!in.has(name_size + 4u)) return false;
        std::string name(reinterpret_cast<const char*>(bytes.data() + in.at), name_size);
        in.at += name_size;
        Texture sprite;
        sprite.width = in.u16();
        sprite.height = in.u16();
        const size_t pixels = size_t{sprite.width} * sprite.height * 4;
        if (!in.has(pixels)) return false;
        sprite.rgba.assign(bytes.begin() + static_cast<ptrdiff_t>(in.at),
                           bytes.begin() + static_cast<ptrdiff_t>(in.at + pixels));
        in.at += pixels;
        sprites_.insert_or_assign(std::move(name), std::move(sprite));
    }
    return true;
}

const Texture* EquipmentTrims::sprite(std::string_view id) const {
    const auto found = sprites_.find(id);
    return found == sprites_.end() ? nullptr : &found->second;
}

} // namespace mcpp::entity
