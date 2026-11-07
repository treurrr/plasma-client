#include "mcpp/packs/resource_pack.hpp"

#include "mcpp/json.hpp"

#include <algorithm>
#include <format>

namespace mcpp::packs {
namespace {

constexpr PackFormat kOurFormat{kPackFormatMajor, kPackFormatMinor};

// `PackFormat.fullCodec`: a format is one number, or a pair, and a bare number
// takes the bottom or the top of its major version depending on which end of
// the range it is.
std::optional<PackFormat> read_format(const json::Value& value, int bare_minor) {
    if (const auto number = value.as_number()) {
        return PackFormat{static_cast<int>(*number), bare_minor};
    }
    if (const auto* array = value.as_array(); array != nullptr && !array->empty()) {
        const auto major = (*array)[0].as_number();
        if (!major.has_value()) return std::nullopt;
        int minor = bare_minor;
        if (array->size() > 1) {
            const auto found = (*array)[1].as_number();
            if (!found.has_value()) return std::nullopt;
            minor = static_cast<int>(*found);
        }
        return PackFormat{static_cast<int>(*major), minor};
    }
    return std::nullopt;
}

constexpr int kTopMinor = 0x7FFFFFFF;

// `PackFormat.IntermediaryFormat.validate`, in the shape a pack actually
// writes it: min_format/max_format when it was made for 65 or later, a
// supported_formats range or a bare pack_format when it was made for 64 or
// earlier. The error cases vanilla distinguishes all end the same way here -
// the range stays empty and the pack counts as incompatible.
std::pair<PackFormat, PackFormat> read_range(const json::Object& section, const char* range_key) {
    const json::Value* min = section.count("min_format") ? &section.at("min_format") : nullptr;
    const json::Value* max = section.count("max_format") ? &section.at("max_format") : nullptr;
    if (min != nullptr && max != nullptr) {
        const auto low = read_format(*min, 0);
        const auto high = read_format(*max, kTopMinor);
        if (low.has_value() && high.has_value() && *low <= *high) return {*low, *high};
        return {PackFormat{1, 0}, PackFormat{0, 0}};
    }
    if (section.count(range_key) != 0) {
        const json::Value& supported = section.at(range_key);
        if (const auto* array = supported.as_array(); array != nullptr && array->size() == 2) {
            const auto low = (*array)[0].as_number();
            const auto high = (*array)[1].as_number();
            if (low.has_value() && high.has_value()) {
                return {PackFormat{static_cast<int>(*low), 0},
                        PackFormat{static_cast<int>(*high), kTopMinor}};
            }
        }
        if (const auto number = supported.as_number()) {
            return {PackFormat{static_cast<int>(*number), 0},
                    PackFormat{static_cast<int>(*number), kTopMinor}};
        }
        return {PackFormat{1, 0}, PackFormat{0, 0}};
    }
    if (section.count("pack_format") != 0) {
        const auto number = section.at("pack_format").as_number();
        if (!number.has_value()) return {PackFormat{1, 0}, PackFormat{0, 0}};
        const int format = static_cast<int>(*number);
        // A bare pack_format past the last pre-minor version is an error in
        // vanilla; such a pack has to declare min_format and max_format.
        if (format > kLastPreMinorFormat) return {PackFormat{1, 0}, PackFormat{0, 0}};
        return {PackFormat{format, 0}, PackFormat{format, kTopMinor}};
    }
    return {PackFormat{1, 0}, PackFormat{0, 0}};
}

} // namespace

// `OverlayMetadataSection.validateOverlayDir`.
bool overlay_dir_valid(std::string_view name) {
    if (name.empty()) return false;
    for (const char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
        if (!ok) return false;
    }
    return true;
}

bool ResourcePack::open(const std::filesystem::path& path, const ZipLimits& limits) {
    close();
    if (!zip_.open_file(path, limits)) {
        error_ = zip_.error();
        return false;
    }
    return read_metadata();
}

bool ResourcePack::open_bytes(std::vector<uint8_t> bytes, const ZipLimits& limits) {
    close();
    if (!zip_.open(std::move(bytes), limits)) {
        error_ = zip_.error();
        return false;
    }
    return read_metadata();
}

void ResourcePack::close() {
    zip_.close();
    meta_ = {};
    search_.clear();
    open_ = false;
    error_.clear();
}

// `Pack.readPackMetadata`: a pack with no readable pack.mcmeta is not a pack,
// and the game refuses the whole reload rather than show half of it.
bool ResourcePack::read_metadata() {
    const auto bytes = zip_.read("pack.mcmeta");
    if (!bytes.has_value()) {
        error_ = "pack.mcmeta is missing or damaged";
        return false;
    }
    json::Value root;
    try {
        root = json::parse(std::string_view(reinterpret_cast<const char*>(bytes->data()),
                                            bytes->size()));
    } catch (const std::exception& problem) {
        error_ = std::format("pack.mcmeta is not readable json: {}", problem.what());
        return false;
    }
    const json::Value* pack = root.find("pack");
    const json::Object* section = pack != nullptr ? pack->as_object() : nullptr;
    if (section == nullptr) {
        error_ = "pack.mcmeta has no pack section";
        return false;
    }
    if (const json::Value* description = pack->find("description"); description != nullptr) {
        meta_.description = json::flatten_component(*description);
    }
    const auto [min, max] = read_range(*section, "supported_formats");
    meta_.min = min;
    meta_.max = max;
    meta_.compatible = min <= kOurFormat && kOurFormat <= max;

    if (const json::Value* overlays = root.find("overlays"); overlays != nullptr) {
        if (const json::Value* entries = overlays->find("entries"); entries != nullptr) {
            if (const json::Array* list = entries->as_array(); list != nullptr) {
                for (const json::Value& entry : *list) {
                    const json::Object* fields = entry.as_object();
                    const json::Value* directory = entry.find("overlay");
                    const std::string* name =
                        directory != nullptr ? directory->as_string() : nullptr;
                    if (fields == nullptr || name == nullptr || !overlay_dir_valid(*name)) continue;
                    const auto [low, high] = read_range(*fields, "formats");
                    if (low <= kOurFormat && kOurFormat <= high) meta_.overlays.push_back(*name);
                }
            }
        }
    }
    // The last overlay declared sits nearest the front, so it is asked first.
    search_.assign(meta_.overlays.rbegin(), meta_.overlays.rend());
    open_ = true;
    return true;
}

std::optional<std::string> ResourcePack::locate(std::string_view asset) const {
    for (const std::string& overlay : search_) {
        std::string candidate = overlay;
        candidate += '/';
        candidate += asset;
        if (zip_.contains(candidate)) return candidate;
    }
    std::string root(asset);
    if (zip_.contains(root)) return root;
    return std::nullopt;
}

std::optional<std::vector<uint8_t>> ResourcePack::read(std::string_view asset) {
    const auto found = locate(asset);
    if (!found.has_value()) return std::nullopt;
    return zip_.read(*found);
}

bool ResourcePack::has(std::string_view asset) const { return locate(asset).has_value(); }

std::vector<std::string> ResourcePack::list(std::string_view prefix,
                                            std::string_view suffix) const {
    std::set<std::string> out;
    const auto consider = [&](std::string_view name, size_t strip) {
        const std::string_view logical = name.substr(strip);
        if (!logical.starts_with(prefix)) return;
        if (!suffix.empty() && !logical.ends_with(suffix)) return;
        out.emplace(logical);
    };
    for (const std::string& name : zip_.names()) {
        bool in_overlay = false;
        for (const std::string& overlay : search_) {
            if (name.size() > overlay.size() + 1 && name.compare(0, overlay.size(), overlay) == 0 &&
                name[overlay.size()] == '/') {
                consider(name, overlay.size() + 1);
                in_overlay = true;
                break;
            }
        }
        if (!in_overlay) consider(name, 0);
    }
    return {out.begin(), out.end()};
}

bool PackStack::add(const std::filesystem::path& path, const ZipLimits& limits) {
    auto pack = std::make_unique<ResourcePack>();
    if (!pack->open(path, limits)) {
        error_ = std::format("{}: {}", path.filename().string(), pack->error());
        return false;
    }
    packs_.push_back(std::move(pack));
    return true;
}

bool PackStack::add_bytes(std::vector<uint8_t> bytes, const ZipLimits& limits) {
    auto pack = std::make_unique<ResourcePack>();
    if (!pack->open_bytes(std::move(bytes), limits)) {
        error_ = pack->error();
        return false;
    }
    packs_.push_back(std::move(pack));
    return true;
}

std::optional<std::vector<uint8_t>> PackStack::read(std::string_view asset) {
    for (auto it = packs_.rbegin(); it != packs_.rend(); ++it) {
        if (auto bytes = (*it)->read(asset)) return bytes;
    }
    return std::nullopt;
}

bool PackStack::has(std::string_view asset) const {
    return std::any_of(packs_.begin(), packs_.end(),
                       [&](const auto& pack) { return pack->has(asset); });
}

std::vector<std::string> PackStack::list(std::string_view prefix, std::string_view suffix) const {
    std::set<std::string> out;
    for (const auto& pack : packs_) {
        for (std::string& name : pack->list(prefix, suffix)) out.emplace(std::move(name));
    }
    return {out.begin(), out.end()};
}

} // namespace mcpp::packs
