#include "mcpp/auth.hpp"

#include "mcpp/json.hpp"

#include <cstdlib>
#include <fstream>
#include <thread>

#include <curl/curl.h>

namespace mcpp::auth {
namespace {

constexpr const char* kDeviceCodeUrl =
    "https://login.microsoftonline.com/consumers/oauth2/v2.0/devicecode";
constexpr const char* kTokenUrl = "https://login.microsoftonline.com/consumers/oauth2/v2.0/token";
constexpr const char* kXboxLiveUrl = "https://user.auth.xboxlive.com/user/authenticate";
constexpr const char* kXstsUrl = "https://xsts.auth.xboxlive.com/xsts/authorize";
constexpr const char* kMinecraftLoginUrl =
    "https://api.minecraftservices.com/authentication/login_with_xbox";
constexpr const char* kProfileUrl = "https://api.minecraftservices.com/minecraft/profile";
constexpr const char* kScope = "XboxLive.signin offline_access";

constexpr auto kSignInTimeout = std::chrono::minutes(15);

size_t collect(char* data, size_t size, size_t count, void* into) {
    static_cast<std::string*>(into)->append(data, size * count);
    return size * count;
}

struct Response {
    long status = 0;
    std::string body;
    bool transport_failed = false;
};

Response request(const char* url, const std::string& body, bool json_body,
                 const std::string& bearer = {}) {
    Response response;
    CURL* handle = curl_easy_init();
    if (handle == nullptr) {
        response.transport_failed = true;
        return response;
    }

    curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, json_body ? "Content-Type: application/json"
                                                   : "Content-Type: application/x-www-form-urlencoded");
    headers = curl_slist_append(headers, "Accept: application/json");
    if (!bearer.empty()) {
        headers = curl_slist_append(headers, ("Authorization: Bearer " + bearer).c_str());
    }

    curl_easy_setopt(handle, CURLOPT_URL, url);
    curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, collect);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, &response.body);
    curl_easy_setopt(handle, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
    // Certificate checking stays on. A sign-in is exactly where it must.
    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 2L);
    if (body.empty()) {
        curl_easy_setopt(handle, CURLOPT_HTTPGET, 1L);
    } else {
        curl_easy_setopt(handle, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    }

    response.transport_failed = curl_easy_perform(handle) != CURLE_OK;
    curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &response.status);

    curl_slist_free_all(headers);
    curl_easy_cleanup(handle);
    return response;
}

std::string json_string(std::string_view value) {
    std::string out = "\"";
    for (char c : value) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:   out += c;
        }
    }
    return out + "\"";
}

std::string form_escaped(std::string_view value) {
    std::string out;
    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else if (c == ' ') {
            out += '+';
        } else {
            static constexpr char kHex[] = "0123456789ABCDEF";
            out += '%';
            out += kHex[c >> 4];
            out += kHex[c & 0x0F];
        }
    }
    return out;
}

std::string field(const json::Value& root, std::string_view key) {
    const auto* value = root.find(key);
    return value ? value->string_or("") : std::string{};
}

// Dashed form, because that is how the rest of the client writes a uuid.
std::string dashed(const std::string& compact) {
    if (compact.size() != 32) return compact;
    return compact.substr(0, 8) + "-" + compact.substr(8, 4) + "-" + compact.substr(12, 4) + "-" +
           compact.substr(16, 4) + "-" + compact.substr(20);
}

std::filesystem::path state_directory() {
    std::filesystem::path base;
    if (const char* state_home = std::getenv("XDG_STATE_HOME")) {
        base = state_home;
    } else if (const char* home = std::getenv("HOME")) {
        base = std::filesystem::path(home) / ".local" / "state";
    }
    return base / "plasma-client";
}

} // namespace

std::string join_server(const Profile& profile, const std::string& server_hash) {
    if (!profile.valid()) return "not signed in";

    std::string undashed;
    undashed.reserve(32);
    for (const char ch : profile.uuid) {
        if (ch != '-') undashed += ch;
    }

    const std::string body = std::format(
        R"({{"accessToken":"{}","selectedProfile":"{}","serverId":"{}"}})",
        profile.access_token, undashed, server_hash);

    const auto response = request("https://sessionserver.mojang.com/session/minecraft/join", body,
                                  true);
    if (response.transport_failed) return "could not reach the session server";
    if (response.status == 204) return {};
    if (response.status == 403) return "the session server refused this token";
    return std::format("the session server answered {}", response.status);
}

