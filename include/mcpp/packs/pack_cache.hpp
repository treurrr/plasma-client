#pragma once
#include "mcpp/packs/pack_protocol.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

// Fetching a pack and keeping it. The game caches by hash under
// server-resource-packs/, so a server that pushes the same pack to a thousand
// players' sessions costs each of them one download ever; this does the same.
namespace mcpp::packs {

std::string sha1_hex(std::span<const uint8_t> bytes);

struct DownloadLimits {
    // `DownloadedPackSource.PackDownloader.MAX_PACK_SIZE_BYTES`.
    size_t max_bytes = 262144000;
    // Vanilla leaves the timeout to the JDK's default, which is none at all on
    // a stalled socket; a client that hangs forever on a dead host is not
    // behaviour worth copying, so there is a ceiling and a low-speed cutoff.
    long timeout_seconds = 120;
    long connect_timeout_seconds = 15;
    long low_speed_bytes = 256;
    long low_speed_seconds = 30;
};

// `DownloadQueue`: the cache directory and what it does to one request.
class PackCache {
public:
    struct Result {
        PackId id{};
        std::filesystem::path path;
        std::string error;
        bool from_cache = false;
        // What the three steps cost, for the report.
        double download_ms = 0.0;
        double hash_ms = 0.0;
        uint64_t bytes = 0;
        bool ok() const { return error.empty(); }
    };

    explicit PackCache(std::filesystem::path directory, DownloadLimits limits = DownloadLimits{});

    // `DownloadQueue.MAX_KEPT_PACKS`: the oldest beyond this many are deleted.
    static constexpr size_t kMaxKeptPacks = 20;

    const std::filesystem::path& directory() const { return directory_; }
    // `HttpUtil.cachedFilePath`: a pack lives under its own sha1.
    std::filesystem::path cached_path(const std::string& hash) const;

    // `HttpUtil.downloadFile`: the cached file when its hash matches, else the
    // url, checked against the hash it was promised to have.
    Result fetch(const ServerPackManager::DownloadRequest& request);
    // `DownloadCacheCleaner.vacuumCacheDir`.
    void vacuum(size_t keep = kMaxKeptPacks);

    // The headers vanilla sends, which some servers key their pack on.
    struct Identity {
        std::string username;
        std::string uuid;         // undashed, as vanilla sends it
        std::string version = "1.21.11";
        std::string version_id = "1.21.11";
        std::string pack_format = "75";
    };
    void set_identity(Identity identity) { identity_ = std::move(identity); }

    // For the tests and for a pack handed over on disk: put bytes in the cache
    // under their own hash and hand back the path.
    Result store(const PackId& id, std::span<const uint8_t> bytes);

private:
    std::filesystem::path directory_;
    DownloadLimits limits_;
    Identity identity_;
};

} // namespace mcpp::packs
