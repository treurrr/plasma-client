#include "mcpp/entity/dye.hpp"

#include <array>
#include <cmath>

namespace mcpp::entity {
namespace {

// `DyeColor`: the third argument of each value, its texture diffuse colour,
// in the enum's own order.
constexpr std::array<uint32_t, 16> kDiffuse = {
    16383998, 16351261, 13061821, 3847130, 16701501, 8439583, 15961002, 4673362,
    10329495, 1481884,  8991416,  3949738, 8606770,  6192150, 11546150, 1908001,
};

// `ColorLerper.Type`: how long one dye of the cycle lasts, in ticks, and the
// factor `getModifiedColor` dims it by.
struct Cycle {
    int32_t duration;
    float factor;
    uint8_t count;
};
constexpr std::array<Cycle, 2> kCycles = {{{25, 0.75f, 16}, {30, 1.25f, 12}}};

// `ColorLerper.MUSIC_NOTE_COLORS`: twelve of the sixteen dyes, in their own
// order, by dye id.
constexpr std::array<int8_t, 12> kMusicNote = {0, 8, 3, 11, 9, 13, 5, 4, 1, 6, 14, 15};

int8_t cycle_dye(ColourCycle cycle, uint8_t at) {
    if (cycle == ColourCycle::MusicNote) return kMusicNote[at];
    return static_cast<int8_t>(at);
}

uint32_t alpha(uint32_t c) { return c >> 24; }
uint32_t red(uint32_t c) { return (c >> 16) & 0xFF; }
uint32_t green(uint32_t c) { return (c >> 8) & 0xFF; }
uint32_t blue(uint32_t c) { return c & 0xFF; }

// `ARGB.color`.
uint32_t argb(uint32_t a, uint32_t r, uint32_t g, uint32_t b) {
    return (a << 24) | (r << 16) | (g << 8) | b;
}

// `Mth.lerpInt`.
int32_t lerp_int(float t, int32_t from, int32_t to) {
    return from + static_cast<int32_t>(std::floor(t * static_cast<float>(to - from)));
}

// `ARGB.srgbLerp`.
uint32_t srgb_lerp(float t, uint32_t from, uint32_t to) {
    return argb(static_cast<uint32_t>(lerp_int(t, static_cast<int32_t>(alpha(from)),
                                               static_cast<int32_t>(alpha(to)))),
                static_cast<uint32_t>(lerp_int(t, static_cast<int32_t>(red(from)),
                                               static_cast<int32_t>(red(to)))),
                static_cast<uint32_t>(lerp_int(t, static_cast<int32_t>(green(from)),
                                               static_cast<int32_t>(green(to)))),
                static_cast<uint32_t>(lerp_int(t, static_cast<int32_t>(blue(from)),
                                               static_cast<int32_t>(blue(to)))));
}

} // namespace

uint32_t dye_texture_colour(int8_t id) {
    // `DyeColor.byId`: out of range is white.
    const size_t at = id >= 0 && id < 16 ? static_cast<size_t>(id) : 0;
    return argb_opaque(kDiffuse[at]);
}

// `ColorLerper.getModifiedColor`: white is its own fixed shade, the rest are
// the dye's own colour scaled.
uint32_t cycle_colour(ColourCycle cycle, int8_t dye) {
    if (dye == 0) return 0xFFE7E7E6u;   // -1644826
    const float factor = kCycles[static_cast<size_t>(cycle)].factor;
    const uint32_t c = dye_texture_colour(dye);
    const auto channel = [factor](uint32_t value) {
        return static_cast<uint32_t>(std::floor(static_cast<float>(value) * factor));
    };
    return argb(255, channel(red(c)), channel(green(c)), channel(blue(c)));
}

// `ColorLerper.getLerpedColor`: one dye of the cycle into the next, a quarter
// of a second apart.
uint32_t lerped_colour(ColourCycle cycle, float age_in_ticks) {
    const Cycle& type = kCycles[static_cast<size_t>(cycle)];
    const int32_t ticks = static_cast<int32_t>(std::floor(age_in_ticks));
    const int32_t step = ticks / type.duration;
    const int32_t count = type.count;
    const int32_t at = step % count;
    const int32_t next = (step + 1) % count;
    const float frac = age_in_ticks - std::floor(age_in_ticks);
    const float t = (static_cast<float>(ticks % type.duration) + frac) /
                    static_cast<float>(type.duration);
    return srgb_lerp(t, cycle_colour(cycle, cycle_dye(cycle, static_cast<uint8_t>(at))),
                     cycle_colour(cycle, cycle_dye(cycle, static_cast<uint8_t>(next))));
}

// `ARGB.multiply`: white on either side is the other one untouched.
uint32_t argb_multiply(uint32_t a, uint32_t b) {
    if (a == 0xFFFFFFFFu) return b;
    if (b == 0xFFFFFFFFu) return a;
    return argb(alpha(a) * alpha(b) / 255, red(a) * red(b) / 255, green(a) * green(b) / 255,
                blue(a) * blue(b) / 255);
}

} // namespace mcpp::entity
