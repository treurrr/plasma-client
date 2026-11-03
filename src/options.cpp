#include "mcpp/options.hpp"

#include <charconv>
#include <cmath>
#include <cstdlib>
#include <format>
#include <fstream>
#include <sstream>

namespace mcpp {
namespace {

// What a first run of 1.21.11 writes, in the order it writes it. The order
// and the keys are those of a vanilla options.txt; the values are the
// game's defaults, not one player's choices.
const std::vector<std::pair<std::string, std::string>>& defaults() {
    static const std::vector<std::pair<std::string, std::string>> table = {
        {"version", "4671"},
        {"ao", "true"},
        {"biomeBlendRadius", "2"},
        {"chunkSectionFadeInTime", "0.75"},
        {"cutoutLeaves", "true"},
        {"enableVsync", "true"},
        {"entityDistanceScaling", "1.0"},
        {"entityShadows", "true"},
        {"forceUnicodeFont", "false"},
        {"japaneseGlyphVariants", "false"},
        {"fov", "0.0"},
        {"fovEffectScale", "1.0"},
        {"darknessEffectScale", "1.0"},
        {"glintSpeed", "0.5"},
        {"glintStrength", "0.75"},
        {"graphicsPreset", "\"fancy\""},
        {"prioritizeChunkUpdates", "0"},
        {"fullscreen", "false"},
        {"gamma", "0.5"},
        {"guiScale", "0"},
        {"maxAnisotropyBit", "1"},
        {"textureFiltering", "0"},
        {"maxFps", "120"},
        {"improvedTransparency", "false"},
        {"inactivityFpsLimit", "\"afk\""},
        {"mipmapLevels", "4"},
        {"narrator", "0"},
        {"particles", "0"},
        {"reducedDebugInfo", "false"},
        {"renderClouds", "\"true\""},
        {"cloudRange", "64"},
        {"renderDistance", "12"},
        {"simulationDistance", "12"},
        {"screenEffectScale", "1.0"},
        {"soundDevice", "\"\""},
        {"vignette", "true"},
        {"weatherRadius", "10"},
        {"autoJump", "false"},
        {"rotateWithMinecart", "false"},
        {"operatorItemsTab", "false"},
        {"autoSuggestions", "true"},
        {"chatColors", "true"},
        {"chatLinks", "true"},
        {"chatLinksPrompt", "true"},
        {"discrete_mouse_scroll", "false"},
        {"invertXMouse", "false"},
        {"invertYMouse", "false"},
        {"realmsNotifications", "true"},
        {"showSubtitles", "false"},
        {"directionalAudio", "false"},
        {"touchscreen", "false"},
        {"bobView", "true"},
        {"toggleCrouch", "false"},
        {"toggleSprint", "false"},
        {"toggleAttack", "false"},
        {"toggleUse", "false"},
        {"sprintWindow", "7"},
        {"darkMojangStudiosBackground", "false"},
        {"hideLightningFlashes", "false"},
        {"hideSplashTexts", "false"},
        {"mouseSensitivity", "0.5"},
        {"damageTiltStrength", "1.0"},
        {"highContrast", "false"},
        {"highContrastBlockOutline", "false"},
        {"narratorHotkey", "true"},
        {"resourcePacks", "[]"},
        {"incompatibleResourcePacks", "[]"},
        {"lastServer", ""},
        {"lang", "en_us"},
        {"chatVisibility", "0"},
        {"chatOpacity", "1.0"},
        {"chatLineSpacing", "0.0"},
        {"textBackgroundOpacity", "0.5"},
        {"backgroundForChatOnly", "true"},
        {"hideServerAddress", "false"},
        {"advancedItemTooltips", "false"},
        {"pauseOnLostFocus", "true"},
        {"overrideWidth", "0"},
        {"overrideHeight", "0"},
        {"chatHeightFocused", "1.0"},
        {"chatDelay", "0.0"},
        {"chatHeightUnfocused", "0.4375"},
        {"chatScale", "1.0"},
        {"chatWidth", "1.0"},
        {"notificationDisplayTime", "1.0"},
        {"useNativeTransport", "true"},
        {"mainHand", "\"right\""},
        {"attackIndicator", "1"},
        {"tutorialStep", "movement"},
        {"mouseWheelSensitivity", "1.0"},
        {"rawMouseInput", "true"},
        {"allowCursorChanges", "true"},
        {"glDebugVerbosity", "1"},
        {"skipMultiplayerWarning", "false"},
        {"hideMatchedNames", "true"},
        {"joinedFirstServer", "false"},
        {"syncChunkWrites", "false"},
        {"showAutosaveIndicator", "true"},
        {"allowServerListing", "true"},
        {"onlyShowSecureChat", "false"},
        {"saveChatDrafts", "false"},
        {"panoramaScrollSpeed", "1.0"},
        {"telemetryOptInExtra", "false"},
        {"onboardAccessibility", "true"},
        {"menuBackgroundBlurriness", "5"},
        {"startedCleanly", "true"},
        {"musicToast", "\"never\""},
        {"musicFrequency", "\"DEFAULT\""},
        {"key_key.attack", "key.mouse.left"},
        {"key_key.use", "key.mouse.right"},
        {"key_key.forward", "key.keyboard.w"},
        {"key_key.left", "key.keyboard.a"},
        {"key_key.back", "key.keyboard.s"},
        {"key_key.right", "key.keyboard.d"},
        {"key_key.jump", "key.keyboard.space"},
        {"key_key.sneak", "key.keyboard.left.shift"},
        {"key_key.sprint", "key.keyboard.left.control"},
        {"key_key.drop", "key.keyboard.q"},
        {"key_key.inventory", "key.keyboard.e"},
        {"key_key.chat", "key.keyboard.t"},
        {"key_key.playerlist", "key.keyboard.tab"},
        {"key_key.pickItem", "key.mouse.middle"},
        {"key_key.command", "key.keyboard.slash"},
        {"key_key.socialInteractions", "key.keyboard.p"},
        {"key_key.toggleGui", "key.keyboard.f1"},
        {"key_key.toggleSpectatorShaderEffects", "key.keyboard.f4"},
        {"key_key.screenshot", "key.keyboard.f2"},
        {"key_key.togglePerspective", "key.keyboard.f5"},
        {"key_key.smoothCamera", "key.keyboard.unknown"},
        {"key_key.fullscreen", "key.keyboard.f11"},
        {"key_key.spectatorOutlines", "key.keyboard.unknown"},
        {"key_key.spectatorHotbar", "key.mouse.middle"},
        {"key_key.swapOffhand", "key.keyboard.f"},
        {"key_key.saveToolbarActivator", "key.keyboard.c"},
        {"key_key.loadToolbarActivator", "key.keyboard.x"},
        {"key_key.advancements", "key.keyboard.l"},
        {"key_key.quickActions", "key.keyboard.g"},
        {"key_key.debug.overlay", "key.keyboard.f3"},
        {"key_key.debug.modifier", "key.keyboard.f3"},
        {"key_key.hotbar.1", "key.keyboard.1"},
        {"key_key.hotbar.2", "key.keyboard.2"},
        {"key_key.hotbar.3", "key.keyboard.3"},
        {"key_key.hotbar.4", "key.keyboard.4"},
        {"key_key.hotbar.5", "key.keyboard.5"},
        {"key_key.hotbar.6", "key.keyboard.6"},
        {"key_key.hotbar.7", "key.keyboard.7"},
        {"key_key.hotbar.8", "key.keyboard.8"},
        {"key_key.hotbar.9", "key.keyboard.9"},
        {"key_key.debug.reloadChunk", "key.keyboard.a"},
        {"key_key.debug.showHitboxes", "key.keyboard.b"},
        {"key_key.debug.clearChat", "key.keyboard.d"},
        {"key_key.debug.crash", "key.keyboard.c"},
        {"key_key.debug.showChunkBorders", "key.keyboard.g"},
        {"key_key.debug.showAdvancedTooltips", "key.keyboard.h"},
        {"key_key.debug.copyRecreateCommand", "key.keyboard.i"},
        {"key_key.debug.spectate", "key.keyboard.n"},
        {"key_key.debug.switchGameMode", "key.keyboard.f4"},
        {"key_key.debug.debugOptions", "key.keyboard.f6"},
        {"key_key.debug.focusPause", "key.keyboard.p"},
        {"key_key.debug.dumpDynamicTextures", "key.keyboard.s"},
        {"key_key.debug.reloadResourcePacks", "key.keyboard.t"},
        {"key_key.debug.profiling", "key.keyboard.l"},
        {"key_key.debug.copyLocation", "key.keyboard.c"},
        {"key_key.debug.dumpVersion", "key.keyboard.v"},
        {"key_key.debug.profilingChart", "key.keyboard.1"},
        {"key_key.debug.fpsCharts", "key.keyboard.2"},
        {"key_key.debug.networkCharts", "key.keyboard.3"},
        {"soundCategory_master", "1.0"},
        {"soundCategory_music", "1.0"},
        {"soundCategory_record", "1.0"},
        {"soundCategory_weather", "1.0"},
        {"soundCategory_block", "1.0"},
        {"soundCategory_hostile", "1.0"},
        {"soundCategory_neutral", "1.0"},
        {"soundCategory_player", "1.0"},
        {"soundCategory_ambient", "1.0"},
        {"soundCategory_voice", "1.0"},
        {"soundCategory_ui", "1.0"},
        {"modelPart_cape", "true"},
        {"modelPart_jacket", "true"},
        {"modelPart_left_sleeve", "true"},
        {"modelPart_right_sleeve", "true"},
        {"modelPart_left_pants_leg", "true"},
        {"modelPart_right_pants_leg", "true"},
        {"modelPart_hat", "true"},
    };
    return table;
}

std::filesystem::path data_home() {
    if (const char* data = std::getenv("XDG_DATA_HOME")) return data;
    if (const char* home = std::getenv("HOME")) {
        return std::filesystem::path(home) / ".local" / "share";
    }
    return {};
}

} // namespace

std::string format_double(double value) {
    if (std::isfinite(value) && value == std::floor(value) && std::abs(value) < 1e7) {
        return std::format("{:.1f}", value);
    }
    return std::format("{}", value);
}

Options::Options() : entries_(defaults()) {}

const std::string* Options::default_of(std::string_view key) {
    for (const auto& [name, value] : defaults()) {
        if (name == key) return &value;
    }
    return nullptr;
}

void Options::parse(std::string_view text) {
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        const size_t colon = line.find(':');
        if (colon != std::string_view::npos && colon > 0) {
            set_raw(line.substr(0, colon), std::string(line.substr(colon + 1)));
        }
        if (end == text.size()) break;
        start = end + 1;
    }
}

