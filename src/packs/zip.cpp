#include "mcpp/packs/zip.hpp"

#include <algorithm>
#include <cstring>
#include <format>
#include <fstream>
#include <zlib.h>

namespace mcpp::packs {
namespace {

constexpr uint32_t kEndOfDirectory = 0x06054b50;
constexpr uint32_t kDirectoryEntry = 0x02014b50;
constexpr uint32_t kLocalHeader = 0x04034b50;
constexpr uint32_t kZip64Locator = 0x07064b50;
constexpr uint32_t kZip64End = 0x06064b50;
constexpr size_t kEndBytes = 22;

uint16_t u16(const uint8_t* at) {
    return static_cast<uint16_t>(at[0] | (at[1] << 8));
}

uint32_t u32(const uint8_t* at) {
    return static_cast<uint32_t>(at[0]) | (static_cast<uint32_t>(at[1]) << 8) |
           (static_cast<uint32_t>(at[2]) << 16) | (static_cast<uint32_t>(at[3]) << 24);
}

uint64_t u64(const uint8_t* at) {
    return static_cast<uint64_t>(u32(at)) | (static_cast<uint64_t>(u32(at + 4)) << 32);
}

} // namespace

bool zip_name_safe(std::string_view name) {
    if (name.empty() || name.size() > 1024) return false;
    if (name.front() == '/' || name.front() == '\\') return false;
    if (name.find('\0') != std::string_view::npos) return false;
    if (name.size() > 1 && name[1] == ':') return false;
    size_t at = 0;
    while (at <= name.size()) {
        const size_t slash = name.find('/', at);
        const std::string_view part = name.substr(at, slash == std::string_view::npos
                                                          ? std::string_view::npos
                                                          : slash - at);
        if (part == "..") return false;
        if (slash == std::string_view::npos) break;
        at = slash + 1;
    }
    return true;
}

bool Zip::fail(std::string message) {
    error_ = std::move(message);
    entries_.clear();
    bytes_.clear();
    opened_ = false;
    return false;
}

void Zip::close() {
    entries_.clear();
    bytes_.clear();
    inflated_ = 0;
    opened_ = false;
    error_.clear();
}

bool Zip::open_file(const std::filesystem::path& path, const ZipLimits& limits) {
    std::error_code code;
    const auto size = std::filesystem::file_size(path, code);
    if (code) return fail(std::format("{} cannot be read", path.string()));
    if (size > limits.archive_bytes) {
        return fail(std::format("{} is {} bytes, over the {} byte limit", path.string(), size,
                                limits.archive_bytes));
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) return fail(std::format("{} cannot be opened", path.string()));
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input) return fail(std::format("{} is shorter than it says", path.string()));
    return open(std::move(bytes), limits);
}

bool Zip::open(std::vector<uint8_t> bytes, const ZipLimits& limits) {
    close();
    if (bytes.size() > limits.archive_bytes) {
        return fail(std::format("archive is {} bytes, over the {} byte limit", bytes.size(),
                                limits.archive_bytes));
    }
    bytes_ = std::move(bytes);
    limits_ = limits;
    if (!index(limits)) return false;
    opened_ = true;
    return true;
}

