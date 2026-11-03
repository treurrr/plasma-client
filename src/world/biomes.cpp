#include "mcpp/world/biomes.hpp"

#include "mcpp/image_png.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>

namespace mcpp::world {
namespace {

constexpr size_t kMapSide = 256;
constexpr size_t kMapBytes = kMapSide * kMapSide * 3;
constexpr uint32_t kDefaultGrass = 0x91BD59;
constexpr uint32_t kDefaultFoliage = 0x77AB2F;

} // namespace

const Biome* BiomeTable::at(size_t index) const {
    return index < biomes_.size() ? &biomes_[index] : nullptr;
}

uint32_t BiomeTable::grass(size_t index, const Colormaps& maps) const {
    const Biome* biome = at(index);
    if (biome == nullptr) return kDefaultGrass;
    if (biome->fixed_grass) return biome->grass;
    return maps.grass(biome->temperature, biome->downfall);
}

uint32_t BiomeTable::foliage(size_t index, const Colormaps& maps) const {
    const Biome* biome = at(index);
    if (biome == nullptr) return kDefaultFoliage;
    if (biome->fixed_foliage) return biome->foliage;
    return maps.foliage(biome->temperature, biome->downfall);
}

uint32_t BiomeTable::water(size_t index) const {
    const Biome* biome = at(index);
    return biome != nullptr ? biome->water : 0x3F76E4u;
}

std::filesystem::path Colormaps::default_path(int32_t protocol) {
    std::filesystem::path base;
    if (const char* data_home = std::getenv("XDG_DATA_HOME")) {
        base = data_home;
    } else if (const char* home = std::getenv("HOME")) {
        base = std::filesystem::path(home) / ".local" / "share";
    }
    return base / "plasma-client" / "data" / ("colormaps-" + std::to_string(protocol) + ".plcm");
}

bool Colormaps::open(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    char magic[4] = {};
    input.read(magic, 4);
    if (std::memcmp(magic, "PLCM", 4) != 0) return false;
    grass_.resize(kMapBytes);
    foliage_.resize(kMapBytes);
    input.read(reinterpret_cast<char*>(grass_.data()), static_cast<std::streamsize>(kMapBytes));
    input.read(reinterpret_cast<char*>(foliage_.data()), static_cast<std::streamsize>(kMapBytes));
    if (!input) {
        grass_.clear();
        foliage_.clear();
        return false;
    }
    return true;
}

// The game indexes the map by temperature across and by rainfall scaled by
// temperature down, which is why a hot dry biome lands in the corner.
uint32_t Colormaps::sample(const std::vector<uint8_t>& map, float temperature,
                           float downfall) const {
    if (map.size() < kMapBytes) return 0xFFFFFF;
    const float clamped = std::clamp(temperature, 0.0f, 1.0f);
    const float rain = std::clamp(downfall, 0.0f, 1.0f) * clamped;
    const auto x = static_cast<size_t>((1.0f - clamped) * 255.0f);
    const auto y = static_cast<size_t>((1.0f - rain) * 255.0f);
    const size_t at = (y * kMapSide + x) * 3;
    return (uint32_t{map[at]} << 16) | (uint32_t{map[at + 1]} << 8) | uint32_t{map[at + 2]};
}

uint32_t Colormaps::grass(float temperature, float downfall) const {
    return grass_.empty() ? kDefaultGrass : sample(grass_, temperature, downfall);
}

uint32_t Colormaps::foliage(float temperature, float downfall) const {
    return foliage_.empty() ? kDefaultFoliage : sample(foliage_, temperature, downfall);
}

} // namespace mcpp::world
