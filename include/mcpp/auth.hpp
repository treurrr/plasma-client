#pragma once
#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mcpp::auth {

// A signed-in player. Having one of these is the proof that the account owns
// the game: api.minecraftservices.com only answers with a profile for an
// account that does.
struct Profile {
    std::string uuid;            // dashed
    std::string name;
    std::string access_token;    // for the server join request
    std::chrono::system_clock::time_point expires;

    bool valid() const { return !uuid.empty() && !access_token.empty(); }
};

// Tells Mojang's session server that this account is about to join a server
// with this handshake hash. The server then asks Mojang the same question.
// Returns empty on success, or a reason.
std::string join_server(const Profile& profile, const std::string& server_hash);

// Shown while the device-code flow waits for the player.
struct DeviceCode {
    std::string user_code;
    std::string verification_uri;
    std::chrono::seconds interval{5};
};

enum class Failure {
    None,
    SessionExpired,    // the cached session ran out and cannot be refreshed here
    Network,
    Declined,          // the player refused on the Microsoft page
    TimedOut,
    NoXboxProfile,     // XErr 2148916233
    CountryUnavailable,// XErr 2148916235
    ChildAccount,      // XErr 2148916238
    NotApprovedByMojang,  // 403 until the app registration is approved
    DoesNotOwnGame,    // 404 from the profile endpoint
};

std::string_view describe(Failure failure);

// Asks Mojang whose token this is. An empty return means the account owns the
// game and `profile` now carries the uuid and name Mojang itself reports; any
// other return is the reason it does not.
std::string check_ownership(Profile& profile);

// Taking over a session from a launcher the player is already signed in to on
// this machine. The store is read, nothing is sent anywhere, and the result
// goes into the same cache the Microsoft flow writes.
struct LauncherStore {
    enum class Kind {
        Vanilla,     // .minecraft/launcher_accounts.json
        PrismLike,   // Prism Launcher / MultiMC accounts.json
    };
    Kind kind = Kind::Vanilla;
    std::string launcher;
    std::filesystem::path path;
};

struct Handover {
    enum class Result {
        Found,
        NoStore,      // no launcher account store on this machine
        NoAccount,    // a store, but no account that can be singled out
        NoToken,      // an account, but the launcher kept no session token
        Expired,      // a token that is past its expiry
        Unreadable,   // a store that is not shaped like one
    };
    Result result = Result::NoStore;
    std::string launcher;
    std::filesystem::path path;
    Profile profile;                 // only when Found
    std::vector<std::string> names;  // profile names seen, for a NoAccount hint
};

std::string_view describe(Handover::Result result);

std::vector<LauncherStore> launcher_stores();

// Split out from the file for testing: the same parse, over text.
Handover parse_launcher_store(LauncherStore::Kind kind, std::string_view text,
                              std::string_view wanted_name,
                              std::chrono::system_clock::time_point now);

Handover read_launcher_store(const LauncherStore& store, std::string_view wanted_name,
                             std::chrono::system_clock::time_point now);

// Walks the stores in order and returns the first one that has a session, or
// the most telling failure if none of them does.
Handover take_from_launcher(std::string_view wanted_name = {},
                            std::chrono::system_clock::time_point now =
                                std::chrono::system_clock::now());

class Session {
public:
    // The Azure application id. Empty means this build cannot sign in at all,
    // which is deliberate: a build without one must not pretend to.
    static std::string client_id();
    static bool configured() { return !client_id().empty(); }

    // Returns a cached profile if one is still valid, refreshing it silently
    // when only the access token has expired.
    std::optional<Profile> restore();

    // Walks the device-code flow. `show` is called once with the code and the
    // page to open; it then polls until the player finishes or gives up.
    std::optional<Profile> sign_in(const std::function<void(const DeviceCode&)>& show);

    // Puts a session that came from somewhere else (a launcher) into the cache
    // `play` and `join` read. There is no refresh token with it, so it lasts
    // only as long as the launcher's own token does.
    void adopt(const Profile& profile, std::string_view source);

    void sign_out();

    Failure last_failure() const { return failure_; }
    std::string_view last_message() const { return message_; }

    static std::filesystem::path token_path();

private:
    struct Exchange;

    Failure failure_ = Failure::None;
    std::string message_;
};

} // namespace mcpp::auth
