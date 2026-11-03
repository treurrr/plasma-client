#include "mcpp/assets/object_store.hpp"

#include "mcpp/json.hpp"

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <system_error>

namespace mcpp::assets {
namespace {

std::optional<std::string> read_text(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool is_sha1(std::string_view hash) {
    if (hash.size() != 40) return false;
    for (const char c : hash) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

std::string asset_index_of(const std::filesystem::path& version_json) {
    const auto text = read_text(version_json);
    if (!text) return {};
    try {
        const auto root = json::parse(*text);
        if (const auto* index = root.find("assetIndex")) {
            if (const auto* id = index->find("id")) return id->string_or("");
        }
    } catch (const std::exception&) {
    }
    return {};
}

} // namespace

std::vector<std::filesystem::path> ObjectStore::default_roots() {
    std::vector<std::filesystem::path> roots;
    if (const char* chosen = std::getenv("PLASMA_MINECRAFT")) roots.emplace_back(chosen);
    if (const char* home_text = std::getenv("HOME")) {
        const std::filesystem::path home(home_text);
        roots.push_back(home / ".minecraft");
        roots.push_back(home / ".var/app/com.mojang.Minecraft/.minecraft");
        roots.push_back(home / ".var/app/com.modrinth.ModrinthApp/data/ModrinthApp/meta");
        roots.push_back(home / ".local/share/ModrinthApp/meta");
        roots.push_back(home / ".local/share/PrismLauncher");
        roots.push_back(home / ".var/app/org.prismlauncher.PrismLauncher/data/PrismLauncher");
        roots.push_back(home / ".local/share/multimc");
    }
    if (const char* appdata = std::getenv("APPDATA")) {
        roots.push_back(std::filesystem::path(appdata) / ".minecraft");
    }
    return roots;
}

// The vanilla launcher keeps versions/<v>/<v>.json, Modrinth names the
// directory after the loader too ("1.21.11-0.20.1"), and Prism keeps its
// own copy of Mojang's description under meta/net.minecraft.
std::string ObjectStore::index_for_version(const std::filesystem::path& root,
                                           std::string_view version) {
    const std::string name(version);
    if (auto id = asset_index_of(root / "versions" / name / (name + ".json")); !id.empty()) {
        return id;
    }
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(root / "versions", error)) {
        const std::string directory = entry.path().filename().string();
        if (!directory.starts_with(name + "-")) continue;
        if (auto id = asset_index_of(entry.path() / (directory + ".json")); !id.empty()) return id;
    }
    return asset_index_of(root / "meta" / "net.minecraft" / (name + ".json"));
}

std::string ObjectStore::index_for_version(std::string_view version) {
    for (const auto& root : default_roots()) {
        if (auto id = index_for_version(root, version); !id.empty()) return id;
    }
    return {};
}

std::optional<ObjectStore> ObjectStore::for_version(std::string_view version) {
    for (const auto& root : default_roots()) {
        const auto index = index_for_version(root, version);
        if (index.empty()) continue;
        ObjectStore store;
        if (store.open(root / "assets", index)) return store;
    }
    return std::nullopt;
}

bool ObjectStore::open(const std::filesystem::path& assets, std::string_view index) {
    objects_.clear();
    assets_ = assets;
    const auto text = read_text(assets / "indexes" / (std::string(index) + ".json"));
    if (!text) return false;
    try {
        const auto root = json::parse(*text);
        const auto* objects = root.find("objects");
        if (objects == nullptr || objects->as_object() == nullptr) return false;
        for (const auto& [name, entry] : *objects->as_object()) {
            const auto* hash = entry.find("hash");
            const auto* size = entry.find("size");
            if (hash == nullptr || size == nullptr || !is_sha1(hash->string_or(""))) continue;
            objects_.emplace(name, Object{hash->string_or(""),
                                          static_cast<uint64_t>(size->number_or(0))});
        }
    } catch (const std::exception&) {
        objects_.clear();
        return false;
    }
    return true;
}

const ObjectStore::Object* ObjectStore::find(std::string_view name) const {
    const auto found = objects_.find(name);
    return found == objects_.end() ? nullptr : &found->second;
}

std::filesystem::path ObjectStore::path(std::string_view name) const {
    const auto* object = find(name);
    if (object == nullptr) return {};
    auto file = assets_ / "objects" / object->hash.substr(0, 2) / object->hash;
    std::error_code error;
    if (!std::filesystem::is_regular_file(file, error)) return {};
    return file;
}

std::optional<std::vector<uint8_t>> ObjectStore::read(std::string_view name) const {
    const auto file = path(name);
    if (file.empty()) return std::nullopt;
    std::ifstream in(file, std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
    if (bytes.size() != find(name)->size) return std::nullopt;
    return bytes;
}

std::vector<std::string> ObjectStore::names(std::string_view prefix) const {
    std::vector<std::string> out;
    for (auto it = objects_.lower_bound(prefix); it != objects_.end(); ++it) {
        if (!it->first.starts_with(prefix)) break;
        out.push_back(it->first);
    }
    return out;
}

} // namespace mcpp::assets
