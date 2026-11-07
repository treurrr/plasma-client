#include "mcpp/packs/pack_cache.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <curl/curl.h>
#include <format>
#include <fstream>
#include <iterator>
#include <openssl/sha.h>

namespace mcpp::packs {
namespace {

constexpr char kHex[] = "0123456789abcdef";

double ms_since(std::chrono::steady_clock::time_point start) {
    const auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(now - start).count();
}

struct Sink {
    std::vector<uint8_t>* bytes = nullptr;
    size_t max = 0;
    bool overflowed = false;
};

size_t collect(char* data, size_t size, size_t count, void* user) {
    auto* sink = static_cast<Sink*>(user);
    const size_t total = size * count;
    if (sink->bytes->size() + total > sink->max) {
        sink->overflowed = true;
        return 0;
    }
    sink->bytes->insert(sink->bytes->end(), data, data + total);
    return total;
}

} // namespace

std::string sha1_hex(std::span<const uint8_t> bytes) {
    unsigned char digest[SHA_DIGEST_LENGTH]{};
    SHA1(bytes.data(), bytes.size(), digest);
    std::string out;
    out.reserve(SHA_DIGEST_LENGTH * 2);
    for (const unsigned char byte : digest) {
        out.push_back(kHex[byte >> 4]);
        out.push_back(kHex[byte & 0xF]);
    }
    return out;
}

PackCache::PackCache(std::filesystem::path directory, DownloadLimits limits)
    : directory_(std::move(directory)), limits_(limits) {
    std::error_code code;
    std::filesystem::create_directories(directory_, code);
}

// `HttpUtil.cachedFilePath`.
std::filesystem::path PackCache::cached_path(const std::string& hash) const {
    return directory_ / hash;
}

PackCache::Result PackCache::store(const PackId& id, std::span<const uint8_t> bytes) {
    Result result;
    result.id = id;
    result.bytes = bytes.size();
    const auto hash_start = std::chrono::steady_clock::now();
    const std::string hash = sha1_hex(bytes);
    result.hash_ms = ms_since(hash_start);
    const auto path = cached_path(hash);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        result.error = std::format("cannot write {}", path.string());
        return result;
    }
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    output.close();
    result.path = path;
    return result;
}

// `HttpUtil.downloadFile`: the cached file first, then the url, and the hash
// is checked either way. A mismatch is a failure, not a pack.
PackCache::Result PackCache::fetch(const ServerPackManager::DownloadRequest& request) {
    Result result;
    result.id = request.id;

    if (request.hash.has_value()) {
        const auto path = cached_path(*request.hash);
        std::error_code code;
        if (std::filesystem::exists(path, code)) {
            std::ifstream input(path, std::ios::binary);
            std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)),
                                       std::istreambuf_iterator<char>());
            const auto hash_start = std::chrono::steady_clock::now();
            const bool matches = sha1_hex(bytes) == *request.hash;
            result.hash_ms = ms_since(hash_start);
            if (matches) {
                result.path = path;
                result.from_cache = true;
                result.bytes = bytes.size();
                std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now(),
                                                 code);
                return result;
            }
            std::filesystem::remove(path, code);
        }
    }

    if (!pack_url_allowed(request.url)) {
        result.error = std::format("{} is not an http or https url", request.url);
        return result;
    }

    std::vector<uint8_t> bytes;
    Sink sink{&bytes, limits_.max_bytes, false};
    long status = 0;
    const auto download_start = std::chrono::steady_clock::now();
    CURL* handle = curl_easy_init();
    if (handle == nullptr) {
        result.error = "curl is not available";
        return result;
    }
    curl_slist* headers = nullptr;
    const auto add = [&headers](const std::string& line) {
        headers = curl_slist_append(headers, line.c_str());
    };
    add("X-Minecraft-Username: " + identity_.username);
    add("X-Minecraft-UUID: " + identity_.uuid);
    add("X-Minecraft-Version: " + identity_.version);
    add("X-Minecraft-Version-ID: " + identity_.version_id);
    add("X-Minecraft-Pack-Format: " + identity_.pack_format);
    curl_easy_setopt(handle, CURLOPT_URL, request.url.c_str());
    curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headers);
    const std::string agent = "Minecraft Java/" + identity_.version;
    curl_easy_setopt(handle, CURLOPT_USERAGENT, agent.c_str());
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, collect);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, &sink);
    curl_easy_setopt(handle, CURLOPT_TIMEOUT, limits_.timeout_seconds);
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT, limits_.connect_timeout_seconds);
    curl_easy_setopt(handle, CURLOPT_LOW_SPEED_LIMIT, limits_.low_speed_bytes);
    curl_easy_setopt(handle, CURLOPT_LOW_SPEED_TIME, limits_.low_speed_seconds);
    curl_easy_setopt(handle, CURLOPT_MAXFILESIZE_LARGE,
                     static_cast<curl_off_t>(limits_.max_bytes));
    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 2L);
    const CURLcode code = curl_easy_perform(handle);
    curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(handle);
    curl_slist_free_all(headers);
    result.download_ms = ms_since(download_start);
    result.bytes = bytes.size();

    if (sink.overflowed) {
        result.error = std::format("pack is over the {} byte limit", limits_.max_bytes);
        return result;
    }
    if (code != CURLE_OK) {
        result.error = std::format("{} could not be fetched: {}", request.url,
                                   curl_easy_strerror(code));
        return result;
    }
    if (status != 200) {
        result.error = std::format("{} answered {}", request.url, status);
        return result;
    }

    const auto hash_start = std::chrono::steady_clock::now();
    const std::string hash = sha1_hex(bytes);
    result.hash_ms = ms_since(hash_start);
    if (request.hash.has_value() && hash != *request.hash) {
        result.error = std::format("hash of the downloaded file ({}) is not the one the server "
                                   "promised ({})", hash, *request.hash);
        return result;
    }

    const auto path = cached_path(hash);
    // Written beside the final name and moved into place, so a download that
    // dies halfway cannot leave a short file under a hash that says otherwise.
    const auto temporary = directory_ / (hash + ".part");
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) {
        result.error = std::format("cannot write {}", temporary.string());
        return result;
    }
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    output.close();
    std::error_code move_code;
    std::filesystem::rename(temporary, path, move_code);
    if (move_code) {
        std::filesystem::remove(temporary, move_code);
        result.error = std::format("cannot move the pack into {}", path.string());
        return result;
    }
    result.path = path;
    return result;
}

// `DownloadCacheCleaner.vacuumCacheDir`: the newest are kept by modification
// time, which is bumped on every cache hit.
void PackCache::vacuum(size_t keep) {
    std::error_code code;
    std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> files;
    for (const auto& entry : std::filesystem::directory_iterator(directory_, code)) {
        if (!entry.is_regular_file(code)) continue;
        files.emplace_back(entry.last_write_time(code), entry.path());
    }
    if (files.size() <= keep) return;
    std::sort(files.begin(), files.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    for (size_t i = keep; i < files.size(); ++i) std::filesystem::remove(files[i].second, code);
}

} // namespace mcpp::packs
