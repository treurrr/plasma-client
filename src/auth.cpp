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

std::string quoted(std::string_view value) {
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
    return base / "crit-client";
}

} // namespace

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
    if (const char* from_environment = std::getenv("CRIT_MSA_CLIENT_ID")) {
        return from_environment;
    }
#ifdef CRIT_MSA_CLIENT_ID
    return CRIT_MSA_CLIENT_ID;
#else
    return {};
#endif
}

std::filesystem::path Session::token_path() { return state_directory() / "account.json"; }

void Session::sign_out() {
    std::error_code ignored;
    std::filesystem::remove(token_path(), ignored);
}

} // namespace mcpp::auth
