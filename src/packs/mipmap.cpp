#include "mcpp/packs/mipmap.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <limits>

namespace mcpp::packs {
namespace {

// `ARGB.srgbToLinearChannel`, `ARGB.linearToSrgbChannel`: the game averages
// texels in linear light through two small tables, so their rounding is part
// of the answer and a float pipeline does not reproduce it.
struct Tables {
    std::array<int32_t, 256> to_linear{};
    std::array<uint8_t, 1024> to_srgb{};
    Tables() {
        for (int i = 0; i < 256; ++i) {
            const double v = i / 255.0;
            const double linear = v >= 0.04045 ? std::pow((v + 0.055) / 1.055, 2.4) : v / 12.92;
            to_linear[static_cast<size_t>(i)] =
                static_cast<int32_t>(std::floor(linear * 1023.0 + 0.5));
        }
        for (int i = 0; i < 1024; ++i) {
            const double v = i / 1023.0;
            const double srgb =
                v >= 0.0031308 ? 1.055 * std::pow(v, 1.0 / 2.4) - 0.055 : 12.92 * v;
            to_srgb[static_cast<size_t>(i)] = static_cast<uint8_t>(std::floor(srgb * 255.0 + 0.5));
        }
    }
};

const Tables& tables() {
    static const Tables built;
    return built;
}

bool any_transparent(const Bitmap& pixels) {
    for (size_t i = 3; i < pixels.rgba.size(); i += 4) {
        if (pixels.rgba[i] == 0) return true;
    }
    return false;
}

bool any_visible(const Bitmap& pixels) {
    for (size_t i = 3; i < pixels.rgba.size(); i += 4) {
        if (pixels.rgba[i] != 0) return true;
    }
    return false;
}

bool is_cutout(MipStrategy strategy) {
    return strategy == MipStrategy::Cutout || strategy == MipStrategy::StrictCutout ||
           strategy == MipStrategy::DarkCutout;
}

} // namespace

MipStrategy mip_strategy_from_name(std::string_view name) {
    if (name == "mean") return MipStrategy::Mean;
    if (name == "cutout") return MipStrategy::Cutout;
    if (name == "strict_cutout") return MipStrategy::StrictCutout;
    if (name == "dark_cutout") return MipStrategy::DarkCutout;
    return MipStrategy::Auto;
}

uint32_t frame_count(const Bitmap& strip) {
    if (strip.width == 0) return 1;
    return std::max<uint32_t>(strip.height / strip.width, 1);
}

// `ARGB.meanLinear`.
Bitmap mean_linear(const Bitmap& source) {
    const Tables& table = tables();
    Bitmap out(source.width / 2, source.height / 2);
    for (uint32_t y = 0; y < out.height; ++y) {
        for (uint32_t x = 0; x < out.width; ++x) {
            const uint8_t* a = source.at(x * 2, y * 2);
            const uint8_t* b = source.at(x * 2 + 1, y * 2);
            const uint8_t* c = source.at(x * 2, y * 2 + 1);
            const uint8_t* d = source.at(x * 2 + 1, y * 2 + 1);
            uint8_t* target = out.at(x, y);
            for (int channel = 0; channel < 3; ++channel) {
                const int32_t total = (table.to_linear[a[channel]] + table.to_linear[b[channel]] +
                                       table.to_linear[c[channel]] + table.to_linear[d[channel]]) /
                                      4;
                target[channel] = table.to_srgb[static_cast<size_t>(total)];
            }
            target[3] = static_cast<uint8_t>((static_cast<int32_t>(a[3]) + b[3] + c[3] + d[3]) / 4);
        }
    }
    return out;
}

// `MipmapGenerator.darkenedAlphaBlend`: leaves average only the texels that
// are there and still divide by four, so a half-empty corner fades out
// instead of smearing.
Bitmap darkened_alpha_blend(const Bitmap& source) {
    const Tables& table = tables();
    Bitmap out(source.width / 2, source.height / 2);
    for (uint32_t y = 0; y < out.height; ++y) {
        for (uint32_t x = 0; x < out.width; ++x) {
            const uint8_t* quads[4] = {source.at(x * 2, y * 2), source.at(x * 2 + 1, y * 2),
                                       source.at(x * 2, y * 2 + 1),
                                       source.at(x * 2 + 1, y * 2 + 1)};
            uint8_t* target = out.at(x, y);
            for (int channel = 0; channel < 4; ++channel) {
                float total = 0.0f;
                for (const uint8_t* quad : quads) {
                    if (quad[3] == 0) continue;
                    total += static_cast<float>(table.to_linear[quad[channel]]) / 1023.0f;
                }
                total /= 4.0f;
                int32_t index = static_cast<int32_t>(std::floor(total * 1023.0f));
                index = std::clamp(index, 0, 1023);
                target[channel] = table.to_srgb[static_cast<size_t>(index)];
            }
        }
    }
    return out;
}

// `TextureUtil.solidify`: every transparent texel takes the colour of the
// nearest texel that is not, breadth first, with alpha left at zero.
Bitmap solidify(const Bitmap& source) {
    const size_t cells = static_cast<size_t>(source.width) * source.height;
    if (cells == 0) return source;
    if (!any_visible(source) || !any_transparent(source)) return source;

    std::vector<std::array<uint8_t, 3>> colour(cells, {0, 0, 0});
    std::vector<int64_t> distance(cells, int64_t{1} << 30);
    std::deque<size_t> queue;
    // The game walks columns first; the same order here breaks the same ties.
    for (uint32_t x = 0; x < source.width; ++x) {
        for (uint32_t y = 0; y < source.height; ++y) {
            const size_t cell = x + static_cast<size_t>(y) * source.width;
            const uint8_t* texel = source.at(x, y);
            if (texel[3] == 0) continue;
            distance[cell] = 0;
            colour[cell] = {texel[0], texel[1], texel[2]};
            queue.push_back(cell);
        }
    }
    constexpr int steps[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    while (!queue.empty()) {
        const size_t cell = queue.front();
        queue.pop_front();
        const int64_t x = static_cast<int64_t>(cell % source.width);
        const int64_t y = static_cast<int64_t>(cell / source.width);
        for (const auto& step : steps) {
            const int64_t nx = x + step[0];
            const int64_t ny = y + step[1];
            if (nx < 0 || ny < 0 || nx >= source.width || ny >= source.height) continue;
            const size_t neighbour = static_cast<size_t>(nx) +
                                     static_cast<size_t>(ny) * source.width;
            if (distance[neighbour] <= distance[cell] + 1) continue;
            distance[neighbour] = distance[cell] + 1;
            colour[neighbour] = colour[cell];
            queue.push_back(neighbour);
        }
    }

    Bitmap out = source;
    for (uint32_t y = 0; y < out.height; ++y) {
        for (uint32_t x = 0; x < out.width; ++x) {
            uint8_t* texel = out.at(x, y);
            if (texel[3] != 0) continue;
            const size_t cell = x + static_cast<size_t>(y) * out.width;
            texel[0] = colour[cell][0];
            texel[1] = colour[cell][1];
            texel[2] = colour[cell][2];
        }
    }
    return out;
}

// `TextureUtil.fillEmptyAreasWithDarkColor`: the darkest texel that is there,
// three quarters as bright, behind every texel that is not.
Bitmap fill_empty_areas_with_dark_colour(const Bitmap& source) {
    if (!any_visible(source)) return source;
    int32_t best = std::numeric_limits<int32_t>::max();
    std::array<uint8_t, 3> darkest{0, 0, 0};
    for (uint32_t x = 0; x < source.width; ++x) {
        for (uint32_t y = 0; y < source.height; ++y) {
            const uint8_t* texel = source.at(x, y);
            if (texel[3] == 0) continue;
            const int32_t sum = texel[0] + texel[1] + texel[2];
            if (sum >= best) continue;
            best = sum;
            darkest = {texel[0], texel[1], texel[2]};
        }
    }
    const std::array<uint8_t, 3> fill{static_cast<uint8_t>(3 * darkest[0] / 4),
                                      static_cast<uint8_t>(3 * darkest[1] / 4),
                                      static_cast<uint8_t>(3 * darkest[2] / 4)};
    Bitmap out = source;
    for (size_t i = 0; i + 3 < out.rgba.size(); i += 4) {
        if (out.rgba[i + 3] != 0) continue;
        out.rgba[i] = fill[0];
        out.rgba[i + 1] = fill[1];
        out.rgba[i + 2] = fill[2];
    }
    return out;
}

// `MipmapGenerator.alphaTestCoverage`: how much of the texture survives the
// cutout once alpha is scaled, sampled on a 4x4 grid inside every square of
// four texels.
float alpha_test_coverage(const Bitmap& source, float threshold, float scale) {
    if (source.height < 2 || source.width < 2) return 0.0f;
    const auto alpha = [&](uint32_t x, uint32_t y) {
        return std::clamp(static_cast<float>(source.at(x, y)[3]) / 255.0f * scale, 0.0f, 1.0f);
    };
    float hits = 0.0f;
    for (uint32_t y = 0; y + 1 < source.height; ++y) {
        for (uint32_t x = 0; x + 1 < source.width; ++x) {
            const float a00 = alpha(x, y);
            const float a10 = alpha(x + 1, y);
            const float a01 = alpha(x, y + 1);
            const float a11 = alpha(x + 1, y + 1);
            float cell = 0.0f;
            for (int sy = 0; sy < 4; ++sy) {
                const float wy = (static_cast<float>(sy) + 0.5f) / 4.0f;
                for (int sx = 0; sx < 4; ++sx) {
                    const float wx = (static_cast<float>(sx) + 0.5f) / 4.0f;
                    const float blended = a00 * (1.0f - wx) * (1.0f - wy) + a10 * wx * (1.0f - wy) +
                                          a01 * (1.0f - wx) * wy + a11 * wx * wy;
                    if (blended > threshold) cell += 1.0f;
                }
            }
            hits += cell / 16.0f;
        }
    }
    return hits / static_cast<float>((source.width - 1) * (source.height - 1));
}

// `MipmapGenerator.scaleAlphaToCoverage`: five bisection steps for the alpha
// scale that keeps as much of the texture as the first level had.
void scale_alpha_to_coverage(Bitmap& level, float target, float threshold, float bias) {
    float low = 0.0f;
    float high = 4.0f;
    float scale = 1.0f;
    float best = 1.0f;
    float best_difference = std::numeric_limits<float>::infinity();
    for (int step = 0; step < 5; ++step) {
        const float coverage = alpha_test_coverage(level, threshold, scale);
        const float difference = std::abs(coverage - target);
        if (difference < best_difference) {
            best_difference = difference;
            best = scale;
        }
        if (coverage < target) {
            low = scale;
        } else if (coverage > target) {
            high = scale;
        } else {
            break;
        }
        scale = (low + high) * 0.5f;
    }
    for (size_t i = 3; i < level.rgba.size(); i += 4) {
        float alpha = static_cast<float>(level.rgba[i]) / 255.0f;
        alpha = std::clamp(alpha * best + bias + 0.025f, 0.0f, 1.0f);
        level.rgba[i] = static_cast<uint8_t>(std::floor(alpha * 255.0f));
    }
}

// `MipmapGenerator.generateMipLevels`: the chain for one sprite, cutouts
// solidified first and every level scaled back to the coverage the first one
// had.
std::vector<Bitmap> generate_mip_levels(const Bitmap& pixels, int max_level,
                                        MipStrategy strategy, float bias) {
    if (strategy == MipStrategy::Auto) {
        strategy = any_transparent(pixels) ? MipStrategy::Cutout : MipStrategy::Mean;
    }
    Bitmap first = pixels;
    if (strategy == MipStrategy::Cutout || strategy == MipStrategy::StrictCutout) {
        first = solidify(first);
    } else if (strategy == MipStrategy::DarkCutout) {
        first = fill_empty_areas_with_dark_colour(first);
    }

    std::vector<Bitmap> levels;
    levels.push_back(std::move(first));
    const bool cutout = is_cutout(strategy);
    const float threshold = strategy == MipStrategy::StrictCutout ? 0.3f : 0.5f;
    const float target = cutout ? alpha_test_coverage(levels.front(), threshold, 1.0f) : 0.0f;

    for (int step = 0; step < max_level; ++step) {
        const Bitmap& previous = levels.back();
        if (previous.height < 2 || previous.width < 2) break;
        Bitmap level = strategy == MipStrategy::DarkCutout ? darkened_alpha_blend(previous)
                                                           : mean_linear(previous);
        if (cutout) scale_alpha_to_coverage(level, target, threshold, bias);
        levels.push_back(std::move(level));
    }
    return levels;
}

Bitmap upscale(const Bitmap& level, uint32_t factor) {
    if (factor <= 1) return level;
    Bitmap out(level.width * factor, level.height * factor);
    for (uint32_t y = 0; y < out.height; ++y) {
        for (uint32_t x = 0; x < out.width; ++x) {
            std::copy_n(level.at(x / factor, y / factor), 4, out.at(x, y));
        }
    }
    return out;
}

Bitmap resample_nearest(const Bitmap& source, uint32_t width, uint32_t height) {
    if (source.empty() || width == 0 || height == 0) return Bitmap(width, height);
    if (source.width == width && source.height == height) return source;
    Bitmap out(width, height);
    for (uint32_t y = 0; y < height; ++y) {
        const uint32_t sy = std::min(y * source.height / std::max(height, 1u), source.height - 1);
        for (uint32_t x = 0; x < width; ++x) {
            const uint32_t sx = std::min(x * source.width / std::max(width, 1u), source.width - 1);
            std::copy_n(source.at(sx, sy), 4, out.at(x, y));
        }
    }
    return out;
}

Bitmap to_power_of_two(const Bitmap& strip) {
    const uint32_t native = strip.width;
    if (native == 0 || (native & (native - 1)) == 0) return strip;
    uint32_t side = 1;
    while (side < native) side <<= 1;
    const uint32_t frames = frame_count(strip);
    Bitmap out(side, side * frames);
    for (uint32_t y = 0; y < out.height; ++y) {
        const uint32_t sy = std::min(y * native / side, native * frames - 1);
        for (uint32_t x = 0; x < side; ++x) {
            const uint32_t sx = std::min(x * native / side, native - 1);
            std::copy_n(strip.at(sx, sy), 4, out.at(x, y));
        }
    }
    return out;
}

// `tools/bake_atlas.py chain_for`.
std::vector<std::vector<Bitmap>> chain_for(const Bitmap& strip, MipStrategy strategy, float bias,
                                           uint32_t tile_size, uint32_t levels_in_atlas) {
    const uint32_t native = strip.width;
    int native_levels = 0;
    while ((1u << native_levels) < native) ++native_levels;
    // `MIP_LEVELS` is four, and a large sprite needs its whole chain anyway.
    const std::vector<Bitmap> chain =
        generate_mip_levels(strip, std::max(4, native_levels), strategy, bias);

    int shift = 0;
    if (native != 0) {
        if (tile_size >= native) {
            while ((native << shift) < tile_size) ++shift;
        } else {
            while ((tile_size << -shift) < native) --shift;
        }
    }
    const uint32_t frames = frame_count(strip);

    std::vector<std::vector<Bitmap>> per_level;
    per_level.reserve(levels_in_atlas);
    for (uint32_t level = 0; level < levels_in_atlas; ++level) {
        const int into = static_cast<int>(level) - shift;
        Bitmap source = into <= 0
                            ? chain.front()
                            : chain[std::min(static_cast<size_t>(into), chain.size() - 1)];
        if (into < 0) source = upscale(source, 1u << -into);
        const uint32_t side = source.width;
        std::vector<Bitmap> level_frames;
        level_frames.reserve(frames);
        for (uint32_t frame = 0; frame < frames; ++frame) {
            Bitmap out(side, side);
            const uint32_t top = frame * side;
            if (top + side <= source.height) {
                std::copy_n(source.at(0, top), static_cast<size_t>(side) * side * 4,
                            out.rgba.data());
            }
            level_frames.push_back(std::move(out));
        }
        per_level.push_back(std::move(level_frames));
    }
    return per_level;
}

} // namespace mcpp::packs
