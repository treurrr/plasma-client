#include "mcpp/translations.hpp"

#include "mcpp/json.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace mcpp {

std::filesystem::path Translations::default_path() {
    if (const char* override_path = std::getenv("PLASMA_LANG")) return override_path;

    std::filesystem::path base;
    if (const char* data_home = std::getenv("XDG_DATA_HOME")) {
        base = data_home;
    } else if (const char* home = std::getenv("HOME")) {
        base = std::filesystem::path(home) / ".local" / "share";
    }
    return base / "plasma-client" / "lang" / "en_us.json";
}

bool Translations::load(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) return false;

    std::ostringstream text;
    text << file.rdbuf();

    const auto root = json::parse(text.str());
    const auto* object = root.as_object();
    if (!object) return false;

    for (const auto& [key, value] : *object) {
        if (const auto* string = value.as_string()) entries_.emplace(key, *string);
    }
    return !entries_.empty();
}

const std::string* Translations::find(std::string_view key) const {
    auto it = entries_.find(key);
    return it == entries_.end() ? nullptr : &it->second;
}

} // namespace mcpp
