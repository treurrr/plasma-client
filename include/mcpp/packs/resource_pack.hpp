#pragma once
#include "mcpp/packs/zip.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

// One pack, opened. A pack is a zip whose files sit under assets/<namespace>/,
// with an optional set of overlay directories that stand in front of them for
// a given game version. Resolving a path means asking the applicable overlays
// first, newest declared last, and the pack root behind them.
namespace mcpp::packs {

// `SharedConstants.getCurrentVersion`: the pack format this client is.
constexpr int kPackFormatMajor = 75;
constexpr int kPackFormatMinor = 0;
// `PackFormat.lastPreMinorVersion(CLIENT_RESOURCES)`: up to here a pack says
// one `pack_format` number and no range at all.
constexpr int kLastPreMinorFormat = 64;

// `PackFormat`: major.minor, ordered.
struct PackFormat {
    int major = 0;
    int minor = 0;
    auto operator<=>(const PackFormat&) const = default;
};

class ResourcePack {
public:
    // `PackMetadataSection` and `OverlayMetadataSection`.
    struct Meta {
        std::string description;
        PackFormat min{0, 0};
        PackFormat max{0, 0};
        bool compatible = false;
        // `OverlayMetadataSection.overlaysForVersion`, in the order declared.
        std::vector<std::string> overlays;
    };

    bool open(const std::filesystem::path& path, const ZipLimits& limits = ZipLimits{});
    bool open_bytes(std::vector<uint8_t> bytes, const ZipLimits& limits = ZipLimits{});
    void close();
    bool is_open() const { return open_; }
    const std::string& error() const { return error_; }
    const Meta& meta() const { return meta_; }

    // A logical path, "assets/minecraft/textures/block/stone.png". The last
    // applicable overlay that has it wins; the pack root is the fallback.
    std::optional<std::vector<uint8_t>> read(std::string_view asset);
    bool has(std::string_view asset) const;
    // Every logical path the pack offers that starts with this prefix and ends
    // with this suffix, overlays folded in, sorted and without duplicates.
    std::vector<std::string> list(std::string_view prefix, std::string_view suffix) const;

    Zip& archive() { return zip_; }

private:
    bool read_metadata();
    std::optional<std::string> locate(std::string_view asset) const;

    Zip zip_;
    Meta meta_;
    bool open_ = false;
    std::string error_;
    // The overlay directories that apply, nearest first.
    std::vector<std::string> search_;
};

// `PackRepository`: the packs the server pushed, the last one on top. Reading
// asks the top pack first, so a pack pushed later replaces what one pushed
// earlier put there.
class PackStack {
public:
    void clear() { packs_.clear(); }
    // Pushed bottom first, the way `PackReloadConfig.Callbacks.packsToLoad` lists them.
    bool add(const std::filesystem::path& path, const ZipLimits& limits = ZipLimits{});
    bool add_bytes(std::vector<uint8_t> bytes, const ZipLimits& limits = ZipLimits{});
    size_t size() const { return packs_.size(); }
    bool empty() const { return packs_.empty(); }
    const std::string& error() const { return error_; }

    std::optional<std::vector<uint8_t>> read(std::string_view asset);
    bool has(std::string_view asset) const;
    std::vector<std::string> list(std::string_view prefix, std::string_view suffix) const;

private:
    std::vector<std::unique_ptr<ResourcePack>> packs_;
    std::string error_;
};

// `OverlayMetadataSection.validateOverlayDir`.
bool overlay_dir_valid(std::string_view name);

} // namespace mcpp::packs
