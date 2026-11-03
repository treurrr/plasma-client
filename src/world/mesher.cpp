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

// One block of padding on every side: ambient occlusion reads diagonals, so the
// inner loop needs a ring of neighbours. With it the loop never tests a bound
// or crosses into another chunk again.
constexpr int kPadded = kSection + 2;
constexpr size_t kPaddedCells = kPadded * kPadded * kPadded;

constexpr size_t padded_index(int x, int y, int z) {
    return static_cast<size_t>((y + 1) * kPadded * kPadded + (z + 1) * kPadded + (x + 1));
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

// Minecraft's smooth lighting: a corner is darkest when both edge neighbours
// are solid, and then the diagonal no longer matters because it cannot be seen.
int corner_shadow(bool side_a, bool side_b, bool diagonal) {
    if (side_a && side_b) return 0;
    return 3 - (static_cast<int>(side_a) + static_cast<int>(side_b) +
                static_cast<int>(diagonal));
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
    const auto should_render = [&](int dx, int dy, int dz) { return !self_full && !same(dx, dy, dz); };

    const bool up = !same(0, 1, 0);
    const bool down = should_render(0, -1, 0) && !occluded_by(0, -1, 0, false, 0.8888889f);
    const bool north = should_render(0, 0, -1);
    const bool south = should_render(0, 0, 1);
    const bool west = should_render(-1, 0, 0);
    const bool east = should_render(1, 0, 0);
    if (!up && !down && !north && !south && !west && !east) return;

    const auto height = [&](int dx, int dz) -> float {
        if (same(dx, 0, dz)) return same(dx, 1, dz) ? 1.0f : fluid_at(dx, 0, dz).own_height();
        return info_at(dx, 0, dz).has(RenderInfo::kCollisionFull) ? -1.0f : 0.0f;
    };
    const auto weighted = [](float sum[2], float h) {
        if (h >= 0.8f) {
            sum[0] += h * 10.0f;
            sum[1] += 10.0f;
        } else if (h >= 0.0f) {
            sum[0] += h;
            sum[1] += 1.0f;
        }
    };
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
                                       const BlockTable& table, const Neighbours& neighbours) {
    std::vector<SectionMesh> meshes;
    const auto section_count = static_cast<int32_t>(blocks.section_count());

    std::vector<int32_t> padded_state(kPaddedCells);
    std::vector<uint8_t> padded_solid(kPaddedCells);
    std::vector<uint8_t> padded_light(kPaddedCells);
    std::vector<uint16_t> padded_biome(kPaddedCells);

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
        if (past_x && past_z) return 0;
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

        // Fill the padded cube once: 5832 reads here instead of 27 per block.
        for (int32_t py = -1; py <= kSection; ++py) {
            for (int32_t pz = -1; pz <= kSection; ++pz) {
                for (int32_t px = -1; px <= kSection; ++px) {
                    const int32_t state = state_at(px, section * kSection + py, pz);
                    const auto cell = padded_index(px, py, pz);
                    padded_state[cell] = state;
                    padded_solid[cell] = state >= 0 && table.occludes(state);
                    padded_light[cell] = light_at(px, section * kSection + py, pz);
                    padded_biome[cell] = biome_at(px, section * kSection + py, pz);
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
                        const uint16_t biome = padded_biome[padded_index(x, local_y, z)] & 0xFF;
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
                                vertex.packed = static_cast<uint16_t>(face | (water ? 3 << 3 : 0) | (3 << 6));
                                vertex.light = static_cast<uint16_t>(light | (biome << 8));
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

                        for (int corner = 0; corner < 4; ++corner) {
                            const int* pick = kCorner[quad.face][corner];
                            const float top = submerged ? kBlock : quad.to[1];
                            float local[3] = {
                                (pick[0] ? quad.to[0] : quad.from[0]) / kBlock,
                                (pick[1] ? top : quad.from[1]) / kBlock,
                                (pick[2] ? quad.to[2] : quad.from[2]) / kBlock,
                            };
                            apply_element_rotation(local, quad);
                            apply_rotation(local, quad.rotation_x, quad.rotation_y);

                            const int away[3] = {normal.x, normal.y, normal.z};
                            const int sign_a = pick[axis_a] ? 1 : -1;
                            const int sign_b = pick[axis_b] ? 1 : -1;
                            const int base_cell[3] = {x + away[0], local_y + away[1], z + away[2]};

                            int probe[3] = {base_cell[0], base_cell[1], base_cell[2]};
                            const auto cell_base = padded_index(probe[0], probe[1], probe[2]);
                            probe[axis_a] += sign_a;
                            const auto cell_a = padded_index(probe[0], probe[1], probe[2]);

                            probe[0] = base_cell[0];
                            probe[1] = base_cell[1];
                            probe[2] = base_cell[2];
                            probe[axis_b] += sign_b;
                            const auto cell_b = padded_index(probe[0], probe[1], probe[2]);

                            probe[axis_a] += sign_a;
                            const auto cell_diagonal = padded_index(probe[0], probe[1], probe[2]);

                            const bool side_a = padded_solid[cell_a] != 0;
                            const bool side_b = padded_solid[cell_b] != 0;
                            const bool diagonal = padded_solid[cell_diagonal] != 0;

                            const auto shadow =
                                (quad.flags & kQuadNoAo) != 0
                                    ? 3u
                                    : static_cast<uint32_t>(corner_shadow(side_a, side_b,
                                                                         diagonal));

                            // Smooth lighting: the corner takes the average of
                            // the four cells touching it, skipping the ones a
                            // block is in the way of.
                            int sky_sum = 0;
                            int block_sum = 0;
                            int lit = 0;
                            for (const auto cell : {cell_base, cell_a, cell_b, cell_diagonal}) {
                                if (padded_solid[cell] != 0) continue;
                                sky_sum += padded_light[cell] & 0x0F;
                                block_sum += padded_light[cell] >> 4;
                                ++lit;
                            }
                            if (lit == 0) {
                                // A face hemmed in by solid blocks - a plant
                                // leaning into a wall - is lit where it stands.
                                const auto here = padded_light[padded_index(x, local_y, z)];
                                sky_sum = here & 0x0F;
                                block_sum = here >> 4;
                                lit = 1;
                            }
                            const auto sky = static_cast<uint16_t>((sky_sum + lit / 2) / lit);
                            const auto block = static_cast<uint16_t>((block_sum + lit / 2) / lit);

                            const float world[3] = {
                                static_cast<float>(x) + local[0],
                                static_cast<float>(local_y) + local[1],
                                static_cast<float>(z) + local[2],
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
                            vertex.packed = static_cast<uint16_t>(face);
                            if (quad.tint >= 0) {
                                vertex.packed |= static_cast<uint16_t>((quad.tint + 1) << 3);
                            }
                            vertex.packed |= static_cast<uint16_t>(shadow << 6);
                            if ((quad.flags & kQuadNoShade) != 0) vertex.packed |= 0x0100u;
                            const uint16_t biome =
                                padded_biome[padded_index(x, local_y, z)] & 0xFF;
                            vertex.light = static_cast<uint16_t>(sky | (block << 4) |
                                                                 (biome << 8));
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
                const Neighbours& neighbours) {
    Mesh merged;
    std::vector<uint32_t> blended;
    for (const auto& section : mesh_sections(chunk, blocks, table, neighbours)) {
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

} // namespace mcpp::world
