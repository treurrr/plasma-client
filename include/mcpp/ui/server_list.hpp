#pragma once
#include "mcpp/nbt.hpp"

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
    // Every field of the entry as it was read, so a field a newer game wrote
    // and this client knows nothing of survives a save.
    nbt::Value extra;

    // What the status ping found, which is not in the file: the list's own
    // working copy of the server's answer.
    enum class State : uint8_t { Initial, Pinging, Successful, Incompatible, Unreachable };
    State state = State::Initial;
    int64_t ping = -1;
    std::string version;
    int32_t protocol = -1;
    int32_t players_online = -1;
    int32_t players_max = -1;
    std::string motd;
    std::vector<std::string> player_list;
    std::vector<uint8_t> icon_png;
};

class ServerList {
public:
    std::vector<ServerEntry> entries;
    // Whatever else the file's root compound held beside "servers".
    nbt::Compound root_extra;

    // The ones the list shows, as indices into entries.
    std::vector<size_t> visible() const;

    static std::optional<ServerList> decode(std::span<const uint8_t> bytes);
    // The entry whose address matches, visible or hidden.
    ServerEntry* find(std::string_view ip);
    // A hidden entry made visible again, as adding a server the file already
    // holds does; null when there is none.
    ServerEntry* unhide(std::string_view ip);
    void add(ServerEntry entry, bool hidden);
    void remove(size_t index);
    void swap(size_t a, size_t b);
    std::vector<uint8_t> encode() const;
    bool load(const std::filesystem::path& path);
    bool save(const std::filesystem::path& path) const;

    static std::filesystem::path default_path();
    static std::optional<std::filesystem::path> vanilla_path();
};

std::vector<uint8_t> base64_decode(std::string_view text);
std::string base64_encode(std::span<const uint8_t> bytes);

} // namespace mcpp::ui
