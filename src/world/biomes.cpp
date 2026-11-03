#include "mcpp/world/biomes.hpp"

#include "mcpp/image_png.hpp"

#include <algorithm>
#include <cmath>
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
    const uint32_t base =
        biome->fixed_grass ? biome->grass : maps.grass(biome->temperature, biome->downfall);
    switch (biome->grass_modifier) {
    // A swamp ignores its climate entirely and a dark forest is dragged half
    // way towards a brown, which is what makes either one recognisable.
    case Biome::GrassModifier::Swamp: return 0x6A7039u;
    case Biome::GrassModifier::DarkForest: return ((base & 0xFEFEFEu) + 0x28340Au) >> 1;
    case Biome::GrassModifier::None: break;
    }
    return base;
}

uint32_t BiomeTable::foliage(size_t index, const Colormaps& maps) const {
    const Biome* biome = at(index);
    if (biome == nullptr) return kDefaultFoliage;
    if (biome->fixed_foliage) return biome->foliage;
    return maps.foliage(biome->temperature, biome->downfall);
}

namespace {

uint32_t from_hsv(float hue, float saturation, float value) {
    const float sector = std::fmod(hue * 6.0f, 6.0f);
    const int step = static_cast<int>(sector);
    const float fraction = sector - static_cast<float>(step);
    const float p = value * (1.0f - saturation);
    const float q = value * (1.0f - saturation * fraction);
    const float u = value * (1.0f - saturation * (1.0f - fraction));
    float rgb[3]{};
    switch (step) {
    case 0: rgb[0] = value; rgb[1] = u; rgb[2] = p; break;
    case 1: rgb[0] = q; rgb[1] = value; rgb[2] = p; break;
    case 2: rgb[0] = p; rgb[1] = value; rgb[2] = u; break;
    case 3: rgb[0] = p; rgb[1] = q; rgb[2] = value; break;
    case 4: rgb[0] = u; rgb[1] = p; rgb[2] = value; break;
    default: rgb[0] = value; rgb[1] = p; rgb[2] = q; break;
    }
    uint32_t out = 0;
    for (const float channel : rgb) {
        out = (out << 8) | static_cast<uint32_t>(std::lround(std::clamp(channel, 0.0f, 1.0f) *
                                                             255.0f));
    }
    return out;
}

} // namespace

uint32_t BiomeTable::sky(size_t index) const {
    const Biome* biome = at(index);
    const float temperature = biome != nullptr ? biome->temperature : 0.8f;
    const float shade = std::clamp(temperature / 3.0f, -1.0f, 1.0f);
    return from_hsv(0.62222222f - shade * 0.05f, 0.5f + shade * 0.1f, 1.0f);
}

uint32_t BiomeTable::dry_foliage(size_t index, const Colormaps& maps) const {
    const Biome* biome = at(index);
    if (biome == nullptr) return 0x8F7A36u;
    return maps.dry_foliage(biome->temperature, biome->downfall);
}

std::vector<uint32_t> BiomeTable::tints(const Colormaps& maps) const {
    std::vector<uint32_t> table(biomes_.size() * 4);
    for (size_t i = 0; i < biomes_.size(); ++i) {
        table[i * 4 + 0] = grass(i, maps);
        table[i * 4 + 1] = foliage(i, maps);
        table[i * 4 + 2] = water(i);
        table[i * 4 + 3] = dry_foliage(i, maps);
    }
    return table;
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
    dry_.resize(kMapBytes);
    input.read(reinterpret_cast<char*>(grass_.data()), static_cast<std::streamsize>(kMapBytes));
    input.read(reinterpret_cast<char*>(foliage_.data()), static_cast<std::streamsize>(kMapBytes));
    input.read(reinterpret_cast<char*>(dry_.data()), static_cast<std::streamsize>(kMapBytes));
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

uint32_t Colormaps::dry_foliage(float temperature, float downfall) const {
    return dry_.empty() ? kDefaultFoliage : sample(dry_, temperature, downfall);
}

} // namespace mcpp::world
