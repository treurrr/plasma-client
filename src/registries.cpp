#include "mcpp/registries.hpp"

#include <cstdlib>
#include <format>
#include <fstream>

namespace mcpp {

bool Registries::load_builtin(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) return false;
    std::map<std::string, std::vector<RegistryEntry>, std::less<>> read;
    std::string line;
    while (std::getline(file, line)) {
        const auto tab = line.find('\t');
        if (tab == std::string::npos) continue;
        read[line.substr(0, tab)].push_back({line.substr(tab + 1), std::nullopt});
    }
    for (auto& [name, entries] : read) {
        if (!registries_.contains(name)) registries_.emplace(name, std::move(entries));
    }
    return !read.empty();
}

std::filesystem::path Registries::builtin_path(int32_t protocol) {
    std::filesystem::path base;
    if (const char* data_home = std::getenv("XDG_DATA_HOME")) {
        base = data_home;
    } else if (const char* home = std::getenv("HOME")) {
        base = std::filesystem::path(home) / ".local" / "share";
    }
    return base / "plasma-client" / "data" / std::format("registries-{}.txt", protocol);
}

} // namespace mcpp
