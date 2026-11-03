#include "mcpp/image_png.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string_view>

#include <zlib.h>

namespace mcpp {
namespace {

uint32_t be32(const uint8_t* p) {
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

struct Pass {
    uint32_t x0, y0, dx, dy;
};
constexpr Pass kAdam7[7] = {{0, 0, 8, 8}, {4, 0, 8, 8}, {0, 4, 4, 8}, {2, 0, 4, 4},
                            {0, 2, 2, 4}, {1, 0, 2, 2}, {0, 1, 1, 2}};
constexpr Pass kWhole[1] = {{0, 0, 1, 1}};

} // namespace

std::optional<Image> read_png(std::span<const uint8_t> bytes) {
    static constexpr uint8_t kSignature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (bytes.size() < 8 || std::memcmp(bytes.data(), kSignature, 8) != 0) return std::nullopt;

    uint32_t width = 0, height = 0;
    uint8_t depth = 0, colour = 0, interlace = 0;
    std::vector<uint8_t> compressed;
    std::vector<uint8_t> palette;
    std::vector<uint8_t> palette_alpha;
    int32_t transparent[3] = {-1, -1, -1};
    for (size_t at = 8; at + 12 <= bytes.size();) {
        const uint32_t length = be32(bytes.data() + at);
        if (length > bytes.size() || at + 12 + length > bytes.size()) return std::nullopt;
        const std::string_view type(reinterpret_cast<const char*>(bytes.data() + at + 4), 4);
        const uint8_t* body = bytes.data() + at + 8;
        if (type == "IHDR" && length >= 13) {
            width = be32(body);
            height = be32(body + 4);
            depth = body[8];
            colour = body[9];
            interlace = body[12];
        } else if (type == "PLTE") {
            palette.assign(body, body + length);
        } else if (type == "tRNS") {
            if (colour == 3) {
                palette_alpha.assign(body, body + length);
            } else if (colour == 0 && length >= 2) {
                transparent[0] = (body[0] << 8) | body[1];
            } else if (colour == 2 && length >= 6) {
                for (int i = 0; i < 3; ++i) transparent[i] = (body[i * 2] << 8) | body[i * 2 + 1];
            }
        } else if (type == "IDAT") {
            compressed.insert(compressed.end(), body, body + length);
        } else if (type == "IEND") {
            break;
        }
        at += 12 + length;
    }
    if (width == 0 || height == 0 || width > 16384 || height > 16384) return std::nullopt;
    if (depth != 1 && depth != 2 && depth != 4 && depth != 8 && depth != 16) return std::nullopt;

    int channels = 0;
    switch (colour) {
    case 0: channels = 1; break;
    case 2: channels = 3; break;
    case 3: channels = 1; break;
    case 4: channels = 2; break;
    case 6: channels = 4; break;
    default: return std::nullopt;
    }
    const size_t bits = static_cast<size_t>(channels) * depth;
    const size_t pixel_bytes = std::max<size_t>(1, bits / 8);
    const std::span<const Pass> passes =
        interlace != 0 ? std::span<const Pass>(kAdam7) : std::span<const Pass>(kWhole);

    const auto pass_size = [&](const Pass& pass, uint32_t& w, uint32_t& h) {
        if (pass.x0 >= width || pass.y0 >= height) return false;
        w = (width - pass.x0 + pass.dx - 1) / pass.dx;
        h = (height - pass.y0 + pass.dy - 1) / pass.dy;
        return true;
    };
    size_t expected = 0;
    for (const auto& pass : passes) {
        uint32_t w = 0, h = 0;
        if (!pass_size(pass, w, h)) continue;
        expected += static_cast<size_t>(h) * (1 + (static_cast<size_t>(w) * bits + 7) / 8);
    }
    std::vector<uint8_t> raw(expected);
    uLongf raw_size = static_cast<uLongf>(raw.size());
    if (::uncompress(raw.data(), &raw_size, compressed.data(),
                     static_cast<uLong>(compressed.size())) != Z_OK ||
        raw_size != raw.size()) {
        return std::nullopt;
    }

    Image image;
    image.width = width;
    image.height = height;
    image.rgba.assign(static_cast<size_t>(width) * height * 4, 0);
    const uint32_t max_sample = (1u << depth) - 1;
    const auto sample = [&](const uint8_t* row, size_t index) -> uint32_t {
        if (depth == 8) return row[index];
        if (depth == 16) return (uint32_t{row[index * 2]} << 8) | row[index * 2 + 1];
        const size_t bit = index * depth;
        return (row[bit / 8] >> (8 - depth - bit % 8)) & max_sample;
    };
    const auto to8 = [&](uint32_t value) -> uint8_t {
        if (depth == 16) return static_cast<uint8_t>(value >> 8);
        return static_cast<uint8_t>(value * 255 / max_sample);
    };

    size_t offset = 0;
    for (const auto& pass : passes) {
        uint32_t w = 0, h = 0;
        if (!pass_size(pass, w, h)) continue;
        const size_t stride = (static_cast<size_t>(w) * bits + 7) / 8;
        std::vector<uint8_t> previous(stride, 0);
        for (uint32_t y = 0; y < h; ++y) {
            const uint8_t filter = raw[offset];
            uint8_t* row = raw.data() + offset + 1;
            for (size_t i = 0; i < stride; ++i) {
                const int a = i >= pixel_bytes ? row[i - pixel_bytes] : 0;
                const int b = previous[i];
                const int c = i >= pixel_bytes ? previous[i - pixel_bytes] : 0;
                switch (filter) {
                case 1: row[i] = static_cast<uint8_t>(row[i] + a); break;
                case 2: row[i] = static_cast<uint8_t>(row[i] + b); break;
                case 3: row[i] = static_cast<uint8_t>(row[i] + (a + b) / 2); break;
                case 4: row[i] = static_cast<uint8_t>(row[i] + paeth(a, b, c)); break;
                default: break;
                }
            }
            for (uint32_t x = 0; x < w; ++x) {
                uint8_t* out =
                    image.rgba.data() + ((static_cast<size_t>(pass.y0 + y * pass.dy) * width) +
                                         pass.x0 + x * pass.dx) *
                                            4;
                const size_t base = static_cast<size_t>(x) * static_cast<size_t>(channels);
                if (colour == 3) {
                    const uint32_t index = sample(row, base);
                    if (index * 3 + 2 < palette.size()) {
                        out[0] = palette[index * 3];
                        out[1] = palette[index * 3 + 1];
                        out[2] = palette[index * 3 + 2];
                    }
                    out[3] = index < palette_alpha.size() ? palette_alpha[index] : 255;
                } else if (colour == 0 || colour == 4) {
                    const uint32_t grey = sample(row, base);
                    out[0] = out[1] = out[2] = to8(grey);
                    if (colour == 4) {
                        out[3] = to8(sample(row, base + 1));
                    } else {
                        out[3] = static_cast<int32_t>(grey) == transparent[0] ? 0 : 255;
                    }
                } else {
                    const uint32_t r = sample(row, base);
                    const uint32_t g = sample(row, base + 1);
                    const uint32_t b = sample(row, base + 2);
                    out[0] = to8(r);
                    out[1] = to8(g);
                    out[2] = to8(b);
                    if (colour == 6) {
                        out[3] = to8(sample(row, base + 3));
                    } else {
                        const bool clear = static_cast<int32_t>(r) == transparent[0] &&
                                           static_cast<int32_t>(g) == transparent[1] &&
                                           static_cast<int32_t>(b) == transparent[2];
                        out[3] = clear ? 0 : 255;
                    }
                }
            }
            std::memcpy(previous.data(), row, stride);
            offset += 1 + stride;
        }
    }
    return image;
}

} // namespace mcpp
