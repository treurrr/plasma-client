#pragma once
#include <cstdint>

// The colours the game multiplies into a model: a dye's own, the rainbow a
// sheep called "jeb_" wears, and how two of them combine.
namespace mcpp::entity {

// `DyeColor.getTextureDiffuseColor`: the sixteen dyes, by their own id,
// opaque. Out of range gives white's, as `DyeColor.byId` does.
uint32_t dye_texture_colour(int8_t id);

// `ColorLerper.Type.getColor`: the two cycles the game lerps a dye through.
enum class ColourCycle : uint8_t { Sheep, MusicNote };

// `ColorLerper.getLerpedColor`: where in the cycle this many ticks lands.
uint32_t lerped_colour(ColourCycle cycle, float age_in_ticks);
// `ColorLerper.getModifiedColor`: one dye dimmed by the cycle's own factor.
uint32_t cycle_colour(ColourCycle cycle, int8_t dye);

// `ARGB.multiply`.
uint32_t argb_multiply(uint32_t a, uint32_t b);
// `ARGB.opaque`.
inline uint32_t argb_opaque(uint32_t rgb) { return rgb | 0xFF000000u; }

} // namespace mcpp::entity