bool Zip::index(const ZipLimits& limits) {
    if (bytes_.size() < kEndBytes) return fail("not a zip: too short");

    // The end record is last, but a comment may follow it, so it is found by
    // walking back over the comment's own maximum length.
    size_t end = std::string::npos;
    const size_t lowest = bytes_.size() > kEndBytes + 0xFFFF ? bytes_.size() - kEndBytes - 0xFFFF : 0;
    for (size_t at = bytes_.size() - kEndBytes + 1; at-- > lowest;) {
        if (u32(bytes_.data() + at) == kEndOfDirectory) {
            end = at;
            break;
        }
    }
    if (end == std::string::npos) return fail("not a zip: no end of central directory");

    uint64_t count = u16(bytes_.data() + end + 10);
    uint64_t directory_size = u32(bytes_.data() + end + 12);
    uint64_t directory_at = u32(bytes_.data() + end + 16);

    // Zip64, which a pack of any size easily is not, but a pack built by a
    // tool that always writes it would otherwise read as empty.
    if (end >= 20 && u32(bytes_.data() + end - 20) == kZip64Locator) {
        const uint64_t zip64_at = u64(bytes_.data() + end - 20 + 8);
        if (zip64_at + 56 <= bytes_.size() && u32(bytes_.data() + zip64_at) == kZip64End) {
            count = u64(bytes_.data() + zip64_at + 32);
            directory_size = u64(bytes_.data() + zip64_at + 40);
            directory_at = u64(bytes_.data() + zip64_at + 48);
        }
    }

    if (count > limits.entries) {
        return fail(std::format("zip has {} entries, over the {} limit", count, limits.entries));
    }
    if (directory_at > bytes_.size() || directory_size > bytes_.size() - directory_at) {
        return fail("zip central directory is outside the file");
    }

    size_t at = static_cast<size_t>(directory_at);
    const size_t directory_end = at + static_cast<size_t>(directory_size);
    for (uint64_t i = 0; i < count; ++i) {
        if (at + 46 > directory_end) return fail("zip central directory is cut short");
        if (u32(bytes_.data() + at) != kDirectoryEntry) return fail("zip directory entry is damaged");
        Entry entry;
        entry.method = u16(bytes_.data() + at + 10);
        entry.crc = u32(bytes_.data() + at + 16);
        entry.compressed = u32(bytes_.data() + at + 20);
        entry.uncompressed = u32(bytes_.data() + at + 24);
        const uint16_t name_length = u16(bytes_.data() + at + 28);
        const uint16_t extra_length = u16(bytes_.data() + at + 30);
        const uint16_t comment_length = u16(bytes_.data() + at + 32);
        entry.local_offset = u32(bytes_.data() + at + 42);
        entry.order = entries_.size();
        if (at + 46 + name_length + extra_length + comment_length > directory_end) {
            return fail("zip directory entry runs past the directory");
        }
        std::string name(reinterpret_cast<const char*>(bytes_.data() + at + 46), name_length);

        // The zip64 extended field carries the real sizes when the 32-bit
        // ones are saturated.
        size_t extra_at = at + 46 + name_length;
        const size_t extra_end = extra_at + extra_length;
        while (extra_at + 4 <= extra_end) {
            const uint16_t tag = u16(bytes_.data() + extra_at);
            const uint16_t size = u16(bytes_.data() + extra_at + 2);
            if (extra_at + 4 + size > extra_end) break;
            if (tag == 0x0001) {
                size_t field = extra_at + 4;
                if (entry.uncompressed == 0xFFFFFFFFu && field + 8 <= extra_at + 4 + size) {
                    entry.uncompressed = u64(bytes_.data() + field);
                    field += 8;
                }
                if (entry.compressed == 0xFFFFFFFFu && field + 8 <= extra_at + 4 + size) {
                    entry.compressed = u64(bytes_.data() + field);
                    field += 8;
                }
                if (entry.local_offset == 0xFFFFFFFFu && field + 8 <= extra_at + 4 + size) {
                    entry.local_offset = u64(bytes_.data() + field);
                }
            }
            extra_at += 4 + size;
        }

        at += 46 + name_length + extra_length + comment_length;
        if (name.empty() || name.back() == '/') continue;
        if (!zip_name_safe(name)) return fail(std::format("zip entry name {} is not safe", name));
        if (entry.uncompressed > limits.entry_bytes) {
            return fail(std::format("zip entry {} inflates to {} bytes, over the {} byte limit",
                                    name, entry.uncompressed, limits.entry_bytes));
        }
        entries_.emplace(std::move(name), entry);
    }
    return true;
}

std::vector<std::string> Zip::names() const {
    std::vector<std::pair<size_t, const std::string*>> ordered;
    ordered.reserve(entries_.size());
    for (const auto& [name, entry] : entries_) ordered.emplace_back(entry.order, &name);
    std::sort(ordered.begin(), ordered.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<std::string> out;
    out.reserve(ordered.size());
    for (const auto& [order, name] : ordered) out.push_back(*name);
    return out;
}

std::optional<std::vector<uint8_t>> Zip::read(const std::string& name) {
    const auto found = entries_.find(name);
    if (found == entries_.end()) return std::nullopt;
    const Entry& entry = found->second;
    if (inflated_ + entry.uncompressed > limits_.total_bytes) {
        error_ = std::format("reading {} would take the archive over {} inflated bytes", name,
                             limits_.total_bytes);
        return std::nullopt;
    }
    if (entry.local_offset + 30 > bytes_.size()) return std::nullopt;
    const uint8_t* local = bytes_.data() + entry.local_offset;
    if (u32(local) != kLocalHeader) return std::nullopt;
    const size_t data_at = static_cast<size_t>(entry.local_offset) + 30 + u16(local + 26) +
                           u16(local + 28);
    if (data_at > bytes_.size() || entry.compressed > bytes_.size() - data_at) return std::nullopt;

    std::vector<uint8_t> out;
    if (entry.method == 0) {
        if (entry.compressed != entry.uncompressed) return std::nullopt;
        out.assign(bytes_.begin() + static_cast<ptrdiff_t>(data_at),
                   bytes_.begin() + static_cast<ptrdiff_t>(data_at + entry.compressed));
    } else if (entry.method == 8) {
        out.resize(static_cast<size_t>(entry.uncompressed));
        z_stream stream{};
        // A negative window means a raw deflate stream with no zlib wrapper,
        // which is what a zip entry holds.
        if (inflateInit2(&stream, -15) != Z_OK) return std::nullopt;
        stream.next_in = const_cast<uint8_t*>(bytes_.data() + data_at);
        stream.avail_in = static_cast<unsigned>(entry.compressed);
        stream.next_out = out.data();
        stream.avail_out = static_cast<unsigned>(out.size());
        const int status = inflate(&stream, Z_FINISH);
        const bool full = stream.total_out == entry.uncompressed;
        inflateEnd(&stream);
        if ((status != Z_STREAM_END && status != Z_OK) || !full) return std::nullopt;
    } else {
        return std::nullopt;
    }
    if (crc32(0, out.data(), static_cast<unsigned>(out.size())) != entry.crc) return std::nullopt;
    inflated_ += out.size();
    return out;
}

} // namespace mcpp::packs
