#include "mcpp/skins.hpp"

#include "mcpp/json.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <format>
#include <fstream>

#include <curl/curl.h>
#include <zlib.h>

namespace mcpp::skins {
namespace {

constexpr std::array<std::string_view, 9> kDefaults = {
    "alex", "ari", "efe", "kai", "makena", "noor", "steve", "sunny", "zuri"};

uint32_t big_endian(const uint8_t* p) {
    return (uint32_t{p[0]} << 24) | (uint32_t{p[1]} << 16) | (uint32_t{p[2]} << 8) | p[3];
}

uint8_t paeth(int a, int b, int c) {
    const int p = a + b - c;
    const int pa = std::abs(p - a);
    const int pb = std::abs(p - b);
    const int pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) return static_cast<uint8_t>(a);
    if (pb <= pc) return static_cast<uint8_t>(b);
    return static_cast<uint8_t>(c);
}

// Copies a w x h rectangle, flipped left to right.
void copy_flipped(entity::Texture& t, int from_x, int from_y, int to_x, int to_y, int w, int h) {
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const size_t from = (size_t(from_y + y) * t.width + size_t(from_x + x)) * 4;
            const size_t to = (size_t(to_y + y) * t.width + size_t(to_x + w - 1 - x)) * 4;
            std::memcpy(&t.rgba[to], &t.rgba[from], 4);
        }
    }
}

// A 4x12x4 limb's island, put back together as the limb on the other side:
// every face mirrored, and the two sides changing places.
void mirror_limb(entity::Texture& t, int from_u, int from_v, int to_u, int to_v) {
    copy_flipped(t, from_u + 4, from_v, to_u + 4, to_v, 4, 4);
    copy_flipped(t, from_u + 8, from_v, to_u + 8, to_v, 4, 4);
    copy_flipped(t, from_u + 8, from_v + 4, to_u, to_v + 4, 4, 12);
    copy_flipped(t, from_u + 4, from_v + 4, to_u + 4, to_v + 4, 4, 12);
    copy_flipped(t, from_u, from_v + 4, to_u + 8, to_v + 4, 4, 12);
    copy_flipped(t, from_u + 12, from_v + 4, to_u + 12, to_v + 4, 4, 12);
}

void set_alpha(entity::Texture& t, int x0, int y0, int x1, int y1, uint8_t alpha) {
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) t.rgba[(size_t(y) * t.width + size_t(x)) * 4 + 3] = alpha;
    }
}

bool all_opaque(const entity::Texture& t, int x0, int y0, int x1, int y1) {
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            if (t.rgba[(size_t(y) * t.width + size_t(x)) * 4 + 3] < 128) return false;
        }
    }
    return true;
}

size_t collect(char* data, size_t size, size_t count, void* out) {
    auto* bytes = static_cast<std::vector<uint8_t>*>(out);
    if (bytes->size() > (1u << 20)) return 0;
    bytes->insert(bytes->end(), data, data + size * count);
    return size * count;
}

// Only Mojang's own texture host: a server cannot make the client fetch
// whatever it names. The game asks for these over plain http; the same host
// answers https.
std::optional<std::string> trusted(std::string_view url) {
    for (const std::string_view scheme : {"http://", "https://"}) {
        if (!url.starts_with(scheme)) continue;
        const auto rest = url.substr(scheme.size());
        if (!rest.starts_with("textures.minecraft.net/texture/")) return std::nullopt;
        const auto hash = rest.substr(rest.rfind('/') + 1);
        if (hash.empty() || !std::ranges::all_of(hash, [](char c) {
                return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
            })) {
            return std::nullopt;
        }
        return std::string(hash);
    }
    return std::nullopt;
}

} // namespace

std::vector<uint8_t> base64_decode(std::string_view text) {
    std::vector<uint8_t> out;
    uint32_t buffer = 0;
    int bits = 0;
    for (const char c : text) {
        int value = -1;
        if (c >= 'A' && c <= 'Z') value = c - 'A';
        else if (c >= 'a' && c <= 'z') value = c - 'a' + 26;
        else if (c >= '0' && c <= '9') value = c - '0' + 52;
        else if (c == '+' || c == '-') value = 62;
        else if (c == '/' || c == '_') value = 63;
        else continue;
        buffer = (buffer << 6) | static_cast<uint32_t>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((buffer >> bits) & 0xFF));
        }
    }
    return out;
}

