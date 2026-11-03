#pragma once
#include "mcpp/entity/assets.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace mcpp::skins {

// What a player's "textures" property names, once its base64 is undone.
struct Textures {
    std::string skin_url;
    bool slim = false;
    std::string cape_url;
};
std::optional<Textures> decode_property(std::string_view base64);

std::vector<uint8_t> base64_decode(std::string_view text);

// One of the eighteen skins in the jar, for a player whose profile names none.
struct Default {
    std::string texture;   // "player/slim/alex"
    bool slim = false;
};
Default default_skin(std::string_view uuid);

// 8-bit PNGs of every colour type, not interlaced: what a skin server sends.
bool decode_png(std::span<const uint8_t> file, entity::Texture& out);

// A skin as the game draws it: always 64x64. An old 64x32 skin gets its left
// arm and leg from the right ones, mirrored, and the parts that have no second
// layer are made opaque, because old skins have rubbish in their alpha there.
// False when the image is no skin at all.
bool normalise(entity::Texture& skin);

// Skins by url, fetched on a worker and kept on disk by the hash the url ends
// in, so a server full of players is a download once and a file read after.
class Cache {
public:
    explicit Cache(std::filesystem::path directory = default_directory());
    ~Cache();

    Cache(const Cache&) = delete;
    Cache& operator=(const Cache&) = delete;

    // The skin if it is in, nullptr while it is on its way or if it failed.
    const entity::Texture* get(const std::string& url);
    bool failed(const std::string& url) const;

    static std::filesystem::path default_directory();

private:
    void work();

    std::filesystem::path directory_;
    mutable std::mutex guard_;
    std::condition_variable wake_;
    std::deque<std::string> queue_;
    std::map<std::string, std::optional<entity::Texture>, std::less<>> done_;
    std::map<std::string, bool, std::less<>> asked_;
    std::atomic<bool> running_{true};
    std::thread worker_;
};

} // namespace mcpp::skins
