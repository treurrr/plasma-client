#pragma once
#include <filesystem>
#include <map>
#include <string>
#include <string_view>

namespace mcpp {

// Minecraft's own language file. It is a game asset, so it is never shipped with
// the client; tools/fetch_lang.sh pulls it from an install the player already has.
class Translations {
public:
    bool load(const std::filesystem::path& path);
    const std::string* find(std::string_view key) const;

    size_t size() const { return entries_.size(); }
    bool empty() const { return entries_.empty(); }

    static std::filesystem::path default_path();

private:
    std::map<std::string, std::string, std::less<>> entries_;
};

} // namespace mcpp