std::optional<Textures> decode_property(std::string_view base64) {
    const auto bytes = base64_decode(base64);
    try {
        const auto root = json::parse(std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                                       bytes.size()));
        const auto* textures = root.find("textures");
        if (textures == nullptr) return std::nullopt;
        Textures out;
        if (const auto* skin = textures->find("SKIN")) {
            if (const auto* url = skin->find("url")) out.skin_url = url->string_or("");
            if (const auto* metadata = skin->find("metadata")) {
                if (const auto* model = metadata->find("model")) {
                    out.slim = model->string_or("") == "slim";
                }
            }
        }
        if (const auto* cape = textures->find("CAPE")) {
            if (const auto* url = cape->find("url")) out.cape_url = url->string_or("");
        }
        return out;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

// Java's UUID hash, folded to an int, picks one of the eighteen; which order
// the eighteen are in is a guess to be checked against offline players seen
// in the vanilla client.
Default default_skin(std::string_view uuid) {
    uint64_t halves[2] = {0, 0};
    size_t digit = 0;
    for (const char c : uuid) {
        int value = -1;
        if (c >= '0' && c <= '9') value = c - '0';
        else if (c >= 'a' && c <= 'f') value = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') value = c - 'A' + 10;
        if (value < 0 || digit >= 32) continue;
        halves[digit / 16] = (halves[digit / 16] << 4) | static_cast<uint64_t>(value);
        ++digit;
    }
    const uint64_t folded = halves[0] ^ halves[1];
    const auto hash = static_cast<int32_t>(static_cast<uint32_t>(folded >> 32) ^
                                           static_cast<uint32_t>(folded));
    const int32_t count = static_cast<int32_t>(kDefaults.size() * 2);
    const int32_t index = ((hash % count) + count) % count;
    Default out;
    out.slim = index < static_cast<int32_t>(kDefaults.size());
    out.texture = std::format("player/{}/{}", out.slim ? "slim" : "wide",
                              kDefaults[static_cast<size_t>(index) % kDefaults.size()]);
    return out;
}

bool decode_png(std::span<const uint8_t> file, entity::Texture& out) {
    static constexpr uint8_t kSignature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    if (file.size() < 8 || std::memcmp(file.data(), kSignature, 8) != 0) return false;
    uint32_t width = 0;
    uint32_t height = 0;
    uint8_t depth = 0;
    uint8_t colour = 0;
    std::vector<uint8_t> packed;
    std::vector<uint8_t> palette;
    std::vector<uint8_t> palette_alpha;
    size_t at = 8;
    while (at + 12 <= file.size()) {
        const uint32_t length = big_endian(&file[at]);
        if (at + 12 + length > file.size()) return false;
        const std::string_view type(reinterpret_cast<const char*>(&file[at + 4]), 4);
        const uint8_t* body = &file[at + 8];
        if (type == "IHDR") {
            if (length < 13) return false;
            width = big_endian(body);
            height = big_endian(body + 4);
            depth = body[8];
            colour = body[9];
            if (body[12] != 0) return false;   // interlaced
        } else if (type == "PLTE") {
            palette.assign(body, body + length);
        } else if (type == "tRNS") {
            palette_alpha.assign(body, body + length);
        } else if (type == "IDAT") {
            packed.insert(packed.end(), body, body + length);
        } else if (type == "IEND") {
            break;
        }
        at += 12 + length;
    }
    if (width == 0 || height == 0 || width > 4096 || height > 4096) return false;
    const int channels = colour == 0 ? 1 : colour == 2 ? 3 : colour == 3 ? 1 : colour == 4 ? 2
                       : colour == 6 ? 4 : 0;
    if (channels == 0) return false;
    if (depth != 8 && !(colour == 3 && (depth == 1 || depth == 2 || depth == 4))) return false;

    const size_t row_bytes = (size_t{width} * channels * depth + 7) / 8;
    std::vector<uint8_t> raw((row_bytes + 1) * height);
    uLongf raw_size = static_cast<uLongf>(raw.size());
    if (uncompress(raw.data(), &raw_size, packed.data(), static_cast<uLong>(packed.size())) !=
            Z_OK ||
        raw_size != raw.size()) {
        return false;
    }

    const size_t step = std::max<size_t>(1, static_cast<size_t>(channels) * depth / 8);
    std::vector<uint8_t> rows(row_bytes * height);
    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t filter = raw[y * (row_bytes + 1)];
        const uint8_t* line = &raw[y * (row_bytes + 1) + 1];
        uint8_t* here = &rows[y * row_bytes];
        const uint8_t* above = y > 0 ? &rows[(y - 1) * row_bytes] : nullptr;
        for (size_t i = 0; i < row_bytes; ++i) {
            const int left = i >= step ? here[i - step] : 0;
            const int up = above != nullptr ? above[i] : 0;
            const int corner = above != nullptr && i >= step ? above[i - step] : 0;
            int predicted = 0;
            switch (filter) {
            case 0: predicted = 0; break;
            case 1: predicted = left; break;
            case 2: predicted = up; break;
            case 3: predicted = (left + up) / 2; break;
            case 4: predicted = paeth(left, up, corner); break;
            default: return false;
            }
            here[i] = static_cast<uint8_t>(line[i] + predicted);
        }
    }

    out.width = width;
    out.height = height;
    out.rgba.assign(size_t{width} * height * 4, 255);
    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* line = &rows[y * row_bytes];
        for (uint32_t x = 0; x < width; ++x) {
            uint8_t* pixel = &out.rgba[(size_t{y} * width + x) * 4];
            switch (colour) {
            case 0: pixel[0] = pixel[1] = pixel[2] = line[x]; break;
            case 2: std::memcpy(pixel, line + x * 3, 3); break;
            case 4:
                pixel[0] = pixel[1] = pixel[2] = line[x * 2];
                pixel[3] = line[x * 2 + 1];
                break;
            case 6: std::memcpy(pixel, line + x * 4, 4); break;
            default: {
                const uint32_t bit = x * depth;
                const uint32_t index =
                    (line[bit / 8] >> (8 - depth - bit % 8)) & ((1u << depth) - 1);
                if (index * 3 + 2 >= palette.size()) return false;
                std::memcpy(pixel, &palette[index * 3], 3);
                pixel[3] = index < palette_alpha.size() ? palette_alpha[index] : 255;
            }
            }
        }
    }
    return true;
}

