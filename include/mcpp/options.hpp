#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The game's own options.txt: one key:value line per option, in the game's
// own order, with the game's own defaults, so a file written here reads back
// in vanilla and the other way round. Keys this client does not know are kept
// and written back where they were.
namespace mcpp {

class Options {
public:
    Options();

    // Lines of a file laid over the defaults. Malformed lines are skipped,
    // the way the game skips them.
    void parse(std::string_view text);
    std::string serialise() const;
    bool load(const std::filesystem::path& path);
    bool save(const std::filesystem::path& path) const;

    // Plasma's own file, beside its own servers.dat.
    static std::filesystem::path default_path();
    // The launcher's options.txt, read once to carry the player's settings
    // across; never written.
    static std::optional<std::filesystem::path> vanilla_path();

    const std::string* raw(std::string_view key) const;
    void set_raw(std::string_view key, std::string value);

    bool get_bool(std::string_view key) const;
    int get_int(std::string_view key) const;
    double get_double(std::string_view key) const;
    // A quoted value with its quotes taken off: graphicsPreset:"fancy".
    std::string get_string(std::string_view key) const;

    void set_bool(std::string_view key, bool value);
    void set_int(std::string_view key, int value);
    void set_double(std::string_view key, double value);
    void set_string(std::string_view key, std::string_view value);

    const std::vector<std::pair<std::string, std::string>>& entries() const { return entries_; }
    // The value a first run of the game writes.
    static const std::string* default_of(std::string_view key);

private:
    std::vector<std::pair<std::string, std::string>> entries_;
};

std::string format_double(double value);

} // namespace mcpp
