#include "mcpp/world/mesher.hpp"

#include "mcpp/jmath.hpp"
#include "mcpp/world/render_table.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>
#include <numbers>
#include <string_view>

namespace mcpp::world {
namespace {

constexpr float kBlock = static_cast<float>(kUnitsPerBlock);
constexpr int kSection = 16;

// Two blocks of padding on every side: ambient occlusion reads diagonals, and
// the test for whether a corner's diagonal can be seen at all reaches two
// blocks out along the face's own direction. With it the loop never tests a
// bound or crosses into another chunk again.
constexpr int kPad = 2;
constexpr int kPadded = kSection + 2 * kPad;
constexpr size_t kPaddedCells = kPadded * kPadded * kPadded;

constexpr size_t padded_index(int x, int y, int z) {
    return static_cast<size_t>((y + kPad) * kPadded * kPadded + (z + kPad) * kPadded +
                               (x + kPad));
}

struct Offset {
    int x, y, z;
};

// Index order matches the baked face ids: down, up, north, south, west, east.
constexpr Offset kFaceOffset[6] = {
    {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0},
};

// Corner order is counter-clockwise seen from outside the box.
constexpr int kCorner[6][4][3] = {
    {{0, 0, 1}, {1, 0, 1}, {1, 0, 0}, {0, 0, 0}},   // down
    {{0, 1, 0}, {1, 1, 0}, {1, 1, 1}, {0, 1, 1}},   // up
    {{1, 1, 0}, {0, 1, 0}, {0, 0, 0}, {1, 0, 0}},   // north
    {{0, 1, 1}, {1, 1, 1}, {1, 0, 1}, {0, 0, 1}},   // south
    {{0, 1, 0}, {0, 1, 1}, {0, 0, 1}, {0, 0, 0}},   // west
    {{1, 1, 1}, {1, 1, 0}, {1, 0, 0}, {1, 0, 1}},   // east
};

constexpr float kCornerUv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};

// The two axes that vary across each face; the third is the face's own normal.
constexpr int kFaceAxes[6][2] = {{0, 2}, {0, 2}, {0, 1}, {0, 1}, {1, 2}, {1, 2}};

// `ClientLevel.getShade`: how dark a face is for pointing the way it does.
constexpr float kFaceShade[6] = {0.5f, 1.0f, 0.8f, 0.8f, 0.6f, 0.6f};

// `ModelBlockRenderer.AdjacencyInfo.AdjacencyInfo`: the four neighbours in
// the plane of each face, in the order the corner blending expects them.
constexpr int kAdjacent[6][4] = {
    {4, 5, 2, 3},   // down:  west, east, north, south
    {5, 4, 2, 3},   // up
    {1, 0, 5, 4},   // north: up, down, east, west
    {4, 5, 0, 1},   // south
    {1, 0, 2, 3},   // west
    {0, 1, 2, 3},   // east
};

// `ModelBlockRenderer.SizeInfo.SizeInfo`: where each extent of the quad sits
// in the shape array, the second six being one minus the first six.
enum : int { kSizeDown, kSizeUp, kSizeNorth, kSizeSouth, kSizeWest, kSizeEast };
constexpr int kSizeCount = 12;

// `ModelBlockRenderer.AdjacencyInfo.AdjacencyInfo`: for each face, for each of
// the four blended corners, the four pairs of extents whose product weights
// that corner into this vertex.
constexpr int kCornerWeights[6][4][8] = {
    {{10, 3, 10, 9, 4, 9, 4, 3}, {10, 2, 10, 8, 4, 8, 4, 2},
     {11, 2, 11, 8, 5, 8, 5, 2}, {11, 3, 11, 9, 5, 9, 5, 3}},   // down
    {{5, 3, 5, 9, 11, 9, 11, 3}, {5, 2, 5, 8, 11, 8, 11, 2},
     {4, 2, 4, 8, 10, 8, 10, 2}, {4, 3, 4, 9, 10, 9, 10, 3}},   // up
    {{1, 10, 1, 4, 7, 4, 7, 10}, {1, 11, 1, 5, 7, 5, 7, 11},
     {0, 11, 0, 5, 6, 5, 6, 11}, {0, 10, 0, 4, 6, 4, 6, 10}},   // north
    {{1, 10, 7, 10, 7, 4, 1, 4}, {0, 10, 6, 10, 6, 4, 0, 4},
     {0, 11, 6, 11, 6, 5, 0, 5}, {1, 11, 7, 11, 7, 5, 1, 5}},   // south
    {{1, 3, 1, 9, 7, 9, 7, 3}, {1, 2, 1, 8, 7, 8, 7, 2},
     {0, 2, 0, 8, 6, 8, 6, 2}, {0, 3, 0, 9, 6, 9, 6, 3}},       // west
    {{6, 3, 6, 9, 0, 9, 0, 3}, {6, 2, 6, 8, 0, 8, 0, 2},
     {7, 2, 7, 8, 1, 8, 1, 2}, {7, 3, 7, 9, 1, 9, 1, 3}},       // east
};

// `ModelBlockRenderer.AmbientVertexRemap.AmbientVertexRemap`, read backwards:
// which of the four blended corners a quad corner is, by the side it lies on
// along the face's two axes.
constexpr int kCornerOrder[6][4] = {
    {1, 0, 2, 3},   // down
    {2, 3, 1, 0},   // up
    {3, 0, 2, 1},   // north
    {1, 0, 2, 3},   // south
    {2, 3, 1, 0},   // west
    {1, 0, 2, 3},   // east
};

// `LightTexture.pack`: block light in bits 4..7, sky light in bits 20..23.
constexpr uint32_t pack_light(uint32_t block, uint32_t sky) {
    return (block << 4) | (sky << 20);
}
constexpr uint32_t kFullBright = pack_light(15, 15);

// `ModelBlockRenderer.AmbientOcclusionRenderStorage.blend`: four packed lights
// averaged, where a corner that is dark because it is inside a block takes the
// light of the face itself instead.
constexpr uint32_t blend_light(uint32_t a, uint32_t b, uint32_t c, uint32_t own) {
    if (a == 0) a = own;
    if (b == 0) b = own;
    if (c == 0) c = own;
    return ((a + b + c + own) >> 2) & 0x00FF00FFu;
}

// The same four, weighted, for a face that does not span its whole side.
inline uint32_t blend_light(const uint32_t light[4], const float weight[4]) {
    const auto channel = [&](int shift) {
        float total = 0.0f;
        for (int i = 0; i < 4; ++i) {
            total += static_cast<float>((light[i] >> shift) & 0xFFu) * weight[i];
        }
        return static_cast<uint32_t>(static_cast<int>(total)) & 0xFFu;
    };
    return (channel(16) << 16) | channel(0);
}

// A quarter turn about the block centre, in the same order the assets use.
void rotate_quarter_turns(float& a, float& b, int turns) {
    for (int i = 0; i < turns; ++i) {
        const float old_a = a;
        a = b;
        b = -old_a;
    }
}

void apply_rotation(float position[3], uint8_t rotation_x, uint8_t rotation_y) {
    if (rotation_x == 0 && rotation_y == 0) return;
    float x = position[0] - 0.5f;
    float y = position[1] - 0.5f;
    float z = position[2] - 0.5f;
    if (rotation_x) rotate_quarter_turns(y, z, rotation_x);
    if (rotation_y) rotate_quarter_turns(z, x, rotation_y);
    position[0] = x + 0.5f;
    position[1] = y + 0.5f;
    position[2] = z + 0.5f;
}

// An element can be turned about one axis by 22.5 or 45 degrees. `rescale`
// widens it across that axis so a cross still spans the whole block.
void apply_element_rotation(float position[3], const BakedQuad& quad) {
    if (quad.element_eighths == 0) return;

    const float angle = static_cast<float>(quad.element_eighths) * 22.5f *
                        std::numbers::pi_v<float> / 180.0f;
    const float cosine = std::cos(angle);
    const float sine = std::sin(angle);
    const int axis = quad.element_axis % 3;
    const int first = (axis + 1) % 3;
    const int second = (axis + 2) % 3;

    float local[3];
    for (int i = 0; i < 3; ++i) {
        local[i] = position[i] - static_cast<float>(quad.element_origin[i]) /
                                    static_cast<float>(kUnitsPerBlock);
    }
    float a = local[first] * cosine - local[second] * sine;
    float b = local[first] * sine + local[second] * cosine;
    if ((quad.flags & kQuadRescale) != 0 && cosine != 0.0f) {
        a /= cosine;
        b /= cosine;
    }
    local[first] = a;
    local[second] = b;
    for (int i = 0; i < 3; ++i) {
        position[i] = local[i] + static_cast<float>(quad.element_origin[i]) /
                                     static_cast<float>(kUnitsPerBlock);
    }
}

int rotate_face(int face, uint8_t rotation_x, uint8_t rotation_y) {
    if (rotation_x == 0 && rotation_y == 0) return face;
    Offset offset = kFaceOffset[face];
    float x = static_cast<float>(offset.x);
    float y = static_cast<float>(offset.y);
    float z = static_cast<float>(offset.z);
    if (rotation_x) rotate_quarter_turns(y, z, rotation_x);
    if (rotation_y) rotate_quarter_turns(z, x, rotation_y);

    for (int i = 0; i < 6; ++i) {
        if (std::lround(x) == kFaceOffset[i].x && std::lround(y) == kFaceOffset[i].y &&
            std::lround(z) == kFaceOffset[i].z) {
            return i;
        }
    }
    return face;
}

// The game's own table of how each state is lit and what fluid it holds,
// loaded once from what tools/bake_block_render.py asked the game for.
const RenderTable& render_table() {
    static const RenderTable table = [] {
        RenderTable loaded;
        loaded.open(RenderTable::default_path(774));
        return loaded;
    }();
    return table;
}

struct FluidTextures {
    int32_t water_still = -1;
    int32_t water_flow = -1;
    int32_t water_overlay = -1;
    int32_t lava_still = -1;
    int32_t lava_flow = -1;
};

FluidTextures fluid_textures(const BlockTable& table) {
    static const BlockTable* cached_for = nullptr;
    static FluidTextures cached;
    static std::mutex guard;
    std::lock_guard lock(guard);
    if (cached_for == &table) return cached;
    FluidTextures found;
    for (uint16_t i = 0; i < 4096; ++i) {
        const std::string_view name = table.texture_name(i);
        if (name.empty()) break;
        if (name == "minecraft:block/water_still") found.water_still = i;
        if (name == "minecraft:block/water_flow") found.water_flow = i;
        if (name == "minecraft:block/water_overlay") found.water_overlay = i;
        if (name == "minecraft:block/lava_still") found.lava_still = i;
        if (name == "minecraft:block/lava_flow") found.lava_flow = i;
    }
    cached = found;
    cached_for = &table;
    return cached;
}

// One liquid corner as the game's fluid renderer places it, relative to the
// block's own corner.
struct LiquidVertex {
    float x, y, z, u, v;
};

// `LiquidBlockRenderer.tesselate`: one liquid block's faces.
// LiquidBlockRenderer, step for step: corner heights averaged over the
// neighbours, the still or flowing texture turned along the flow, sides cut
// at each corner's height, and the light of the block or the one above,
// whichever is brighter.
template <typename StateAt, typename LightAt, typename Emit>
void mesh_liquid(int32_t state, const FluidTextures& textures, StateAt&& state_at, LightAt&& light_at,
                 Emit&& emit) {
    const RenderTable& render = render_table();
    const FluidState self = render.fluid(state);
    const Fluid kind = self.fluid;
    const bool lava = kind == Fluid::Lava;
    const auto fluid_at = [&](int dx, int dy, int dz) { return render.fluid(state_at(dx, dy, dz)); };
    const auto same = [&](int dx, int dy, int dz) { return fluid_at(dx, dy, dz).fluid == kind; };
    const auto info_at = [&](int dx, int dy, int dz) -> const RenderInfo& {
        return render.at(state_at(dx, dy, dz));
    };
    // Whether a full-block occluder on that side hides a face of the given height.
    const auto occluded_by = [&](int dx, int dy, int dz, bool up, float height) {
        const int32_t neighbour = state_at(dx, dy, dz);
        if (neighbour < 0 || !render.at(neighbour).has(RenderInfo::kSolidRender)) return false;
        return !up || height == 1.0f;
    };
    const bool self_full = render.at(state).has(RenderInfo::kSolidRender);
    // `LiquidBlockRenderer.shouldRenderFace`: not hidden by the block itself and not the same fluid.
    const auto should_render = [&](int dx, int dy, int dz) { return !self_full && !same(dx, dy, dz); };

    const bool up = !same(0, 1, 0);
    const bool down = should_render(0, -1, 0) && !occluded_by(0, -1, 0, false, 0.8888889f);
    const bool north = should_render(0, 0, -1);
    const bool south = should_render(0, 0, 1);
    const bool west = should_render(-1, 0, 0);
    const bool east = should_render(1, 0, 0);
    if (!up && !down && !north && !south && !west && !east) return;

    // `LiquidBlockRenderer.getHeight`: a neighbour's surface, 1 under the same fluid, -1 for a solid block.
    const auto height = [&](int dx, int dz) -> float {
        if (same(dx, 0, dz)) return same(dx, 1, dz) ? 1.0f : fluid_at(dx, 0, dz).own_height();
        return info_at(dx, 0, dz).has(RenderInfo::kCollisionFull) ? -1.0f : 0.0f;
    };
    // `LiquidBlockRenderer.addWeightedHeight`: high surfaces count ten times.
    const auto weighted = [](float sum[2], float h) {
        if (h >= 0.8f) {
            sum[0] += h * 10.0f;
            sum[1] += 10.0f;
        } else if (h >= 0.0f) {
            sum[0] += h;
            sum[1] += 1.0f;
        }
    };
    // `LiquidBlockRenderer.calculateAverageHeight`: one corner from its three neighbours and the diagonal.
    const auto average = [&](float own, float a, float b, int dx, int dz) {
        if (b >= 1.0f || a >= 1.0f) return 1.0f;
        float sum[2] = {0.0f, 0.0f};
        if (b > 0.0f || a > 0.0f) {
            const float corner = height(dx, dz);
            if (corner >= 1.0f) return 1.0f;
            weighted(sum, corner);
        }
        weighted(sum, own);
        weighted(sum, b);
        weighted(sum, a);
        return sum[0] / sum[1];
    };
    const float own = height(0, 0);
    float ne = 1.0f, nw = 1.0f, se = 1.0f, sw = 1.0f;
    if (own < 1.0f) {
        const float hn = height(0, -1);
        const float hs = height(0, 1);
        const float he = height(1, 0);
        const float hw = height(-1, 0);
        ne = average(own, hn, he, 1, -1);
        nw = average(own, hn, hw, -1, -1);
        se = average(own, hs, he, 1, 1);
        sw = average(own, hs, hw, -1, 1);
    }

    const uint8_t here = light_at(0, 0, 0);
    const uint8_t above = light_at(0, 1, 0);
    // `LiquidBlockRenderer.getLightColor`: the brighter of each light, here and above.
    const auto brighter = [](uint8_t a, uint8_t b) {
        return static_cast<uint8_t>(std::max(a & 0x0F, b & 0x0F) | std::max(a & 0xF0, b & 0xF0));
    };
    const uint8_t light = brighter(here, above);
    const int32_t still = lava ? textures.lava_still : textures.water_still;
    const int32_t flowing = lava ? textures.lava_flow : textures.water_flow;
    const float bottom = down ? 0.001f : 0.0f;

    if (up && !occluded_by(0, 1, 0, true, std::min(std::min(nw, sw), std::min(se, ne)))) {
        nw -= 0.001f;
        sw -= 0.001f;
        se -= 0.001f;
        ne -= 0.001f;
        // `FlowingFluid.getFlow`: the flow vector.
        // FlowingFluid.getFlow: the push towards lower neighbours.
        double fx = 0.0;
        double fz = 0.0;
        constexpr int kSides[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
        for (const auto& side : kSides) {
            const FluidState next = fluid_at(side[0], 0, side[1]);
            if (next.fluid != Fluid::None && next.fluid != kind) continue;
            float h = next.fluid == kind ? next.own_height() : 0.0f;
            float push = 0.0f;
            if (h == 0.0f) {
                if (!info_at(side[0], 0, side[1]).has(RenderInfo::kCollisionFull)) {
                    const FluidState below = fluid_at(side[0], -1, side[1]);
                    if (below.fluid == Fluid::None || below.fluid == kind) {
                        h = below.fluid == kind ? below.own_height() : 0.0f;
                        if (h > 0.0f) push = self.own_height() - (h - 0.8888889f);
                    }
                }
            } else if (h > 0.0f) {
                push = self.own_height() - h;
            }
            if (push != 0.0f) {
                fx += side[0] * static_cast<double>(push);
                fz += side[1] * static_cast<double>(push);
            }
        }
        LiquidVertex top[4];
        int32_t top_texture = still;
        if (std::abs(fx) < 1.0e-12 && std::abs(fz) < 1.0e-12) {
            top[0] = {0, nw, 0, 0, 0};
            top[1] = {0, sw, 1, 0, 1};
            top[2] = {1, se, 1, 1, 1};
            top[3] = {1, ne, 0, 1, 0};
            emit(top, still, 1, light, !lava);
        } else {
            const float angle = static_cast<float>(std::atan2(fz, fx)) - static_cast<float>(std::numbers::pi / 2.0);
            const float s = jmath::sin(angle) * 0.25f;
            const float c = jmath::cos(angle) * 0.25f;
            // Halves of the 32-texel flow sprite, as the game's getU(0.5 + ..) reads it.
            top[0] = {0, nw, 0, 0.5f + (-c - s), 0.5f + (-c + s)};
            top[1] = {0, sw, 1, 0.5f + (-c + s), 0.5f + (c + s)};
            top[2] = {1, se, 1, 0.5f + (c + s), 0.5f + (c - s)};
            top[3] = {1, ne, 0, 0.5f + (c - s), 0.5f + (-c - s)};
            top_texture = flowing;
            emit(top, flowing, 1, light, !lava);
        }
        // `FluidState.shouldRenderBackwardUpFace`: an open or foreign cell around the one above.
        bool backward = false;
        for (int dx = -1; dx <= 1 && !backward; ++dx) {
            for (int dz = -1; dz <= 1; ++dz) {
                if (!same(dx, 1, dz) && !info_at(dx, 1, dz).has(RenderInfo::kSolidRender)) {
                    backward = true;
                    break;
                }
            }
        }
        if (backward) {
            const LiquidVertex reversed[4] = {top[0], top[3], top[2], top[1]};
            emit(reversed, top_texture, 1, light, !lava);
        }
    }

    if (down) {
        const LiquidVertex floor[4] = {{0, bottom, 1, 0, 1}, {0, bottom, 0, 0, 0},
                                       {1, bottom, 0, 1, 0}, {1, bottom, 1, 1, 1}};
        emit(floor, still, 0, brighter(light_at(0, -1, 0), here), !lava);
    }

    struct Side {
        int dx, dz;
        bool render;
        int face;
    };
    const Side sides[4] = {{0, -1, north, 2}, {0, 1, south, 3}, {-1, 0, west, 4}, {1, 0, east, 5}};
    for (const auto& side : sides) {
        if (!side.render) continue;
        float h0, h1, x0, x1, z0, z1;
        switch (side.face) {
        case 2: h0 = nw; h1 = ne; x0 = 0; x1 = 1; z0 = z1 = 0.001f; break;
        case 3: h0 = se; h1 = sw; x0 = 1; x1 = 0; z0 = z1 = 1.0f - 0.001f; break;
        case 4: h0 = sw; h1 = nw; x0 = x1 = 0.001f; z0 = 1; z1 = 0; break;
        default: h0 = ne; h1 = se; x0 = x1 = 1.0f - 0.001f; z0 = 0; z1 = 1; break;
        }
        if (occluded_by(side.dx, 0, side.dz, false, std::max(h0, h1))) continue;
        int32_t texture = flowing;
        bool overlay = false;
        if (!lava) {
            const int32_t neighbour = state_at(side.dx, 0, side.dz);
            if (neighbour > 0 && render.at(neighbour).has(RenderInfo::kLeaves | RenderInfo::kHalfTransparent)) {
                texture = textures.water_overlay;
                overlay = true;
            }
        }
        const float v0 = (1.0f - h0) * 0.5f;
        const float v1 = (1.0f - h1) * 0.5f;
        const LiquidVertex wall[4] = {{x0, h0, z0, 0.0f, v0}, {x1, h1, z1, 0.5f, v1},
                                      {x1, bottom, z1, 0.5f, 0.5f}, {x0, bottom, z0, 0.0f, 0.5f}};
        emit(wall, texture, side.face, light, !lava);
        if (!overlay) {
            const LiquidVertex back[4] = {wall[3], wall[2], wall[1], wall[0]};
            emit(back, texture, side.face, light, !lava);
        }
    }
}

} // namespace

ChunkBlocks::ChunkBlocks(const Chunk& chunk) : sections_(chunk.sections.size()) {
    for (size_t i = 0; i < chunk.sections.size(); ++i) {
        // Most of a column is air; leaving those sections empty saves both the
        // unpacking and the memory.
        if (chunk.sections[i].non_air_count == 0) continue;

        const auto& blocks = chunk.sections[i].blocks;
        auto& flat = sections_[i];
        flat.resize(kBlocksPerSection);
        for (size_t at = 0; at < kBlocksPerSection; ++at) flat[at] = blocks.at(at);
    }

    // Light is one byte per block for the whole column: a nibble lookup per
    // read would otherwise happen 27 times per block while meshing.
    biomes_.resize(chunk.sections.size() * kBiomesPerSection);
    for (size_t i = 0; i < chunk.sections.size(); ++i) {
        const auto& biomes = chunk.sections[i].biomes;
        for (size_t at = 0; at < kBiomesPerSection; ++at) {
            biomes_[i * kBiomesPerSection + at] =
                static_cast<uint16_t>(std::max<int32_t>(0, biomes.at(at)));
        }
    }

    light_.resize(chunk.sections.size() * kBlocksPerSection);
    for (size_t i = 0; i < chunk.sections.size(); ++i) {
        for (int32_t y = 0; y < 16; ++y) {
            for (int32_t z = 0; z < 16; ++z) {
                for (int32_t x = 0; x < 16; ++x) {
                    const int32_t column_y = static_cast<int32_t>(i) * 16 + y;
                    light_[i * kBlocksPerSection + static_cast<size_t>(y) * 256 +
                           static_cast<size_t>(z) * 16 + static_cast<size_t>(x)] =
                        static_cast<uint8_t>(chunk.sky_light_at(x, column_y, z) |
                                             (chunk.block_light_at(x, column_y, z) << 4));
                }
            }
        }
    }
}

uint16_t ChunkBlocks::biome_at(int32_t x, int32_t y, int32_t z) const {
    if (x < 0 || x > 15 || z < 0 || z > 15 || y < 0) return 0;
    const auto section = static_cast<size_t>(y) / 16;
    if (section >= sections_.size()) return 0;
    return biomes_[section * kBiomesPerSection + static_cast<size_t>((y % 16) / 4) * 16 +
                   static_cast<size_t>(z / 4) * 4 + static_cast<size_t>(x / 4)];
}

uint8_t ChunkBlocks::light_at(int32_t x, int32_t y, int32_t z) const {
    if (x < 0 || x > 15 || z < 0 || z > 15 || y < 0) return 0x0F;
    const auto section = static_cast<size_t>(y) / 16;
    if (section >= sections_.size()) return 0x0F;
    return light_[section * kBlocksPerSection + static_cast<size_t>(y % 16) * 256 +
                  static_cast<size_t>(z) * 16 + static_cast<size_t>(x)];
}

int32_t ChunkBlocks::at(int32_t x, int32_t y, int32_t z) const {
    if (y < 0) return -1;
    const auto section = static_cast<size_t>(y / kSection);
    if (section >= sections_.size()) return -1;
    const auto& flat = sections_[section];
    if (flat.empty()) return 0;
    return flat[static_cast<size_t>(y % kSection) * 256 + static_cast<size_t>(z) * 16 +
                static_cast<size_t>(x)];
}

std::vector<SectionMesh> mesh_sections(const Chunk& chunk, const ChunkBlocks& blocks,
                                       const BlockTable& table, const Neighbours& neighbours,
                                       const BiomeTints& tints) {
    std::vector<SectionMesh> meshes;
    const auto section_count = static_cast<int32_t>(blocks.section_count());
    const RenderTable& render = render_table();

    std::vector<int32_t> padded_state(kPaddedCells);
    std::vector<uint8_t> padded_solid(kPaddedCells);
    std::vector<uint8_t> padded_light(kPaddedCells);
    // What ambient occlusion and smooth lighting read, once per cell instead
    // of once per corner of every quad that touches it.
    std::vector<uint32_t> padded_packed(kPaddedCells);
    std::vector<float> padded_shade(kPaddedCells);
    std::vector<uint8_t> padded_open(kPaddedCells);
    std::vector<uint8_t> padded_full(kPaddedCells);

    const auto state_at = [&](int32_t x, int32_t y, int32_t z) -> int32_t {
        const bool past_x = x < 0 || x > 15;
        const bool past_z = z < 0 || z > 15;

        // Ambient occlusion also samples diagonals, which can leave the chunk on
        // two axes at once; that chunk is not among the four we are given.
        if (past_x && past_z) return -1;

        if (past_x) {
            const ChunkBlocks* side = x < 0 ? neighbours.west : neighbours.east;
            return side ? side->at(x & 15, y, z) : -1;
        }
        if (past_z) {
            const ChunkBlocks* side = z < 0 ? neighbours.north : neighbours.south;
            return side ? side->at(x, y, z & 15) : -1;
        }
        return blocks.at(x, y, z);
    };

    const auto biome_at = [&](int32_t x, int32_t y, int32_t z) -> uint16_t {
        const bool past_x = x < 0 || x > 15;
        const bool past_z = z < 0 || z > 15;
        if (past_x && past_z) {
            // The blend's square is the only thing that reaches a diagonal.
            const ChunkBlocks* corner = x < 0 ? (z < 0 ? neighbours.north_west : neighbours.south_west)
                                              : (z < 0 ? neighbours.north_east : neighbours.south_east);
            return corner ? corner->biome_at(x & 15, y, z & 15) : 0;
        }
        if (past_x) {
            const ChunkBlocks* side = x < 0 ? neighbours.west : neighbours.east;
            return side ? side->biome_at(x & 15, y, z) : 0;
        }
        if (past_z) {
            const ChunkBlocks* side = z < 0 ? neighbours.north : neighbours.south;
            return side ? side->biome_at(x, y, z & 15) : 0;
        }
        return blocks.biome_at(x, y, z);
    };

    // The blend over a 5x5 square costs 25 reads a block, and every corner of
    // every tinted quad wants it. A biome is the same across a 4x4x4 cell, so
    // one answer per cell and per column of the section serves them all, which
    // is ours: the game caches whole 16x16 layers in BlockTintCache instead.
    std::array<uint32_t, kTintChannels * 4 * kSection * kSection> blend_cache{};
    std::array<uint8_t, kTintChannels * 4 * kSection * kSection> blend_known{};
    int32_t blend_section = -1;
    const auto blended_tint = [&](int channel, int32_t x, int32_t local_y, int32_t z, int32_t section) {
        if (section != blend_section) {
            blend_known.fill(0);
            blend_section = section;
        }
        const size_t key = ((static_cast<size_t>(channel) * 4 +
                             static_cast<size_t>(local_y / 4)) * kSection +
                            static_cast<size_t>(z)) * kSection + static_cast<size_t>(x);
        if (blend_known[key] != 0) return blend_cache[key];
        const int32_t y = section * kSection + local_y;
        const uint32_t colour = blend_biome_tint(
            tints, channel, x, z,
            [&](int32_t bx, int32_t bz) { return biome_at(bx, y, bz); });
        blend_known[key] = 1;
        blend_cache[key] = colour;
        return colour;
    };

    // Daylight on anything we cannot see, so an unloaded neighbour reads as
    // open sky rather than as a wall of shadow.
    const auto light_at = [&](int32_t x, int32_t y, int32_t z) -> uint8_t {
        const bool past_x = x < 0 || x > 15;
        const bool past_z = z < 0 || z > 15;
        if (past_x && past_z) return 0x0F;
        if (past_x) {
            const ChunkBlocks* side = x < 0 ? neighbours.west : neighbours.east;
            return side ? side->light_at(x & 15, y, z) : 0x0F;
        }
        if (past_z) {
            const ChunkBlocks* side = z < 0 ? neighbours.north : neighbours.south;
            return side ? side->light_at(x, y, z & 15) : 0x0F;
        }
        return blocks.light_at(x, y, z);
    };

    for (int32_t section = 0; section < section_count; ++section) {
        if (chunk.sections[static_cast<size_t>(section)].non_air_count == 0) continue;

        // Fill the padded cube once, instead of 27 reads per block.
        for (int32_t py = -kPad; py < kSection + kPad; ++py) {
            for (int32_t pz = -kPad; pz < kSection + kPad; ++pz) {
                for (int32_t px = -kPad; px < kSection + kPad; ++px) {
                    const int32_t state = state_at(px, section * kSection + py, pz);
                    const auto cell = padded_index(px, py, pz);
                    padded_state[cell] = state;
                    padded_solid[cell] = state >= 0 && table.occludes(state);
                    const uint8_t nibbles = light_at(px, section * kSection + py, pz);
                    padded_light[cell] = nibbles;

                    // `LevelRenderer.getLightColor`: a block that renders
                    // emissive is full bright, and one that glows lifts the
                    // block light it stands in to its own emission.
                    const RenderInfo& info = render.at(state);
                    uint32_t packed = pack_light(nibbles >> 4, nibbles & 0x0Fu);
                    if (info.has(RenderInfo::kEmissive)) {
                        packed = kFullBright;
                    } else if ((nibbles >> 4) < info.emission) {
                        packed = pack_light(info.emission, nibbles & 0x0Fu);
                    }
                    padded_packed[cell] = packed;
                    padded_shade[cell] = info.shade_brightness;
                    padded_open[cell] =
                        !info.has(RenderInfo::kViewBlocking) || info.light_block == 0;
                    padded_full[cell] = info.has(RenderInfo::kSolidRender);
                }
            }
        }

        SectionMesh mesh;
        mesh.origin[0] = chunk.x * kSection;
        mesh.origin[1] = chunk.min_y + section * kSection;
        mesh.origin[2] = chunk.z * kSection;
        mesh.vertices.reserve(1 << 12);
        mesh.indices.reserve(1 << 12);
        std::vector<uint32_t> blended;

        for (int32_t local_y = 0; local_y < kSection; ++local_y) {
            for (int32_t z = 0; z < kSection; ++z) {
                for (int32_t x = 0; x < kSection; ++x) {
                    const int32_t state = padded_state[padded_index(x, local_y, z)];
                    if (state <= 0) continue;
                    const auto quads = table.quads(state);
                    if (render_table().fluid(state).fluid != Fluid::None) {
                        const bool lava_here =
                            render_table().fluid(state).fluid == Fluid::Lava;
                        const uint32_t water_tint =
                            lava_here ? 0u : blended_tint(2, x, local_y, z, section);
                        const auto emit = [&](const LiquidVertex corners[4], int32_t texture, int face,
                                              uint8_t light, bool water) {
                            if (texture < 0) return;
                            const auto base = static_cast<uint32_t>(mesh.vertices.size());
                            for (int i = 0; i < 4; ++i) {
                                Vertex vertex{};
                                vertex.position[0] = Vertex::encode(static_cast<float>(x) + corners[i].x);
                                vertex.position[1] = Vertex::encode(static_cast<float>(local_y) + corners[i].y);
                                vertex.position[2] = Vertex::encode(static_cast<float>(z) + corners[i].z);
                                vertex.uv[0] = static_cast<uint16_t>(std::clamp(corners[i].u, 0.0f, 1.0f) * 65535.0f + 0.5f);
                                vertex.uv[1] = static_cast<uint16_t>(std::clamp(corners[i].v, 0.0f, 1.0f) * 65535.0f + 0.5f);
                                vertex.texture = static_cast<uint16_t>(texture);
                                vertex.packed = water ? uint16_t{3} : uint16_t{0};
                                vertex.set_brightness(kFaceShade[face]);
                                vertex.light = static_cast<uint16_t>(
                                    ((light >> 4) * 16u) | ((light & 0x0Fu) * 16u) << 8);
                                vertex.tint = water_tint;
                                mesh.vertices.push_back(vertex);
                            }
                            // The game winds its faces the other way round from this mesher.
                            auto& into = water ? blended : mesh.indices;
                            into.insert(into.end(), {base, base + 2, base + 1, base, base + 3, base + 2});
                            ++mesh.quads_emitted;
                        };
                        mesh_liquid(
                            state, fluid_textures(table),
                            [&](int dx, int dy, int dz) {
                                return padded_state[padded_index(x + dx, local_y + dy, z + dz)];
                            },
                            [&](int dx, int dy, int dz) {
                                return padded_light[padded_index(x + dx, local_y + dy, z + dz)];
                            },
                            emit);
                        if (table.fluid(state)) continue;
                    }
                    if (quads.empty()) continue;

                    const RenderInfo& here = render.at(state);
                    // `ModelBlockRenderer.tesselateBlock`: a plant stands a
                    // little off the middle of its own block, by a hash of the
                    // column it grows in, so a field is not a grid.
                    float wander[3];
                    block_offset(here, chunk.x * kSection + x, chunk.z * kSection + z, wander);

                    // A fluid also hides against its own kind, or an ocean
                    // would be full of faces between neighbouring water blocks.
                    const bool self_fluid = table.fluid(state);
                    // Only the top block of a fluid column has a lowered
                    // surface; a submerged one fills its block, or the column
                    // would be sliced by a gap at every level.
                    const bool submerged =
                        self_fluid && table.fluid(padded_state[padded_index(x, local_y + 1, z)]);
                    // Glass and ice hide the face between two of the same
                    // block, so a sheet of it is one sheet and not a lattice.
                    const bool self_culls = table.self_culls(state);
                    const uint32_t own_block = self_culls ? table.block_of(state) : 0;
                    bool hidden[6];
                    for (int side = 0; side < 6; ++side) {
                        const Offset offset = kFaceOffset[side];
                        const auto cell =
                            padded_index(x + offset.x, local_y + offset.y, z + offset.z);
                        if (padded_solid[cell]) {
                            hidden[side] = true;
                            continue;
                        }
                        const int32_t neighbour = padded_state[cell];
                        if (self_culls) {
                            hidden[side] = neighbour > 0 && table.block_of(neighbour) == own_block;
                            continue;
                        }
                        if (!self_fluid) {
                            hidden[side] = false;
                            continue;
                        }
                        hidden[side] = neighbour > 0 && table.fluid(neighbour);
                    }


                    for (const auto& quad : quads) {
                        if (quad.cullface >= 0) {
                            const int culling_face =
                                rotate_face(quad.cullface, quad.rotation_x, quad.rotation_y);
                            if (hidden[culling_face]) {
                                ++mesh.quads_culled;
                                continue;
                            }
                        }

                        const auto base = static_cast<uint32_t>(mesh.vertices.size());
                        const int face = rotate_face(quad.face, quad.rotation_x, quad.rotation_y);
                        const Offset normal = kFaceOffset[face];
                        const int axis_a = kFaceAxes[face][0];
                        const int axis_b = kFaceAxes[face][1];

                        float placed[4][3];
                        for (int corner = 0; corner < 4; ++corner) {
                            const int* pick = kCorner[quad.face][corner];
                            const float top = submerged ? kBlock : quad.to[1];
                            placed[corner][0] = (pick[0] ? quad.to[0] : quad.from[0]) / kBlock;
                            placed[corner][1] = (pick[1] ? top : quad.from[1]) / kBlock;
                            placed[corner][2] = (pick[2] ? quad.to[2] : quad.from[2]) / kBlock;
                            apply_element_rotation(placed[corner], quad);
                            apply_rotation(placed[corner], quad.rotation_x, quad.rotation_y);
                        }

                        // `ModelBlockRenderer.calculateShape`: how far the quad
                        // reaches along each axis, and whether that is the
                        // whole side of the block or only part of it.
                        float low[3] = {32.0f, 32.0f, 32.0f};
                        float high[3] = {-32.0f, -32.0f, -32.0f};
                        for (const auto& corner : placed) {
                            for (int axis = 0; axis < 3; ++axis) {
                                low[axis] = std::min(low[axis], corner[axis]);
                                high[axis] = std::max(high[axis], corner[axis]);
                            }
                        }
                        float shape[kSizeCount];
                        shape[kSizeWest] = low[0];
                        shape[kSizeEast] = high[0];
                        shape[kSizeDown] = low[1];
                        shape[kSizeUp] = high[1];
                        shape[kSizeNorth] = low[2];
                        shape[kSizeSouth] = high[2];
                        for (int i = 0; i < 6; ++i) shape[6 + i] = 1.0f - shape[i];

                        const int normal_axis = face < 2 ? 1 : (face < 4 ? 2 : 0);
                        const bool towards_positive = (face & 1) != 0;
                        const bool face_partial =
                            low[axis_a] >= 1.0e-4f || low[axis_b] >= 1.0e-4f ||
                            high[axis_a] <= 0.9999f || high[axis_b] <= 0.9999f;
                        const bool face_cubic =
                            low[normal_axis] == high[normal_axis] &&
                            ((towards_positive ? high[normal_axis] > 0.9999f
                                               : low[normal_axis] < 1.0e-4f) ||
                             here.has(RenderInfo::kCollisionFull));

                        const size_t own_cell = padded_index(x, local_y, z);
                        const size_t front_cell =
                            padded_index(x + normal.x, local_y + normal.y, z + normal.z);
                        // `ModelBlockRenderer.tesselateBlock`: a block that
                        // gives off light is lit flat, however its model asks.
                        const bool smooth = (quad.flags & kQuadNoAo) == 0 && here.emission == 0;

                        float brightness[4];
                        uint32_t corner_light[4];
                        // `ClientLevel.getShade`: a face is darkened for the
                        // way it points, unless the quad asks not to be.
                        const float shade =
                            (quad.flags & kQuadNoShade) != 0 ? 1.0f : kFaceShade[face];

                        if (smooth) {
                            // `ModelBlockRenderer.AmbientOcclusionRenderStorage.calculate`.
                            const int bx = x + (face_cubic ? normal.x : 0);
                            const int by = local_y + (face_cubic ? normal.y : 0);
                            const int bz = z + (face_cubic ? normal.z : 0);

                            float side_shade[4];
                            uint32_t side_light[4];
                            bool side_open[4];
                            for (int i = 0; i < 4; ++i) {
                                const Offset d = kFaceOffset[kAdjacent[face][i]];
                                const size_t cell = padded_index(bx + d.x, by + d.y, bz + d.z);
                                side_shade[i] = padded_shade[cell];
                                side_light[i] = padded_packed[cell];
                                side_open[i] = padded_open[padded_index(bx + d.x + normal.x,
                                                                        by + d.y + normal.y,
                                                                        bz + d.z + normal.z)] != 0;
                            }

                            float diagonal_shade[4];
                            uint32_t diagonal_light[4];
                            // The four corners pair the sides (3,0), (2,0),
                            // (2,1) and (3,1); where neither side lets light
                            // past, the diagonal behind them cannot be seen and
                            // the first side stands in for it.
                            static constexpr int kPairs[4][2] = {{3, 0}, {2, 0}, {2, 1}, {3, 1}};
                            for (int k = 0; k < 4; ++k) {
                                const int first = kPairs[k][1];
                                const int second = kPairs[k][0];
                                if (!side_open[second] && !side_open[0]) {
                                    diagonal_shade[k] = side_shade[0];
                                    diagonal_light[k] = side_light[0];
                                    continue;
                                }
                                const Offset a = kFaceOffset[kAdjacent[face][first]];
                                const Offset b = kFaceOffset[kAdjacent[face][second]];
                                const size_t cell = padded_index(bx + a.x + b.x, by + a.y + b.y,
                                                                 bz + a.z + b.z);
                                diagonal_shade[k] = padded_shade[cell];
                                diagonal_light[k] = padded_packed[cell];
                            }

                            uint32_t centre_light = padded_packed[own_cell];
                            if (face_cubic || padded_full[front_cell] == 0) {
                                centre_light = padded_packed[front_cell];
                            }
                            const float centre_shade = padded_shade[padded_index(bx, by, bz)];

                            float blended_shade[4];
                            uint32_t blended_light[4];
                            for (int k = 0; k < 4; ++k) {
                                const int first = kPairs[k][1];
                                const int second = kPairs[k][0];
                                blended_shade[k] = (side_shade[second] + side_shade[first] +
                                                    diagonal_shade[k] + centre_shade) * 0.25f;
                                blended_light[k] = blend_light(side_light[second],
                                                               side_light[first],
                                                               diagonal_light[k], centre_light);
                            }

                            for (int corner = 0; corner < 4; ++corner) {
                                const int* pick = kCorner[quad.face][corner];
                                const int k = kCornerOrder[face][(pick[axis_a] ? 2 : 0) +
                                                                 (pick[axis_b] ? 1 : 0)];
                                if (!face_partial) {
                                    brightness[corner] = blended_shade[k] * shade;
                                    corner_light[corner] = blended_light[k];
                                    continue;
                                }
                                float weight[4];
                                float total = 0.0f;
                                for (int j = 0; j < 4; ++j) {
                                    weight[j] = shape[kCornerWeights[face][k][j * 2]] *
                                                shape[kCornerWeights[face][k][j * 2 + 1]];
                                    total += blended_shade[j] * weight[j];
                                }
                                brightness[corner] = std::clamp(total, 0.0f, 1.0f) * shade;
                                corner_light[corner] = blend_light(blended_light, weight);
                            }
                        } else {
                            // `ModelBlockRenderer.renderModelFaceFlat`: one
                            // light for the whole quad, taken from the cell the
                            // face looks into.
                            const int light_face =
                                quad.cullface >= 0
                                    ? rotate_face(quad.cullface, quad.rotation_x,
                                                  quad.rotation_y)
                                    : face;
                            const Offset towards = kFaceOffset[light_face];
                            const size_t cell =
                                quad.cullface >= 0 || face_cubic
                                    ? padded_index(x + towards.x, local_y + towards.y,
                                                   z + towards.z)
                                    : own_cell;
                            const uint8_t nibbles = padded_light[cell];
                            uint32_t flat = pack_light(nibbles >> 4, nibbles & 0x0Fu);
                            if (here.has(RenderInfo::kEmissive)) {
                                flat = kFullBright;
                            } else if ((nibbles >> 4) < here.emission) {
                                flat = pack_light(here.emission, nibbles & 0x0Fu);
                            }
                            for (int corner = 0; corner < 4; ++corner) {
                                brightness[corner] = shade;
                                corner_light[corner] = flat;
                            }
                        }

                        for (int corner = 0; corner < 4; ++corner) {
                            const float world[3] = {
                                static_cast<float>(x) + placed[corner][0] + wander[0],
                                static_cast<float>(local_y) + placed[corner][1] + wander[1],
                                static_cast<float>(z) + placed[corner][2] + wander[2],
                            };

                            Vertex vertex{};
                            for (int axis = 0; axis < 3; ++axis) {
                                vertex.position[axis] = Vertex::encode(world[axis]);
                            }

                            const float u = (quad.uv[0] + (quad.uv[2] - quad.uv[0]) *
                                                              kCornerUv[corner][0]) /
                                            (16.0f * kUnitsPerModelUnit);
                            const float v = (quad.uv[1] + (quad.uv[3] - quad.uv[1]) *
                                                              kCornerUv[corner][1]) /
                                            (16.0f * kUnitsPerModelUnit);
                            vertex.uv[0] = static_cast<uint16_t>(
                                std::clamp(u, 0.0f, 1.0f) * 65535.0f + 0.5f);
                            vertex.uv[1] = static_cast<uint16_t>(
                                std::clamp(v, 0.0f, 1.0f) * 65535.0f + 0.5f);

                            vertex.texture = static_cast<uint16_t>(quad.texture);
                            vertex.packed = quad.tint >= 0
                                                ? static_cast<uint16_t>(quad.tint + 1)
                                                : uint16_t{0};
                            const int tint_channel = kTintChannel[vertex.packed & 7u];
                            vertex.set_brightness(brightness[corner]);
                            vertex.light = static_cast<uint16_t>(
                                (corner_light[corner] & 0xFFu) |
                                (((corner_light[corner] >> 16) & 0xFFu) << 8));
                            vertex.tint = tint_channel >= 0
                                             ? blended_tint(tint_channel, x, local_y, z, section)
                                             : 0u;
                            mesh.vertices.push_back(vertex);
                        }

                        auto& into = (quad.flags & kQuadTranslucent) != 0 ? blended
                                                                          : mesh.indices;
                        into.insert(into.end(),
                                    {base, base + 1, base + 2, base, base + 2, base + 3});
                        ++mesh.quads_emitted;
                    }
                }
            }
        }

        mesh.opaque_indices = mesh.indices.size();
        mesh.indices.insert(mesh.indices.end(), blended.begin(), blended.end());

        if (mesh.indices.empty()) {
            // Still count what was culled, so the totals stay honest.
            if (!meshes.empty()) meshes.back().quads_culled += mesh.quads_culled;
            continue;
        }

        const auto decode = [](uint16_t value) {
            return static_cast<float>(value) / static_cast<float>(kPositionScale) -
                   static_cast<float>(kPositionBias);
        };
        for (int axis = 0; axis < 3; ++axis) {
            mesh.bounds_min[axis] = decode(mesh.vertices.front().position[axis]);
            mesh.bounds_max[axis] = mesh.bounds_min[axis];
        }
        for (const auto& vertex : mesh.vertices) {
            for (int axis = 0; axis < 3; ++axis) {
                const float value = decode(vertex.position[axis]);
                mesh.bounds_min[axis] = std::min(mesh.bounds_min[axis], value);
                mesh.bounds_max[axis] = std::max(mesh.bounds_max[axis], value);
            }
        }
        meshes.push_back(std::move(mesh));
    }
    return meshes;
}

Mesh mesh_chunk(const Chunk& chunk, const ChunkBlocks& blocks, const BlockTable& table,
                const Neighbours& neighbours, const BiomeTints& tints) {
    Mesh merged;
    std::vector<uint32_t> blended;
    for (const auto& section : mesh_sections(chunk, blocks, table, neighbours, tints)) {
        const auto base = static_cast<uint32_t>(merged.vertices.size());
        // The merged form keeps section-local positions; only the renderer's
        // section path knows where each section sits.
        merged.vertices.insert(merged.vertices.end(), section.vertices.begin(),
                               section.vertices.end());
        for (size_t i = 0; i < section.indices.size(); ++i) {
            auto& into = i < section.opaque_indices ? merged.indices : blended;
            into.push_back(base + section.indices[i]);
        }
        merged.quads_emitted += section.quads_emitted;
        merged.quads_culled += section.quads_culled;
    }
    merged.opaque_indices = merged.indices.size();
    merged.indices.insert(merged.indices.end(), blended.begin(), blended.end());
    return merged;
}

void quad_corners(const BakedQuad& quad, float out[4][3], int& face) {
    face = rotate_face(quad.face, quad.rotation_x, quad.rotation_y);
    for (int corner = 0; corner < 4; ++corner) {
        const int* pick = kCorner[quad.face][corner];
        float local[3] = {
            (pick[0] ? quad.to[0] : quad.from[0]) / kBlock,
            (pick[1] ? quad.to[1] : quad.from[1]) / kBlock,
            (pick[2] ? quad.to[2] : quad.from[2]) / kBlock,
        };
        apply_element_rotation(local, quad);
        apply_rotation(local, quad.rotation_x, quad.rotation_y);
        std::copy(local, local + 3, out[corner]);
    }
}

int quad_cull_face(const BakedQuad& quad) {
    return quad.cullface < 0 ? -1 : rotate_face(quad.cullface, quad.rotation_x, quad.rotation_y);
}

} // namespace mcpp::world
