#include "mcpp/auth.hpp"

#include <chrono>
#include <iostream>
#include <string_view>

namespace {

using mcpp::auth::Handover;
using mcpp::auth::LauncherStore;
using Result = Handover::Result;

int failures = 0;

void check(bool condition, std::string_view what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << '\n';
        ++failures;
    }
}

// 2026-01-01T00:00:00Z, the "now" every case is judged against.
constexpr std::chrono::system_clock::time_point now{std::chrono::seconds(1767225600)};

// Made up values throughout; a real store is never test data.
constexpr const char* kVanilla = R"({
  "accounts": {
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa": {
      "accessToken": "token-for-oldest",
      "accessTokenExpiresAt": "2025-06-01T10:00:00Z",
      "localId": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
      "minecraftProfile": {"id": "11111111111111111111111111111111", "name": "StaleOne"},
      "type": "Xbox",
      "username": "someone@example.invalid"
    },
    "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb": {
      "accessToken": "token-for-active",
      "accessTokenExpiresAt": "2026-01-02T03:04:05Z",
      "localId": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
      "minecraftProfile": {"id": "22222222222222222222222222222222", "name": "ChosenOne"},
      "type": "Xbox",
      "username": "other@example.invalid"
    },
    "cccccccccccccccccccccccccccccccc": {
      "accessToken": "",
      "accessTokenExpiresAt": "2026-03-04T05:06:07Z",
      "localId": "cccccccccccccccccccccccccccccccc",
      "minecraftProfile": {"id": "33333333333333333333333333333333", "name": "NoTokenOne"},
      "type": "Xbox"
    }
  },
  "activeAccountLocalId": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
  "mojangClientToken": ""
})";

constexpr const char* kVanillaOneStale = R"({
  "accounts": {
    "dddddddddddddddddddddddddddddddd": {
      "accessToken": "token-long-gone",
      "accessTokenExpiresAt": "2025-12-31T23:59:59Z",
      "localId": "dddddddddddddddddddddddddddddddd",
      "minecraftProfile": {"id": "44444444444444444444444444444444", "name": "LoneStale"},
      "type": "Xbox"
    }
  },
  "activeAccountLocalId": "dddddddddddddddddddddddddddddddd"
})";

constexpr const char* kPrism = R"({
  "accounts": [
    {
      "type": "Offline",
      "profile": {"id": "55555555555555555555555555555555", "name": "OfflinePlayer"},
      "ygg": {"token": "0", "exp": 4102444800}
    },
    {
      "active": true,
      "type": "MSA",
      "entitlement": {"canPlayMinecraft": true, "ownsMinecraft": true},
      "msa-client-id": "00000000-0000-0000-0000-000000000000",
      "msa": {"token": "msa-token", "iat": 1767225000, "exp": 1767228600},
      "profile": {"id": "66666666666666666666666666666666", "name": "PrismOne"},
      "ygg": {"token": "ygg-token", "iat": 1767225000, "exp": 1767311400,
              "extra": {"userName": "PrismOne", "clientToken": ""}}
    },
    {
      "type": "MSA",
      "profile": {"id": "77777777777777777777777777777777", "name": "PrismStale"},
      "ygg": {"token": "ygg-old", "iat": 1767139200, "exp": 1767139500}
    }
  ],
  "formatVersion": 3
})";

constexpr const char* kMultiMc = R"({
  "accounts": [
    {
      "accessToken": "mmc-token",
      "activeProfile": "88888888888888888888888888888888",
      "profiles": [
        {"id": "99999999999999999999999999999999", "name": "NotThisOne"},
        {"id": "88888888888888888888888888888888", "name": "MmcOne"}
      ],
      "username": "mmc@example.invalid"
    }
  ],
  "formatVersion": 2
})";

Handover vanilla(std::string_view text, std::string_view wanted = {}) {
    return mcpp::auth::parse_launcher_store(LauncherStore::Kind::Vanilla, text, wanted, now);
}

Handover prism(std::string_view text, std::string_view wanted = {}) {
    return mcpp::auth::parse_launcher_store(LauncherStore::Kind::PrismLike, text, wanted, now);
}

void vanilla_picks_the_active_account() {
    const auto handover = vanilla(kVanilla);
    check(handover.result == Result::Found, "vanilla: active account is found");
    check(handover.profile.name == "ChosenOne", "vanilla: the active profile name");
    check(handover.profile.uuid == "22222222-2222-2222-2222-222222222222",
          "vanilla: the uuid comes back dashed");
    check(handover.profile.access_token == "token-for-active", "vanilla: the token of that account");
    check(handover.profile.expires == std::chrono::system_clock::time_point(
                                          std::chrono::seconds(1767323045)),
          "vanilla: accessTokenExpiresAt is read as an instant");
}

void vanilla_picks_by_name() {
    const auto stale = vanilla(kVanilla, "staleone");
    check(stale.result == Result::Expired, "vanilla: a named stale account reports expired");
    check(stale.profile.access_token.empty(), "vanilla: an expired account hands over no token");

    const auto empty = vanilla(kVanilla, "NoTokenOne");
    check(empty.result == Result::NoToken, "vanilla: an account with no token says so");

    const auto missing = vanilla(kVanilla, "Nobody");
    check(missing.result == Result::NoAccount, "vanilla: an unknown name is no account");
    check(missing.names.size() == 3, "vanilla: the names are offered as a hint");
}

