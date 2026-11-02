#include "mcpp/image_png.hpp"

#include "mcpp/byte_buffer.hpp"

#include <cstring>
#include <format>
#include <fstream>
#include <vector>

#include <zlib.h>

namespace mcpp {
namespace {

void push_u32(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>(value >> 24));
    out.push_back(static_cast<uint8_t>(value >> 16));
    out.push_back(static_cast<uint8_t>(value >> 8));
    out.push_back(static_cast<uint8_t>(value));
}

void push_chunk(std::vector<uint8_t>& out, const char type[4], std::span<const uint8_t> payload) {
    push_u32(out, static_cast<uint32_t>(payload.size()));
    const size_t crc_start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), payload.begin(), payload.end());
    const uLong crc = ::crc32(0, out.data() + crc_start, static_cast<uInt>(out.size() - crc_start));
    push_u32(out, static_cast<uint32_t>(crc));
}

} // namespace

void write_png(const std::filesystem::path& path, uint32_t width, uint32_t height,
               std::span<const uint8_t> rgba) {
    if (rgba.size() != static_cast<size_t>(width) * height * 4) {
        throw ProtocolError(std::format("png: {} bytes voor {}x{} pixels", rgba.size(), width,
                                        height));
    }

    std::vector<uint8_t> raw;
    raw.reserve(rgba.size() + height);
    for (uint32_t y = 0; y < height; ++y) {
        raw.push_back(0);   // filter: none
        const auto* row = rgba.data() + static_cast<size_t>(y) * width * 4;
        raw.insert(raw.end(), row, row + static_cast<size_t>(width) * 4);
    }

    uLongf compressed_size = ::compressBound(static_cast<uLong>(raw.size()));
    std::vector<uint8_t> compressed(compressed_size);
    if (::compress2(compressed.data(), &compressed_size, raw.data(),
                    static_cast<uLong>(raw.size()), Z_BEST_SPEED) != Z_OK) {
        throw ProtocolError("png: zlib-compressie faalde");
    }
    compressed.resize(compressed_size);

    std::vector<uint8_t> file{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};

    std::vector<uint8_t> ihdr;
    push_u32(ihdr, width);
    push_u32(ihdr, height);
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});   // 8-bit, RGBA, deflate, no filter, no interlace
    push_chunk(file, "IHDR", ihdr);
    push_chunk(file, "IDAT", compressed);
    push_chunk(file, "IEND", {});

    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream out(path, std::ios::binary);
    if (!out) throw ProtocolError(std::format("kan {} niet schrijven", path.string()));
    out.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
}

} // namespace mcpp
