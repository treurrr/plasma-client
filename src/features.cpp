#include "mcpp/features.hpp"

#include <array>
#include <cstdlib>
#include <fstream>
#include <string>

namespace mcpp::features {

namespace {

constexpr std::array<Feature, static_cast<size_t>(Id::Count)> kFeatures{{
    {"fps",          "FPS counter",           Category::Hud,    true,  false},
    {"cps",          "CPS counter",           Category::Hud,    true,  false},
    {"ping",         "Ping",                  Category::Hud,    true,  false},
    {"keystrokes",   "Keystrokes",            Category::Hud,    true,  false},
    {"armorhud",     "Armor HUD",             Category::Hud,    true,  false},
    {"potionhud",    "Potion HUD",            Category::Hud,    true,  false},
    {"coords",       "Coordinates",           Category::Hud,    false, false},
    {"togglesprint", "Toggle sprint",         Category::Input,  false, true},
    {"togglesneak",  "Toggle sneak",          Category::Input,  false, true},
    {"zoom",         "Zoom",                  Category::Input,  true,  false},
    {"hitboxes",     "Hitboxes",              Category::Visual, false, true},
    {"fullbright",   "Fullbright",            Category::Visual, true,  false},
    {"nohurtcam",    "No hurt cam",           Category::Visual, true,  false},
    {"nofireoverlay","No fire overlay",       Category::Visual, true,  false},
    {"cleanscore",   "Clean scoreboard",      Category::Visual, false, false},
    {"plasmahotbar", "Plasma hotbar",         Category::Visual, false, false},
}};

} // namespace

Registry::Registry() {
    for (size_t i = 0; i < kFeatures.size(); ++i) {
        enabled_[i] = kFeatures[i].enabled_by_default;
    }
}

Registry& Registry::instance() {
    static Registry registry;
    return registry;
}

std::span<const Feature> Registry::all() const { return {kFeatures.data(), kFeatures.size()}; }

void Registry::lock(Id id, std::string reason) {
    const auto at = static_cast<size_t>(id);
    if (at >= std::size(locked_)) return;
    locked_[at] = true;
    lock_reason_[at] = std::move(reason);
}

void Registry::unlock_all() {
    for (size_t at = 0; at < std::size(locked_); ++at) {
        locked_[at] = false;
        lock_reason_[at].clear();
    }
}

bool Registry::locked(Id id) const {
    const auto at = static_cast<size_t>(id);
    return at < std::size(locked_) && locked_[at];
}

std::string_view Registry::lock_reason(Id id) const {
    const auto at = static_cast<size_t>(id);
    return at < std::size(lock_reason_) ? lock_reason_[at] : std::string_view{};
}

const Feature* Registry::find(std::string_view key) {
    for (const auto& feature : kFeatures) {
        if (feature.key == key) return &feature;
    }
    return nullptr;
}

bool Registry::wanted(Id id) const {
    const auto at = static_cast<size_t>(id);
    return at < std::size(enabled_) && enabled_[at];
}

bool Registry::enabled(Id id) const {
    const auto index = static_cast<size_t>(id);
    return index < kFeatures.size() && enabled_[index] && !locked_[index];
}

void Registry::set(Id id, bool on) {
    const auto index = static_cast<size_t>(id);
    if (index < kFeatures.size()) enabled_[index] = on;
}

bool Registry::toggle(Id id) {
    const auto index = static_cast<size_t>(id);
    if (index >= kFeatures.size()) return false;
    enabled_[index] = !enabled_[index];
    return enabled_[index];
}

std::filesystem::path Registry::default_config_path() {
    const char* home = std::getenv("HOME");
    std::filesystem::path base = home != nullptr ? home : ".";
    return base / ".config" / "plasma-client" / "features.txt";
}

void Registry::load(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) return;

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line.front() == '#') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string_view key(line.data(), eq);
        const std::string_view value(line.data() + eq + 1, line.size() - eq - 1);
        if (key == "profile") {
            set_profile(value == "vanilla" ? Profile::Vanilla : Profile::Plasma, true);
            continue;
        }
        for (size_t i = 0; i < kFeatures.size(); ++i) {
            if (kFeatures[i].key == key) {
                enabled_[i] = (value == "1" || value == "true" || value == "on");
                break;
            }
        }
    }
}

void Registry::save(const std::filesystem::path& path) const {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream file(path, std::ios::trunc);
    if (!file) return;

    file << "# Plasma Client\n";
    file << "profile=" << (profile_ == Profile::Vanilla ? "vanilla" : "plasma") << '\n';
    for (size_t i = 0; i < kFeatures.size(); ++i) {
        file << kFeatures[i].key << '=' << (enabled_[i] ? 1 : 0) << '\n';
    }
}

std::string_view category_name(Category category) {
    switch (category) {
        case Category::Hud: return "HUD";
        case Category::Input: return "Input";
        case Category::Visual: return "Visual";
    }
    return "?";
}

} // namespace mcpp::features
