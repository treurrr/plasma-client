#include "mcpp/auth.hpp"

#include "auth_detail.hpp"
#include "mcpp/json.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <string>
#include <utility>

namespace mcpp::auth {
namespace {

using detail::dashed;
using detail::field;

// A token with no recorded expiry is given this much rope: the server decides
// in the end, and a real build asks Mojang before it writes anything.
constexpr auto kUnknownLifetime = std::chrono::hours(1);

std::filesystem::path home() {
    const char* value = std::getenv("HOME");
    return value != nullptr ? std::filesystem::path(value) : std::filesystem::path{};
}

std::filesystem::path from_environment(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr && *value != '\0' ? std::filesystem::path(value)
                                              : std::filesystem::path{};
}

bool same_name(std::string_view left, std::string_view right) {
    return std::ranges::equal(left, right, [](unsigned char a, unsigned char b) {
        return std::tolower(a) == std::tolower(b);
    });
}

int digits(std::string_view text, size_t at, size_t count) {
    int value = 0;
    for (size_t i = at; i < at + count; ++i) {
        if (i >= text.size() || std::isdigit(static_cast<unsigned char>(text[i])) == 0) return -1;
        value = value * 10 + (text[i] - '0');
    }
    return value;
}

// Days since 1970-01-01 for a proleptic Gregorian date, so that an instant can
// be read without a libc call that a strict standard build hides.
long long days_from_civil(int year, int month, int day) {
    year -= month <= 2 ? 1 : 0;
    const long long era = (year >= 0 ? year : year - 399) / 400;
    const auto year_of_era = static_cast<unsigned>(year - era * 400);
    const unsigned day_of_year =
        (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + static_cast<unsigned>(day) - 1;
    const unsigned day_of_era =
        year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    return era * 146097 + static_cast<long long>(day_of_era) - 719468;
}

// The launcher writes an ISO 8601 instant, as "2026-11-06T18:19:25Z" or with a
// fraction and an offset. Only this shape is accepted; anything else counts as
// no expiry at all rather than as a date silently read wrong.
std::optional<std::chrono::system_clock::time_point> parse_instant(std::string_view text) {
    if (text.size() < 19 || text[4] != '-' || text[7] != '-' ||
        (text[10] != 'T' && text[10] != ' ') || text[13] != ':' || text[16] != ':') {
        return std::nullopt;
    }
    const int year = digits(text, 0, 4);
    const int month = digits(text, 5, 2);
    const int day = digits(text, 8, 2);
    const int hour = digits(text, 11, 2);
    const int minute = digits(text, 14, 2);
    const int second = digits(text, 17, 2);
    if (year < 0 || month < 1 || month > 12 || day < 1 || day > 31 || hour < 0 || minute < 0 ||
        second < 0) {
        return std::nullopt;
    }

    size_t at = 19;
    if (at < text.size() && text[at] == '.') {
        ++at;
        while (at < text.size() && std::isdigit(static_cast<unsigned char>(text[at])) != 0) ++at;
    }
    long long offset = 0;
    if (at < text.size() && (text[at] == '+' || text[at] == '-')) {
        const int offset_hour = digits(text, at + 1, 2);
        if (offset_hour < 0) return std::nullopt;
        const size_t minutes_at = text.size() > at + 3 && text[at + 3] == ':' ? at + 4 : at + 3;
        const int offset_minute = std::max(digits(text, minutes_at, 2), 0);
        offset = (offset_hour * 3600LL + offset_minute * 60LL) * (text[at] == '+' ? -1 : 1);
    }

    const long long seconds = days_from_civil(year, month, day) * 86400LL + hour * 3600LL +
                              minute * 60LL + second + offset;
    return std::chrono::system_clock::time_point(std::chrono::seconds(seconds));
}

std::optional<std::chrono::system_clock::time_point> epoch_field(const json::Value& root,
                                                                 std::string_view key) {
    const auto* value = root.find(key);
    if (value == nullptr || !value->as_number()) return std::nullopt;
    auto count = static_cast<long long>(value->number_or(0));
    if (count <= 0) return std::nullopt;
    // Prism writes seconds; a value this large can only be milliseconds.
    if (count > 100000000000LL) count /= 1000;
    return std::chrono::system_clock::from_time_t(static_cast<std::time_t>(count));
}

struct Candidate {
    std::string name;
    std::string uuid;
    std::string token;
    std::optional<std::chrono::system_clock::time_point> expires;
    bool active = false;
};

int rank(Handover::Result result) {
    switch (result) {
    case Handover::Result::Found:      return 6;
    case Handover::Result::Expired:    return 5;
    case Handover::Result::NoToken:    return 4;
    case Handover::Result::NoAccount:  return 3;
    case Handover::Result::Unreadable: return 2;
    case Handover::Result::NoStore:    return 1;
    }
    return 0;
}

Handover choose(std::vector<Candidate> candidates, std::string_view wanted_name,
                std::chrono::system_clock::time_point now) {
    Handover handover;
    for (const auto& candidate : candidates) {
        if (!candidate.name.empty()) handover.names.push_back(candidate.name);
    }
    if (candidates.empty()) {
        handover.result = Handover::Result::NoAccount;
        return handover;
    }

    const Candidate* chosen = nullptr;
    if (!wanted_name.empty()) {
        for (const auto& candidate : candidates) {
            if (same_name(candidate.name, wanted_name)) chosen = &candidate;
        }
        if (chosen == nullptr) {
            handover.result = Handover::Result::NoAccount;
            return handover;
        }
    }
    if (chosen == nullptr) {
        for (const auto& candidate : candidates) {
            if (candidate.active) chosen = &candidate;
        }
    }
    if (chosen == nullptr && candidates.size() == 1) chosen = &candidates.front();
    if (chosen == nullptr) {
        // Several accounts and none of them marked: the player says which.
        handover.result = Handover::Result::NoAccount;
        return handover;
    }

    if (chosen->token.empty()) {
        handover.result = Handover::Result::NoToken;
        handover.names = {chosen->name};
        return handover;
    }
    const auto expires = chosen->expires.value_or(now + kUnknownLifetime);
    if (expires <= now) {
        handover.result = Handover::Result::Expired;
        handover.names = {chosen->name};
        return handover;
    }

    handover.result = Handover::Result::Found;
    handover.names = {chosen->name};
    handover.profile.uuid = chosen->uuid;
    handover.profile.name = chosen->name;
    handover.profile.access_token = chosen->token;
    handover.profile.expires = expires;
    return handover;
}

std::vector<Candidate> vanilla_candidates(const json::Value& root) {
    std::vector<Candidate> candidates;
    const auto* accounts = root.find("accounts");
    const auto* object = accounts != nullptr ? accounts->as_object() : nullptr;
    if (object == nullptr) return candidates;
    const std::string active = field(root, "activeAccountLocalId");

    for (const auto& [local_id, account] : *object) {
        const auto* profile = account.find("minecraftProfile");
        if (profile == nullptr) continue;
        Candidate candidate;
        candidate.name = field(*profile, "name");
        candidate.uuid = dashed(field(*profile, "id"));
        candidate.token = field(account, "accessToken");
        candidate.expires = parse_instant(field(account, "accessTokenExpiresAt"));
        candidate.active = !active.empty() && (local_id == active ||
                                               field(account, "localId") == active);
        if (candidate.uuid.empty() || candidate.name.empty()) continue;
        candidates.push_back(std::move(candidate));
    }
    return candidates;
}

// Prism Launcher and MultiMC keep an array. Prism holds the Mojang session in
// `ygg`; MultiMC's older file holds it flat with a list of profiles.
std::vector<Candidate> prism_candidates(const json::Value& root) {
    std::vector<Candidate> candidates;
    const auto* accounts = root.find("accounts");
    const auto* array = accounts != nullptr ? accounts->as_array() : nullptr;
    if (array == nullptr) return candidates;

    for (const auto& account : *array) {
        const std::string type = field(account, "type");
        if (type == "Offline") continue;
        Candidate candidate;
        const auto* active = account.find("active");
        candidate.active = active != nullptr && active->as_bool().value_or(false);

        if (const auto* profile = account.find("profile"); profile != nullptr) {
            candidate.name = field(*profile, "name");
            candidate.uuid = dashed(field(*profile, "id"));
        }
        if (const auto* token = account.find("ygg"); token != nullptr) {
            candidate.token = field(*token, "token");
            candidate.expires = epoch_field(*token, "exp");
            if (!candidate.expires) {
                if (const auto issued = epoch_field(*token, "iat")) {
                    candidate.expires = *issued + std::chrono::hours(24);
                }
            }
            if (candidate.name.empty()) {
                if (const auto* extra = token->find("extra"); extra != nullptr) {
                    candidate.name = field(*extra, "userName");
                }
            }
        }
        if (candidate.token.empty()) {
            candidate.token = field(account, "accessToken");
            const std::string selected = field(account, "activeProfile");
            if (const auto* profiles = account.find("profiles");
                profiles != nullptr && profiles->as_array() != nullptr) {
                for (const auto& profile : *profiles->as_array()) {
                    const std::string id = field(profile, "id");
                    if (selected.empty() || id == selected || candidate.uuid.empty()) {
                        candidate.uuid = dashed(id);
                        candidate.name = field(profile, "name");
                    }
                    if (!selected.empty() && id == selected) break;
                }
            }
        }
        if (candidate.uuid.empty() || candidate.name.empty()) continue;
        candidates.push_back(std::move(candidate));
    }
    return candidates;
}

void add_store(std::vector<LauncherStore>& stores, LauncherStore::Kind kind,
               std::string launcher, const std::filesystem::path& path) {
    if (path.empty()) return;
    std::error_code ignored;
    if (!std::filesystem::is_regular_file(path, ignored)) return;
    for (const auto& store : stores) {
        if (store.path == path) return;
    }
    stores.push_back({kind, std::move(launcher), path});
}

} // namespace

std::string_view describe(Handover::Result result) {
    switch (result) {
    case Handover::Result::Found:   return "a session was handed over";
    case Handover::Result::NoStore: return "no launcher account file on this machine";
    case Handover::Result::NoAccount:
        return "no account in the launcher that could be singled out";
    case Handover::Result::NoToken:
        return "the launcher keeps no session token for this account right now";
    case Handover::Result::Expired: return "the launcher's session has expired";
    case Handover::Result::Unreadable:
        return "the launcher's account file is not shaped as expected";
    }
    return "unknown";
}

std::vector<LauncherStore> launcher_stores() {
    std::vector<LauncherStore> stores;
    const auto base = home();

    // An explicit path wins, for a launcher kept somewhere of its own.
    if (const auto given = from_environment("PLASMA_LAUNCHER_ACCOUNTS"); !given.empty()) {
        const bool prism = given.filename() == "accounts.json";
        add_store(stores, prism ? LauncherStore::Kind::PrismLike : LauncherStore::Kind::Vanilla,
                  prism ? "a Prism-style launcher" : "the Minecraft launcher", given);
    }
    for (const char* name : {"MINECRAFT_DIR", "MINECRAFT_HOME", "INST_MC_DIR"}) {
        if (const auto directory = from_environment(name); !directory.empty()) {
            add_store(stores, LauncherStore::Kind::Vanilla, "the Minecraft launcher",
                      directory / "launcher_accounts.json");
        }
    }
    if (!base.empty()) {
        add_store(stores, LauncherStore::Kind::Vanilla, "the Minecraft launcher",
                  base / ".minecraft" / "launcher_accounts.json");
        add_store(stores, LauncherStore::Kind::Vanilla, "the Minecraft launcher (flatpak)",
                  base / ".var/app/com.mojang.Minecraft/.minecraft/launcher_accounts.json");
        add_store(stores, LauncherStore::Kind::Vanilla, "the Minecraft launcher (snap)",
                  base / "snap/mc-installer/common/.minecraft/launcher_accounts.json");
        add_store(stores, LauncherStore::Kind::PrismLike, "Prism Launcher",
                  base / ".local/share/PrismLauncher/accounts.json");
        add_store(stores, LauncherStore::Kind::PrismLike, "Prism Launcher (flatpak)",
                  base / ".var/app/org.prismlauncher.PrismLauncher/data/PrismLauncher/accounts.json");
        add_store(stores, LauncherStore::Kind::PrismLike, "MultiMC",
                  base / ".local/share/multimc/accounts.json");
        add_store(stores, LauncherStore::Kind::PrismLike, "MultiMC",
                  base / ".local/share/MultiMC/accounts.json");
    }
    if (const auto roaming = from_environment("APPDATA"); !roaming.empty()) {
        add_store(stores, LauncherStore::Kind::Vanilla, "the Minecraft launcher",
                  roaming / ".minecraft" / "launcher_accounts.json");
        add_store(stores, LauncherStore::Kind::PrismLike, "Prism Launcher",
                  roaming / "PrismLauncher" / "accounts.json");
    }
    return stores;
}

Handover parse_launcher_store(LauncherStore::Kind kind, std::string_view text,
                              std::string_view wanted_name,
                              std::chrono::system_clock::time_point now) {
    json::Value root;
    try {
        root = json::parse(text);
    } catch (const std::exception&) {
        Handover handover;
        handover.result = Handover::Result::Unreadable;
        return handover;
    }
    if (root.as_object() == nullptr) {
        Handover handover;
        handover.result = Handover::Result::Unreadable;
        return handover;
    }
    auto candidates = kind == LauncherStore::Kind::Vanilla ? vanilla_candidates(root)
                                                           : prism_candidates(root);
    if (candidates.empty() && root.find("accounts") == nullptr) {
        Handover handover;
        handover.result = Handover::Result::Unreadable;
        return handover;
    }
    return choose(std::move(candidates), wanted_name, now);
}

Handover read_launcher_store(const LauncherStore& store, std::string_view wanted_name,
                             std::chrono::system_clock::time_point now) {
    Handover handover;
    handover.launcher = store.launcher;
    handover.path = store.path;

    std::ifstream file(store.path);
    if (!file) {
        handover.result = Handover::Result::NoStore;
        return handover;
    }
    const std::string text((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
    auto parsed = parse_launcher_store(store.kind, text, wanted_name, now);
    parsed.launcher = store.launcher;
    parsed.path = store.path;
    return parsed;
}

Handover take_from_launcher(std::string_view wanted_name,
                            std::chrono::system_clock::time_point now) {
    Handover best;
    for (const auto& store : launcher_stores()) {
        auto handover = read_launcher_store(store, wanted_name, now);
        if (handover.result == Handover::Result::Found) return handover;
        if (rank(handover.result) > rank(best.result)) best = std::move(handover);
    }
    return best;
}

} // namespace mcpp::auth
