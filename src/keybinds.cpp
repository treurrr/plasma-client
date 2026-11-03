#include "mcpp/keybinds.hpp"

#include <algorithm>
#include <charconv>

namespace mcpp {
namespace {

struct Named {
    std::string_view name;
    int code;
};

// The game's names for keys, which are GLFW's codes.
constexpr Named kKeys[] = {
    {"unknown", -1},       {"space", 32},         {"apostrophe", 39},    {"comma", 44},
    {"minus", 45},         {"period", 46},        {"slash", 47},         {"semicolon", 59},
    {"equal", 61},         {"left.bracket", 91},  {"backslash", 92},     {"right.bracket", 93},
    {"grave.accent", 96},  {"world.1", 161},      {"world.2", 162},      {"escape", 256},
    {"enter", 257},        {"tab", 258},          {"backspace", 259},    {"insert", 260},
    {"delete", 261},       {"right", 262},        {"left", 263},         {"down", 264},
    {"up", 265},           {"page.up", 266},      {"page.down", 267},    {"home", 268},
    {"end", 269},          {"caps.lock", 280},    {"scroll.lock", 281},  {"num.lock", 282},
    {"print.screen", 283}, {"pause", 284},        {"keypad.0", 320},     {"keypad.1", 321},
    {"keypad.2", 322},     {"keypad.3", 323},     {"keypad.4", 324},     {"keypad.5", 325},
    {"keypad.6", 326},     {"keypad.7", 327},     {"keypad.8", 328},     {"keypad.9", 329},
    {"keypad.decimal", 330}, {"keypad.divide", 331}, {"keypad.multiply", 332},
    {"keypad.subtract", 333}, {"keypad.add", 334}, {"keypad.enter", 335}, {"keypad.equal", 336},
    {"left.shift", 340},   {"left.control", 341}, {"left.alt", 342},     {"left.win", 343},
    {"right.shift", 344},  {"right.control", 345}, {"right.alt", 346},   {"right.win", 347},
    {"menu", 348},
};

constexpr Named kMouse[] = {{"left", 0}, {"right", 1}, {"middle", 2}};

struct Default {
    std::string_view name;
    std::string_view key;
    std::string_view category;
};

constexpr std::string_view kMovement = "key.category.minecraft.movement";
constexpr std::string_view kMisc = "key.category.minecraft.misc";
constexpr std::string_view kMultiplayer = "key.category.minecraft.multiplayer";
constexpr std::string_view kGameplay = "key.category.minecraft.gameplay";
constexpr std::string_view kInventory = "key.category.minecraft.inventory";
constexpr std::string_view kCreative = "key.category.minecraft.creative";
constexpr std::string_view kSpectator = "key.category.minecraft.spectator";
constexpr std::string_view kDebug = "key.category.minecraft.debug";

// In the order the game writes them to options.txt.
constexpr Default kDefaults[] = {
    {"key.attack", "key.mouse.left", kGameplay},
    {"key.use", "key.mouse.right", kGameplay},
    {"key.forward", "key.keyboard.w", kMovement},
    {"key.left", "key.keyboard.a", kMovement},
    {"key.back", "key.keyboard.s", kMovement},
    {"key.right", "key.keyboard.d", kMovement},
    {"key.jump", "key.keyboard.space", kMovement},
    {"key.sneak", "key.keyboard.left.shift", kMovement},
    {"key.sprint", "key.keyboard.left.control", kMovement},
    {"key.drop", "key.keyboard.q", kInventory},
    {"key.inventory", "key.keyboard.e", kInventory},
    {"key.chat", "key.keyboard.t", kMultiplayer},
    {"key.playerlist", "key.keyboard.tab", kMultiplayer},
    {"key.pickItem", "key.mouse.middle", kGameplay},
    {"key.command", "key.keyboard.slash", kMultiplayer},
    {"key.socialInteractions", "key.keyboard.p", kMultiplayer},
    {"key.toggleGui", "key.keyboard.f1", kMisc},
    {"key.toggleSpectatorShaderEffects", "key.keyboard.f4", kMisc},
    {"key.screenshot", "key.keyboard.f2", kMisc},
    {"key.togglePerspective", "key.keyboard.f5", kMisc},
    {"key.smoothCamera", "key.keyboard.unknown", kMisc},
    {"key.fullscreen", "key.keyboard.f11", kMisc},
    {"key.spectatorOutlines", "key.keyboard.unknown", kSpectator},
    {"key.spectatorHotbar", "key.mouse.middle", kSpectator},
    {"key.swapOffhand", "key.keyboard.f", kInventory},
    {"key.saveToolbarActivator", "key.keyboard.c", kCreative},
    {"key.loadToolbarActivator", "key.keyboard.x", kCreative},
    {"key.advancements", "key.keyboard.l", kMisc},
    {"key.quickActions", "key.keyboard.g", kMultiplayer},
    {"key.debug.overlay", "key.keyboard.f3", kDebug},
    {"key.debug.modifier", "key.keyboard.f3", kDebug},
    {"key.hotbar.1", "key.keyboard.1", kInventory},
    {"key.hotbar.2", "key.keyboard.2", kInventory},
    {"key.hotbar.3", "key.keyboard.3", kInventory},
    {"key.hotbar.4", "key.keyboard.4", kInventory},
    {"key.hotbar.5", "key.keyboard.5", kInventory},
    {"key.hotbar.6", "key.keyboard.6", kInventory},
    {"key.hotbar.7", "key.keyboard.7", kInventory},
    {"key.hotbar.8", "key.keyboard.8", kInventory},
    {"key.hotbar.9", "key.keyboard.9", kInventory},
    {"key.debug.reloadChunk", "key.keyboard.a", kDebug},
    {"key.debug.showHitboxes", "key.keyboard.b", kDebug},
    {"key.debug.clearChat", "key.keyboard.d", kDebug},
    {"key.debug.crash", "key.keyboard.c", kDebug},
    {"key.debug.showChunkBorders", "key.keyboard.g", kDebug},
    {"key.debug.showAdvancedTooltips", "key.keyboard.h", kDebug},
    {"key.debug.copyRecreateCommand", "key.keyboard.i", kDebug},
    {"key.debug.spectate", "key.keyboard.n", kDebug},
    {"key.debug.switchGameMode", "key.keyboard.f4", kDebug},
    {"key.debug.debugOptions", "key.keyboard.f6", kDebug},
    {"key.debug.focusPause", "key.keyboard.p", kDebug},
    {"key.debug.dumpDynamicTextures", "key.keyboard.s", kDebug},
    {"key.debug.reloadResourcePacks", "key.keyboard.t", kDebug},
    {"key.debug.profiling", "key.keyboard.l", kDebug},
    {"key.debug.copyLocation", "key.keyboard.c", kDebug},
    {"key.debug.dumpVersion", "key.keyboard.v", kDebug},
    {"key.debug.profilingChart", "key.keyboard.1", kDebug},
    {"key.debug.fpsCharts", "key.keyboard.2", kDebug},
    {"key.debug.networkCharts", "key.keyboard.3", kDebug},
};

} // namespace

std::string InputKey::name() const {
    if (type == Type::Mouse) {
        for (const auto& named : kMouse) {
            if (named.code == code) return "key.mouse." + std::string(named.name);
        }
        return "key.mouse." + std::to_string(code + 1);
    }
    if ((code >= 'A' && code <= 'Z') || (code >= '0' && code <= '9')) {
        return std::string("key.keyboard.") +
               static_cast<char>(code >= 'A' ? code - 'A' + 'a' : code);
    }
    if (code >= 290 && code <= 314) return "key.keyboard.f" + std::to_string(code - 289);
    for (const auto& named : kKeys) {
        if (named.code == code) return "key.keyboard." + std::string(named.name);
    }
    return "key.keyboard.unknown";
}

std::optional<InputKey> InputKey::from_name(std::string_view text) {
    constexpr std::string_view kKeyboard = "key.keyboard.";
    constexpr std::string_view kMousePrefix = "key.mouse.";
    if (text.starts_with(kMousePrefix)) {
        const auto rest = text.substr(kMousePrefix.size());
        for (const auto& named : kMouse) {
            if (named.name == rest) return InputKey::mouse(named.code);
        }
        int number = 0;
        const auto [end, error] = std::from_chars(rest.data(), rest.data() + rest.size(), number);
        if (error == std::errc{} && end == rest.data() + rest.size() && number >= 1) {
            return InputKey::mouse(number - 1);
        }
        return std::nullopt;
    }
    if (!text.starts_with(kKeyboard)) return std::nullopt;
    const auto rest = text.substr(kKeyboard.size());
    if (rest.size() == 1) {
        const char c = rest[0];
        if (c >= 'a' && c <= 'z') return InputKey::keyboard(c - 'a' + 'A');
        if (c >= '0' && c <= '9') return InputKey::keyboard(c);
    }
    if (rest.size() >= 2 && rest[0] == 'f') {
        int number = 0;
        const auto [end, error] =
            std::from_chars(rest.data() + 1, rest.data() + rest.size(), number);
        if (error == std::errc{} && end == rest.data() + rest.size() && number >= 1 &&
            number <= 25) {
            return InputKey::keyboard(289 + number);
        }
    }
    for (const auto& named : kKeys) {
        if (named.name == rest) return InputKey::keyboard(named.code);
    }
    return std::nullopt;
}

Keybinds::Keybinds() {
    for (const auto& entry : kDefaults) {
        Keybind bind;
        bind.name = entry.name;
        bind.category = entry.category;
        bind.fallback = InputKey::from_name(entry.key).value_or(InputKey{});
        bind.current = bind.fallback;
        binds_.push_back(std::move(bind));
    }
}

const std::vector<std::string>& Keybinds::categories() {
    static const std::vector<std::string> order = {
        std::string(kMovement),  std::string(kMisc),      std::string(kMultiplayer),
        std::string(kGameplay),  std::string(kInventory), std::string(kCreative),
        std::string(kSpectator), std::string(kDebug)};
    return order;
}

Keybind* Keybinds::find(std::string_view name) {
    for (auto& bind : binds_) {
        if (bind.name == name) return &bind;
    }
    return nullptr;
}

const Keybind* Keybinds::find(std::string_view name) const {
    for (const auto& bind : binds_) {
        if (bind.name == name) return &bind;
    }
    return nullptr;
}

void Keybinds::load(const Options& options) {
    for (auto& bind : binds_) {
        if (const auto* value = options.raw("key_" + bind.name)) {
            if (auto key = InputKey::from_name(*value)) bind.current = *key;
        }
    }
}

void Keybinds::store(Options& options) const {
    for (const auto& bind : binds_) options.set_raw("key_" + bind.name, bind.current.name());
}

void Keybinds::press(InputKey key) {
    if (key.unbound()) return;
    for (auto& bind : binds_) {
        if (bind.current == key) {
            bind.down = true;
            ++bind.clicks;
        }
    }
}

void Keybinds::release(InputKey key) {
    for (auto& bind : binds_) {
        if (bind.current == key) bind.down = false;
    }
}

void Keybinds::release_all() {
    for (auto& bind : binds_) {
        bind.down = false;
        bind.clicks = 0;
    }
}

bool Keybinds::down(std::string_view name) const {
    const auto* bind = find(name);
    return bind != nullptr && bind->down;
}

bool Keybinds::consume(std::string_view name) {
    auto* bind = find(name);
    if (bind == nullptr || bind->clicks == 0) return false;
    --bind->clicks;
    return true;
}

int Keybinds::take_clicks(std::string_view name) {
    auto* bind = find(name);
    if (bind == nullptr) return 0;
    const int clicks = bind->clicks;
    bind->clicks = 0;
    return clicks;
}

std::vector<const Keybind*> Keybinds::clashes(const Keybind& bind) const {
    std::vector<const Keybind*> out;
    if (bind.current.unbound()) return out;
    // A debug binding is a chord with F3 held, so it shares its key with the
    // rest without taking it from them.
    const bool debug = bind.category == kDebug;
    for (const auto& other : binds_) {
        if (&other == &bind || other.current != bind.current) continue;
        if ((other.category == kDebug) != debug) continue;
        if (debug && (other.name == "key.debug.overlay" || other.name == "key.debug.modifier" ||
                      bind.name == "key.debug.overlay" || bind.name == "key.debug.modifier")) {
            continue;
        }
        out.push_back(&other);
    }
    return out;
}

} // namespace mcpp
