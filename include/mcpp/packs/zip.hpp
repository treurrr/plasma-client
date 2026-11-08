#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

// A resource pack is a zip from a stranger. It is read here without being
// unpacked to disk: the central directory is indexed once and an entry is
// inflated only when something asks for it, with every size the file claims
// about itself checked against a cap before a byte is allocated.
namespace mcpp::packs {

// What a pack may claim about itself before the claim is refused.
struct ZipLimits {
    // `DownloadedPackSource.createDownloader`: the cap the game puts on the
    // file it fetches.
    size_t archive_bytes = 262144000;
    // What one entry may inflate to, and what the whole archive may, so a
    // pack cannot hand us a gigabyte from a kilobyte.
    size_t entry_bytes = 64u << 20;
    size_t total_bytes = 512u << 20;
    size_t entries = 65535;
};

class Zip {
public:
    using Limits = ZipLimits;

    Zip() = default;

    bool open(std::vector<uint8_t> bytes, const ZipLimits& limits = ZipLimits{});
    bool open_file(const std::filesystem::path& path, const ZipLimits& limits = ZipLimits{});
    void close();
    bool is_open() const { return !entries_.empty() || opened_; }

    const std::string& error() const { return error_; }
    // Names as they appear in the archive, in central-directory order.
    std::vector<std::string> names() const;
    bool contains(const std::string& name) const { return entries_.count(name) != 0; }
    // The entry inflated, or nothing when it is absent, damaged, or over a cap.
    std::optional<std::vector<uint8_t>> read(const std::string& name);
    // How much has been inflated out of this archive so far.
    size_t inflated_bytes() const { return inflated_; }
    size_t archive_bytes() const { return bytes_.size(); }

private:
    struct Entry {
        uint16_t method = 0;
        uint32_t crc = 0;
        uint64_t compressed = 0;
        uint64_t uncompressed = 0;
        uint64_t local_offset = 0;
        size_t order = 0;
    };

    bool fail(std::string message);
    bool index(const ZipLimits& limits);

    std::vector<uint8_t> bytes_;
    std::map<std::string, Entry> entries_;
    ZipLimits limits_;
    size_t inflated_ = 0;
    bool opened_ = false;
    std::string error_;
};

// A name a pack may not have: absolute, or climbing out of the pack. Both
// would only matter if we wrote entries to disk, which we do not, but a name
// like that is a sign the archive is not what it says it is.
bool zip_name_safe(std::string_view name);

} // namespace mcpp::packs
