#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace mcpp {

// Writes 8-bit RGBA. Rendered frames need to be looked at, not just counted.
void write_png(const std::filesystem::path& path, uint32_t width, uint32_t height,
               std::span<const uint8_t> rgba);

// Every colour type and bit depth a png can carry, interlaced or not, read
// into 8-bit RGBA. A server icon, a panorama face and a skin all arrive as
// png, and none of them is ours to choose the format of.
struct Image {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> rgba;
};
std::optional<Image> read_png(std::span<const uint8_t> bytes);

} // namespace mcpp
