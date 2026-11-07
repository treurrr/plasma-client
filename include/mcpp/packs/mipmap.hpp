#pragma once
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

// The game's own mip chain for one sprite. tools/bake_atlas.py builds this at
// bake time out of the player's jar; a pack arrives mid-session, so the same
// chain has to be buildable here, texel for texel, or a replaced block looks
// right up close and wrong at ten metres.
namespace mcpp::packs {

struct Bitmap {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> rgba;

    Bitmap() = default;
    Bitmap(uint32_t w, uint32_t h) : width(w), height(h), rgba(static_cast<size_t>(w) * h * 4, 0) {}
    bool empty() const { return width == 0 || height == 0; }
    uint8_t* at(uint32_t x, uint32_t y) {
        return rgba.data() + (static_cast<size_t>(y) * width + x) * 4;
    }
    const uint8_t* at(uint32_t x, uint32_t y) const {
        return rgba.data() + (static_cast<size_t>(y) * width + x) * 4;
    }
};

// `TextureMetadataSection.mipmapStrategy`.
enum class MipStrategy { Auto, Mean, Cutout, StrictCutout, DarkCutout };
MipStrategy mip_strategy_from_name(std::string_view name);

// `ARGB.meanLinear`: four texels averaged, alpha as plain integers and colour
// through the game's two rounding tables.
Bitmap mean_linear(const Bitmap& source);
// `MipmapGenerator.darkenedAlphaBlend`.
Bitmap darkened_alpha_blend(const Bitmap& source);
// `TextureUtil.solidify`.
Bitmap solidify(const Bitmap& source);
// `TextureUtil.fillEmptyAreasWithDarkColor`.
Bitmap fill_empty_areas_with_dark_colour(const Bitmap& source);
// `MipmapGenerator.alphaTestCoverage`.
float alpha_test_coverage(const Bitmap& source, float threshold, float scale);
// `MipmapGenerator.scaleAlphaToCoverage`.
void scale_alpha_to_coverage(Bitmap& level, float target, float threshold, float bias);
// `MipmapGenerator.generateMipLevels`.
std::vector<Bitmap> generate_mip_levels(const Bitmap& pixels, int max_level,
                                        MipStrategy strategy, float bias);

Bitmap upscale(const Bitmap& level, uint32_t factor);
// A sprite whose side is not a power of two, resampled up to the next one.
Bitmap to_power_of_two(const Bitmap& strip);
// Nearest-texel resample of one square frame, which is what the item baker
// does to a sprite that is not the icon's size.
Bitmap resample_nearest(const Bitmap& source, uint32_t width, uint32_t height);

// A vertical strip's frame count: its height over its width, at least one.
uint32_t frame_count(const Bitmap& strip);

// `tools/bake_atlas.py chain_for`: one frame per level per layer, for a strip
// of any native size against an atlas of this tile size. A sprite smaller than
// the tile is repeated up to it, which leaves every level below unchanged; a
// sprite larger than the tile starts further down its own chain, which is the
// same texels the game would show if its atlas were this size.
std::vector<std::vector<Bitmap>> chain_for(const Bitmap& strip, MipStrategy strategy, float bias,
                                           uint32_t tile_size, uint32_t levels_in_atlas);

} // namespace mcpp::packs
