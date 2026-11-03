#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Sounds, languages and the rest of what is not in the client jar, read from
// the player's own installation the way the launcher keeps them: an index per
// asset version that names every file by the sha1 of its contents, and the
// files themselves under objects/<first two digits>/<sha1>.
namespace mcpp::assets {

class ObjectStore {
public:
    struct Object {
        std::string hash;
        uint64_t size = 0;
    };

    // Each launcher's game directory, the one holding assets/ and versions/,
    // in the order they are searched.
    static std::vector<std::filesystem::path> default_roots();
    // The asset index a version uses, from the launcher's own description of
    // that version: "29" for 1.21.11. Empty when no launcher has the version.
    static std::string index_for_version(const std::filesystem::path& root,
                                         std::string_view version);
    static std::string index_for_version(std::string_view version);
    // The index of that version, from the first launcher that has both.
    static std::optional<ObjectStore> for_version(std::string_view version);

    // assets is the directory with indexes/ and objects/ in it.
    bool open(const std::filesystem::path& assets, std::string_view index);

    size_t size() const { return objects_.size(); }
    const std::filesystem::path& directory() const { return assets_; }
    const Object* find(std::string_view name) const;
    // Where "minecraft/sounds/random/click.ogg" is on disk, or empty when the
    // index does not name it or the launcher never downloaded it.
    std::filesystem::path path(std::string_view name) const;
    // The file's bytes, or nothing when it is missing or not the size the
    // index says, which is what a download cut short looks like.
    std::optional<std::vector<uint8_t>> read(std::string_view name) const;
    // Every name that starts with prefix, sorted.
    std::vector<std::string> names(std::string_view prefix) const;

private:
    std::filesystem::path assets_;
    std::map<std::string, Object, std::less<>> objects_;
};

} // namespace mcpp::assets
