#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace mcpp::world {

// What a biome contributes to how the world looks. Servers send this with the
// rest of the registries, so a datapack biome is handled the same as a vanilla
// one without the client knowing anything about it beforehand.
struct Biome {
    // The two the game applies on top of the colour map, named the way the
    // biome's own effects name them.
    enum class GrassModifier { None, Swamp, DarkForest };

    std::string name;
    float temperature = 0.5f;
    float downfall = 0.5f;
    bool fixed_grass = false;
    uint32_t grass = 0;
    bool fixed_foliage = false;
    uint32_t foliage = 0;
    uint32_t water = 0x3F76E4;
    GrassModifier grass_modifier = GrassModifier::None;
    // A swamp fixes the colour of dry leaves the way others fix grass.
    bool fixed_dry_foliage = false;
    uint32_t dry_foliage = 0;
    // Rain falls here, rather than nothing or snow; the fog thickens with it.
    bool has_precipitation = true;
};

// The 256x256 maps the game looks grass and foliage colour up in. They are
// Mojang's, so they are read from the player's own installation and never ship
// with the client.
class Colormaps {
public:
    bool open(const std::filesystem::path& path);
    bool loaded() const { return !grass_.empty(); }
    uint32_t grass(float temperature, float downfall) const;
    uint32_t foliage(float temperature, float downfall) const;
    uint32_t dry_foliage(float temperature, float downfall) const;

    static std::filesystem::path default_path(int32_t protocol);

private:
    uint32_t sample(const std::vector<uint8_t>& map, float temperature, float downfall) const;

    std::vector<uint8_t> grass_;     // 256*256*3
    std::vector<uint8_t> foliage_;
    std::vector<uint8_t> dry_;
};

// Biomes in the order the server sent them, which is the order chunk palettes
// index into.
class BiomeTable {
public:
    void clear() { biomes_.clear(); }
    void add(Biome biome) { biomes_.push_back(std::move(biome)); }
    size_t size() const { return biomes_.size(); }
    bool empty() const { return biomes_.empty(); }
    const Biome* at(size_t index) const;

    // 0xRRGGBB, resolved through the colormaps where the biome does not give a
    // colour of its own.
    uint32_t grass(size_t index, const Colormaps& maps) const;
    uint32_t foliage(size_t index, const Colormaps& maps) const;
    uint32_t water(size_t index) const;
    uint32_t dry_foliage(size_t index, const Colormaps& maps) const;
    // The four of them per biome, flattened the way the renderer uploads them.
    std::vector<uint32_t> tints(const Colormaps& maps) const;
    // The game computes this one rather than storing it: a colder biome gets a
    // deeper, bluer sky.
    uint32_t sky(size_t index) const;

private:
    std::vector<Biome> biomes_;
};

} // namespace mcpp::world
