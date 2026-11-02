#pragma once
#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

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

    void sign_out();

    Failure last_failure() const { return failure_; }
    std::string_view last_message() const { return message_; }

    static std::filesystem::path token_path();

private:
    Failure failure_ = Failure::None;
    std::string message_;
};

} // namespace mcpp::auth
