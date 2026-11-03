#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

// servers.dat, in the game's own NBT, so the list a player already has is the
// list they see here.
namespace mcpp::ui {

struct ServerEntry {
    enum class Packs : uint8_t { Enabled, Disabled, Prompt };

    std::string name;
    std::string ip;
    // The icon as the file keeps it: base64 of a 64x64 png.
    std::string icon;
    Packs packs = Packs::Prompt;
    // The game keeps the last Direct Connection address in the file too, out
    // of sight of the list.
    bool hidden = false;
};

class ServerList {
public:
    std::vector<ServerEntry> entries;

    // The ones the list shows, as indices into entries.
    std::vector<size_t> visible() const;

    static std::optional<ServerList> decode(std::span<const uint8_t> bytes);
    std::vector<uint8_t> encode() const;
    bool load(const std::filesystem::path& path);
    bool save(const std::filesystem::path& path) const;

    static std::filesystem::path default_path();
    static std::optional<std::filesystem::path> vanilla_path();
};

std::vector<uint8_t> base64_decode(std::string_view text);
std::string base64_encode(std::span<const uint8_t> bytes);

} // namespace mcpp::ui