std::string Options::serialise() const {
    std::string out;
    for (const auto& [key, value] : entries_) {
        out += key;
        out += ':';
        out += value;
        out += '\n';
    }
    return out;
}

bool Options::load(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::ostringstream text;
    text << file.rdbuf();
    parse(text.str());
    return true;
}

bool Options::save(const std::filesystem::path& path) const {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    const auto temporary = path.string() + ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) return false;
        file << serialise();
        if (!file) return false;
    }
    std::filesystem::rename(temporary, path, error);
    return !error;
}

std::filesystem::path Options::default_path() {
    return data_home() / "plasma-client" / "options.txt";
}

std::optional<std::filesystem::path> Options::vanilla_path() {
    std::vector<std::filesystem::path> roots;
    if (const char* chosen = std::getenv("PLASMA_MINECRAFT")) roots.emplace_back(chosen);
    if (const char* home = std::getenv("HOME")) {
        roots.push_back(std::filesystem::path(home) / ".minecraft");
        roots.push_back(std::filesystem::path(home) / ".var/app/com.mojang.Minecraft/.minecraft");
    }
    for (const auto& root : roots) {
        std::error_code error;
        if (std::filesystem::is_regular_file(root / "options.txt", error)) {
            return root / "options.txt";
        }
    }
    return std::nullopt;
}