std::string_view describe(Failure failure) {
    switch (failure) {
    case Failure::None:            return "no error";
    case Failure::Network:         return "no connection to Microsoft";
    case Failure::Declined:        return "you declined the sign-in";
    case Failure::TimedOut:        return "the sign-in took too long";
    case Failure::NoXboxProfile:   return "this account has no Xbox profile yet";
    case Failure::CountryUnavailable: return "Xbox Live is not available in this country";
    case Failure::ChildAccount:    return "child account: add it to a family";
    case Failure::NotApprovedByMojang:
        return "this client has not been approved by Mojang yet";
    case Failure::DoesNotOwnGame:  return "this account does not own Minecraft: Java Edition";
    }
    return "unknown error";
}

std::string Session::client_id() {
    if (const char* from_environment = std::getenv("PLASMA_MSA_CLIENT_ID")) {
        return from_environment;
    }
#ifdef PLASMA_MSA_CLIENT_ID
    return PLASMA_MSA_CLIENT_ID;
#else
    return {};
#endif
}


namespace {

struct Stored {
    std::string refresh_token;
    std::string minecraft_token;
    std::string uuid;
    std::string name;
    long long expires_at = 0;   // seconds since the epoch
};

long long now_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

Stored read_stored(const std::filesystem::path& path) {
    Stored stored;
    std::ifstream file(path);
    if (!file) return stored;
    const std::string text((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
    try {
        const auto root = json::parse(text);
        stored.refresh_token = field(root, "refresh_token");
        stored.minecraft_token = field(root, "minecraft_token");
        stored.uuid = field(root, "uuid");
        stored.name = field(root, "name");
        if (const auto* value = root.find("expires_at"); value != nullptr) {
            stored.expires_at = static_cast<long long>(value->number_or(0));
        }
    } catch (const std::exception&) {
        return {};
    }
    return stored;
}

void write_stored(const std::filesystem::path& path, const Stored& stored) {
    std::error_code ignored;
    std::filesystem::create_directories(path.parent_path(), ignored);
    std::ofstream file(path, std::ios::trunc);
    if (!file) return;
    file << "{\"refresh_token\":" << json_string(stored.refresh_token)
         << ",\"minecraft_token\":" << json_string(stored.minecraft_token)
         << ",\"uuid\":" << json_string(stored.uuid)
         << ",\"name\":" << json_string(stored.name)
         << ",\"expires_at\":" << stored.expires_at << "}\n";
    file.close();
    // The file holds a token that is as good as the password; nobody else on
    // the machine gets to read it.
    std::filesystem::permissions(path, std::filesystem::perms::owner_read |
                                           std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::replace, ignored);
}

} // namespace

// Each of these four exchanges hands its answer to the next one. They are
// separate because each fails in its own way, and the player needs to be told
// which one said no.
struct Session::Exchange {
    Session& session;

    bool fail(Failure failure, std::string message) {
        session.failure_ = failure;
        session.message_ = std::move(message);
        return false;
    }

    // Microsoft access token, either fresh from the device flow or refreshed.
    bool microsoft_refresh(const std::string& refresh_token, std::string& access_token,
                           std::string& new_refresh) {
        const std::string form = "client_id=" + form_escaped(Session::client_id()) +
                                 "&grant_type=refresh_token&refresh_token=" +
                                 form_escaped(refresh_token) + "&scope=" + form_escaped(kScope);
        const auto response = request(kTokenUrl, form, false);
        if (response.transport_failed) return fail(Failure::Network, "no answer from Microsoft");
        if (response.status != 200) return fail(Failure::Declined, "the sign-in has expired");
        const auto root = json::parse(response.body);
        access_token = field(root, "access_token");
        new_refresh = field(root, "refresh_token");
        return !access_token.empty();
    }

    // Xbox Live, then XSTS, then Minecraft, then the profile that proves the
    // account owns the game.
    bool to_profile(const std::string& microsoft_token, Profile& profile) {
        const std::string xbox_body =
            R"({"Properties":{"AuthMethod":"RPS","SiteName":"user.auth.xboxlive.com",)"
            R"("RpsTicket":)" + json_string("d=" + microsoft_token) +
            R"(},"RelyingParty":"http://auth.xboxlive.com","TokenType":"JWT"})";
        auto response = request(kXboxLiveUrl, xbox_body, true);
        if (response.transport_failed) return fail(Failure::Network, "no answer from Xbox Live");
        if (response.status != 200) {
            return fail(Failure::NoXboxProfile, "Xbox Live refused the sign-in");
        }
        auto root = json::parse(response.body);
        const std::string xbox_token = field(root, "Token");
        std::string user_hash;
        if (const auto* claims = root.find("DisplayClaims"); claims != nullptr) {
            if (const auto* xui = claims->find("xui"); xui != nullptr) {
                if (const auto* first = xui->at(0); first != nullptr) {
                    user_hash = field(*first, "uhs");
                }
            }
        }
        if (xbox_token.empty() || user_hash.empty()) {
            return fail(Failure::NoXboxProfile, "Xbox Live answered without a token");
        }

        const std::string xsts_body =
            R"({"Properties":{"SandboxId":"RETAIL","UserTokens":[)" + json_string(xbox_token) +
            R"(]},"RelyingParty":"rp://api.minecraftservices.com/","TokenType":"JWT"})";
        response = request(kXstsUrl, xsts_body, true);
        if (response.transport_failed) return fail(Failure::Network, "no answer from Xbox");
        if (response.status == 401) {
            const auto reason = json::parse(response.body);
            const auto code = static_cast<long long>(
                reason.find("XErr") != nullptr ? reason.find("XErr")->number_or(0) : 0);
            switch (code) {
            case 2148916233: return fail(Failure::NoXboxProfile, "this account has no Xbox profile");
            case 2148916235: return fail(Failure::CountryUnavailable, "Xbox Live is not available here");
            case 2148916238: return fail(Failure::ChildAccount, "child account: add it to a family");
            default: return fail(Failure::NoXboxProfile, "Xbox refused this account");
            }
        }
        if (response.status != 200) return fail(Failure::NoXboxProfile, "Xbox refused the token");
        root = json::parse(response.body);
        const std::string xsts_token = field(root, "Token");
        if (xsts_token.empty()) return fail(Failure::NoXboxProfile, "Xbox answered without a token");

        const std::string minecraft_body =
            R"({"identityToken":)" + json_string("XBL3.0 x=" + user_hash + ";" + xsts_token) + "}";
        response = request(kMinecraftLoginUrl, minecraft_body, true);
        if (response.transport_failed) return fail(Failure::Network, "no answer from Mojang");
        if (response.status == 403) {
            return fail(Failure::NotApprovedByMojang, "Mojang has not approved this client");
        }
        if (response.status != 200) return fail(Failure::Network, "Mojang refused the token");
        root = json::parse(response.body);
        profile.access_token = field(root, "access_token");
        const auto lifetime = root.find("expires_in") != nullptr
                                  ? root.find("expires_in")->number_or(86400)
                                  : 86400;
        profile.expires = std::chrono::system_clock::now() +
                          std::chrono::seconds(static_cast<long long>(lifetime));
        if (profile.access_token.empty()) return fail(Failure::Network, "Mojang gave no token");

        response = request(kProfileUrl, {}, true, profile.access_token);
        if (response.transport_failed) return fail(Failure::Network, "no answer from Mojang");
        if (response.status == 404) {
            return fail(Failure::DoesNotOwnGame, "this account does not own Minecraft: Java Edition");
        }
        if (response.status != 200) return fail(Failure::Network, "Mojang refused the profile");
        root = json::parse(response.body);
        profile.uuid = dashed(field(root, "id"));
        profile.name = field(root, "name");
        return profile.valid();
    }
};

std::optional<Profile> Session::restore() {
    failure_ = Failure::None;
    message_.clear();
    if (!configured()) {
        failure_ = Failure::NotApprovedByMojang;
        message_ = "this build has no application id";
        return std::nullopt;
    }

    auto stored = read_stored(token_path());
    if (stored.refresh_token.empty()) return std::nullopt;

    if (!stored.minecraft_token.empty() && stored.expires_at > now_seconds() + 60) {
        Profile profile;
        profile.uuid = stored.uuid;
        profile.name = stored.name;
        profile.access_token = stored.minecraft_token;
        profile.expires = std::chrono::system_clock::from_time_t(
            static_cast<std::time_t>(stored.expires_at));
        if (profile.valid()) return profile;
    }

    Exchange exchange{*this};
    std::string microsoft_token;
    std::string refreshed;
    if (!exchange.microsoft_refresh(stored.refresh_token, microsoft_token, refreshed)) {
        return std::nullopt;
    }
    Profile profile;
    if (!exchange.to_profile(microsoft_token, profile)) return std::nullopt;

    stored.refresh_token = refreshed.empty() ? stored.refresh_token : refreshed;
    stored.minecraft_token = profile.access_token;
    stored.uuid = profile.uuid;
    stored.name = profile.name;
    stored.expires_at = std::chrono::duration_cast<std::chrono::seconds>(
                            profile.expires.time_since_epoch())
                            .count();
    write_stored(token_path(), stored);
    return profile;
}

std::optional<Profile> Session::sign_in(const std::function<void(const DeviceCode&)>& show) {
    failure_ = Failure::None;
    message_.clear();
    if (!configured()) {
        failure_ = Failure::NotApprovedByMojang;
        message_ = "this build has no application id";
        return std::nullopt;
    }

    Exchange exchange{*this};

    const std::string start = "client_id=" + form_escaped(client_id()) + "&scope=" +
                              form_escaped(kScope);
    auto response = request(kDeviceCodeUrl, start, false);
    if (response.transport_failed) {
        exchange.fail(Failure::Network, "no answer from Microsoft");
        return std::nullopt;
    }
    if (response.status != 200) {
        exchange.fail(Failure::Network, std::format("Microsoft answered {}", response.status));
        return std::nullopt;
    }

    auto root = json::parse(response.body);
    DeviceCode code;
    code.user_code = field(root, "user_code");
    code.verification_uri = field(root, "verification_uri");
    const std::string device_code = field(root, "device_code");
    if (const auto* value = root.find("interval"); value != nullptr) {
        code.interval = std::chrono::seconds(static_cast<long long>(value->number_or(5)));
    }
    if (device_code.empty() || code.user_code.empty()) {
        exchange.fail(Failure::Network, "Microsoft gave no device code");
        return std::nullopt;
    }
    if (show) show(code);

    const std::string poll = "client_id=" + form_escaped(client_id()) +
                             "&grant_type=urn:ietf:params:oauth:grant-type:device_code"
                             "&device_code=" + form_escaped(device_code);
    const auto deadline = std::chrono::steady_clock::now() + kSignInTimeout;
    std::string microsoft_token;
    std::string refresh_token;
    while (std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(code.interval);
        response = request(kTokenUrl, poll, false);
        if (response.transport_failed) continue;
        root = json::parse(response.body);
        if (response.status == 200) {
            microsoft_token = field(root, "access_token");
            refresh_token = field(root, "refresh_token");
            break;
        }
        const std::string error = field(root, "error");
        if (error == "authorization_pending") continue;
        if (error == "slow_down") {
            code.interval += std::chrono::seconds(5);
            continue;
        }
        if (error == "authorization_declined" || error == "access_denied") {
            exchange.fail(Failure::Declined, "you declined the sign-in");
            return std::nullopt;
        }
        if (error == "expired_token") break;
        exchange.fail(Failure::Network, error.empty() ? "Microsoft refused the sign-in" : error);
        return std::nullopt;
    }
    if (microsoft_token.empty()) {
        exchange.fail(Failure::TimedOut, "the sign-in took too long");
        return std::nullopt;
    }

    Profile profile;
    if (!exchange.to_profile(microsoft_token, profile)) return std::nullopt;

    Stored stored;
    stored.refresh_token = refresh_token;
    stored.minecraft_token = profile.access_token;
    stored.uuid = profile.uuid;
    stored.name = profile.name;
    stored.expires_at = std::chrono::duration_cast<std::chrono::seconds>(
                            profile.expires.time_since_epoch())
                            .count();
    write_stored(token_path(), stored);
    return profile;
}

std::filesystem::path Session::token_path() { return state_directory() / "account.json"; }

void Session::sign_out() {
    std::error_code ignored;
    std::filesystem::remove(token_path(), ignored);
}

} // namespace mcpp::auth