void vanilla_without_an_active_account_needs_a_name() {
    constexpr std::string_view two = R"({"accounts": {
      "a": {"accessToken": "t1", "accessTokenExpiresAt": "2026-02-01T00:00:00Z",
            "minecraftProfile": {"id": "11111111111111111111111111111111", "name": "One"}},
      "b": {"accessToken": "t2", "accessTokenExpiresAt": "2026-02-01T00:00:00Z",
            "minecraftProfile": {"id": "22222222222222222222222222222222", "name": "Two"}}}})";
    const auto handover = vanilla(two);
    check(handover.result == Result::NoAccount, "vanilla: two accounts and no marker is ambiguous");
    check(handover.names.size() == 2, "vanilla: both names are offered");

    const auto lone = vanilla(kVanillaOneStale);
    check(lone.result == Result::Expired, "vanilla: the only account, expired, reports expired");
    check(lone.names.size() == 1 && lone.names.front() == "LoneStale",
          "vanilla: the expired account is named");
}

void prism_picks_the_marked_account() {
    const auto handover = prism(kPrism);
    check(handover.result == Result::Found, "prism: the account marked active is found");
    check(handover.profile.name == "PrismOne", "prism: the profile name");
    check(handover.profile.uuid == "66666666-6666-6666-6666-666666666666", "prism: dashed uuid");
    check(handover.profile.access_token == "ygg-token", "prism: the yggdrasil token, not the msa one");
    check(handover.profile.expires ==
              std::chrono::system_clock::time_point(std::chrono::seconds(1767311400)),
          "prism: exp is seconds since the epoch");

    const auto stale = prism(kPrism, "PrismStale");
    check(stale.result == Result::Expired, "prism: a stale entry reports expired");

    const auto offline = prism(kPrism, "OfflinePlayer");
    check(offline.result == Result::NoAccount, "prism: an offline account is not a session");
}

void prism_falls_back_to_the_issue_time() {
    constexpr std::string_view no_exp = R"({"accounts": [{"active": true, "type": "MSA",
      "profile": {"id": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaab", "name": "NoExp"},
      "ygg": {"token": "t", "iat": 1767225000}}], "formatVersion": 3})";
    const auto handover = prism(no_exp);
    check(handover.result == Result::Found, "prism: a token with only an issue time is usable");
    check(handover.profile.expires ==
              std::chrono::system_clock::time_point(std::chrono::seconds(1767225000 + 86400)),
          "prism: a missing exp falls back to a day after iat");

    constexpr std::string_view old_iat = R"({"accounts": [{"active": true, "type": "MSA",
      "profile": {"id": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaac", "name": "OldIat"},
      "ygg": {"token": "t", "iat": 1700000000}}], "formatVersion": 3})";
    check(prism(old_iat).result == Result::Expired,
          "prism: a day after an old issue time is expired");
}

void multimc_shape_is_read() {
    const auto handover = prism(kMultiMc);
    check(handover.result == Result::Found, "multimc: the flat shape is read");
    check(handover.profile.name == "MmcOne", "multimc: activeProfile decides the profile");
    check(handover.profile.uuid == "88888888-8888-8888-8888-888888888888", "multimc: dashed uuid");
    check(handover.profile.access_token == "mmc-token", "multimc: the token");
}

void rubbish_is_rubbish() {
    check(vanilla("not json at all").result == Result::Unreadable, "unreadable: not json");
    check(vanilla("[]").result == Result::Unreadable, "unreadable: not an object");
    check(vanilla(R"({"somethingElse": 1})").result == Result::Unreadable,
          "unreadable: no accounts at all");
    check(vanilla(R"({"accounts": {}})").result == Result::NoAccount,
          "an empty account list is no account, not rubbish");
}

void instants_in_other_shapes() {
    constexpr std::string_view shapes = R"({"accounts": {
      "a": {"accessToken": "t", "accessTokenExpiresAt": "2026-01-02T03:04:05.678Z",
            "minecraftProfile": {"id": "11111111111111111111111111111111", "name": "Frac"}}}})";
    const auto fraction = vanilla(shapes);
    check(fraction.result == Result::Found, "instant: a fraction is tolerated");
    check(fraction.profile.expires ==
              std::chrono::system_clock::time_point(std::chrono::seconds(1767323045)),
          "instant: the fraction is dropped, not misread");

    constexpr std::string_view offset = R"({"accounts": {
      "a": {"accessToken": "t", "accessTokenExpiresAt": "2026-01-02T05:04:05+02:00",
            "minecraftProfile": {"id": "11111111111111111111111111111111", "name": "Offset"}}}})";
    check(vanilla(offset).profile.expires ==
              std::chrono::system_clock::time_point(std::chrono::seconds(1767323045)),
          "instant: an offset is taken off");

    constexpr std::string_view nonsense = R"({"accounts": {
      "a": {"accessToken": "t", "accessTokenExpiresAt": "whenever",
            "minecraftProfile": {"id": "11111111111111111111111111111111", "name": "NoDate"}}}})";
    const auto unknown = vanilla(nonsense);
    check(unknown.result == Result::Found, "instant: an unreadable date does not lose the session");
    check(unknown.profile.expires > now && unknown.profile.expires <= now + std::chrono::hours(1),
          "instant: an unknown expiry is treated as an hour at most");
}

} // namespace

int main() {
    vanilla_picks_the_active_account();
    vanilla_picks_by_name();
    vanilla_without_an_active_account_needs_a_name();
    prism_picks_the_marked_account();
    prism_falls_back_to_the_issue_time();
    multimc_shape_is_read();
    rubbish_is_rubbish();
    instants_in_other_shapes();

    if (failures != 0) {
        std::cerr << failures << " checks failed\n";
        return 1;
    }
    std::cout << "launcher handover: all checks passed\n";
    return 0;
}