const std::string* Options::raw(std::string_view key) const {
    for (const auto& [name, value] : entries_) {
        if (name == key) return &value;
    }
    return nullptr;
}

void Options::set_raw(std::string_view key, std::string value) {
    for (auto& [name, current] : entries_) {
        if (name == key) {
            current = std::move(value);
            return;
        }
    }
    entries_.emplace_back(std::string(key), std::move(value));
}

bool Options::get_bool(std::string_view key) const {
    const auto* value = raw(key);
    return value != nullptr && *value == "true";
}

int Options::get_int(std::string_view key) const {
    const auto* value = raw(key);
    if (value == nullptr) return 0;
    int out = 0;
    std::from_chars(value->data(), value->data() + value->size(), out);
    return out;
}

double Options::get_double(std::string_view key) const {
    const auto* value = raw(key);
    if (value == nullptr) return 0.0;
    double out = 0.0;
    std::from_chars(value->data(), value->data() + value->size(), out);
    return out;
}

std::string Options::get_string(std::string_view key) const {
    const auto* value = raw(key);
    if (value == nullptr) return {};
    if (value->size() >= 2 && value->front() == '"' && value->back() == '"') {
        std::string out;
        for (size_t i = 1; i + 1 < value->size(); ++i) {
            if ((*value)[i] == '\\' && i + 2 < value->size()) ++i;
            out += (*value)[i];
        }
        return out;
    }
    return *value;
}

void Options::set_bool(std::string_view key, bool value) { set_raw(key, value ? "true" : "false"); }

void Options::set_int(std::string_view key, int value) { set_raw(key, std::to_string(value)); }

void Options::set_double(std::string_view key, double value) {
    set_raw(key, format_double(value));
}

void Options::set_string(std::string_view key, std::string_view value) {
    std::string quoted = "\"";
    for (const char c : value) {
        if (c == '"' || c == '\\') quoted += '\\';
        quoted += c;
    }
    quoted += '"';
    set_raw(key, std::move(quoted));
}

} // namespace mcpp
