#pragma once
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace mcpp::features {

// Everything here must be defensible on a server with anticheat. Anything that
// gives information or input the vanilla client cannot have is out of scope:
// no reach, aim assist, auto-clicker, velocity changes, ESP or hitbox edits.
// Grouped by category: the UI walks this list in order.
enum class Id : uint8_t {
    FpsCounter,
    CpsCounter,
    PingDisplay,
    Keystrokes,
    ArmorHud,
    PotionHud,
    Coordinates,

    ToggleSprint,
    ToggleSneak,
    Zoom,

    Hitboxes,
    Fullbright,
    NoHurtCam,
    NoFireOverlay,
    ScoreboardCleanup,
    Count,
};

enum class Category : uint8_t { Hud, Input, Visual };

struct Feature {
    std::string_view key;   // name used in the config file
    std::string_view label;
    Category category;
    bool enabled_by_default;
    // Some servers forbid toggle sprint or hitbox outlines even though vanilla
    // can render them. Those are opt-in and flagged in the UI.
    bool server_may_object;
};

// Feature keys as the server sees them, and the plugin channel they travel on.
// A server that wants to allow this client has to be able to switch features
// off, so the channel is part of the product rather than an extra.
constexpr std::string_view kPluginChannel = "crit:features";

class Registry {
public:
    static Registry& instance();

    std::span<const Feature> all() const;

    // False when the player turned it off, and false when the server forbade
    // it. Callers never have to remember to ask twice: if a server says no,
    // the feature is off everywhere, not only greyed out in the menu.
    bool enabled(Id id) const;
    void set(Id id, bool on);
    bool toggle(Id id);

    // What the player chose, ignoring any server lock. Only the menu needs it,
    // to show the switch in the position it will return to.
    bool wanted(Id id) const;

    // Locks last for one session; leaving a server clears them.
    void lock(Id id, std::string reason);
    void unlock_all();
    bool locked(Id id) const;
    std::string_view lock_reason(Id id) const;

    static const Feature* find(std::string_view key);

    void load(const std::filesystem::path& path);
    void save(const std::filesystem::path& path) const;
    static std::filesystem::path default_config_path();

private:
    Registry();

    bool enabled_[static_cast<size_t>(Id::Count)] = {};
    bool locked_[static_cast<size_t>(Id::Count)] = {};
    std::string lock_reason_[static_cast<size_t>(Id::Count)];
};

std::string_view category_name(Category category);

} // namespace mcpp::features