bool normalise(entity::Texture& skin) {
    if (skin.width != 64 || (skin.height != 32 && skin.height != 64)) return false;
    if (skin.height == 32) {
        skin.height = 64;
        skin.rgba.resize(64 * 64 * 4, 0);
        // A legacy hat that is solid everywhere was drawn by an editor that
        // filled the whole sheet; the game reads it as no hat.
        if (all_opaque(skin, 32, 0, 64, 16)) set_alpha(skin, 32, 0, 64, 16, 0);
        mirror_limb(skin, 0, 16, 16, 48);
        mirror_limb(skin, 40, 16, 32, 48);
    }
    set_alpha(skin, 0, 0, 32, 16, 255);
    set_alpha(skin, 0, 16, 64, 32, 255);
    set_alpha(skin, 16, 48, 48, 64, 255);
    return true;
}

std::filesystem::path Cache::default_directory() {
    std::filesystem::path base;
    if (const char* cache = std::getenv("XDG_CACHE_HOME")) {
        base = cache;
    } else if (const char* home = std::getenv("HOME")) {
        base = std::filesystem::path(home) / ".cache";
    }
    return base / "plasma-client" / "skins";
}

Cache::Cache(std::filesystem::path directory)
    : directory_(std::move(directory)), worker_([this] { work(); }) {}

Cache::~Cache() {
    running_.store(false);
    wake_.notify_all();
    worker_.join();
}

const entity::Texture* Cache::get(const std::string& url) {
    std::lock_guard lock(guard_);
    if (const auto found = done_.find(url); found != done_.end()) {
        return found->second ? &*found->second : nullptr;
    }
    if (asked_.emplace(url, true).second) {
        queue_.push_back(url);
        wake_.notify_one();
    }
    return nullptr;
}

bool Cache::failed(const std::string& url) const {
    std::lock_guard lock(guard_);
    const auto found = done_.find(url);
    return found != done_.end() && !found->second;
}

void Cache::work() {
    while (true) {
        std::string url;
        {
            std::unique_lock lock(guard_);
            wake_.wait(lock, [&] { return !queue_.empty() || !running_.load(); });
            if (!running_.load()) return;
            url = std::move(queue_.front());
            queue_.pop_front();
        }

        std::optional<entity::Texture> result;
        if (const auto hash = trusted(url)) {
            const auto file = directory_ / (*hash + ".png");
            std::vector<uint8_t> bytes;
            if (std::ifstream cached(file, std::ios::binary); cached) {
                bytes.assign(std::istreambuf_iterator<char>(cached), {});
            }
            entity::Texture texture;
            bool fresh = false;
            bool have = decode_png(bytes, texture);
            if (!have) {
                bytes.clear();
                if (CURL* handle = curl_easy_init()) {
                    const auto address = "https://textures.minecraft.net/texture/" + *hash;
                    curl_easy_setopt(handle, CURLOPT_URL, address.c_str());
                    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, collect);
                    curl_easy_setopt(handle, CURLOPT_WRITEDATA, &bytes);
                    curl_easy_setopt(handle, CURLOPT_TIMEOUT, 15L);
                    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 1L);
                    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 2L);
                    long status = 0;
                    if (curl_easy_perform(handle) == CURLE_OK) {
                        curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status);
                    }
                    curl_easy_cleanup(handle);
                    have = fresh = status == 200 && decode_png(bytes, texture);
                }
            }
            if (have && normalise(texture)) {
                if (fresh) {
                    std::error_code ignored;
                    std::filesystem::create_directories(directory_, ignored);
                    std::ofstream(file, std::ios::binary)
                        .write(reinterpret_cast<const char*>(bytes.data()),
                               static_cast<std::streamsize>(bytes.size()));
                }
                result = std::move(texture);
            }
        }
        std::lock_guard lock(guard_);
        done_.insert_or_assign(url, std::move(result));
    }
}

} // namespace mcpp::skins
