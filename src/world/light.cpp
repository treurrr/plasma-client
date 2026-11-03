#include "mcpp/world/light.hpp"

#include <algorithm>
#include <array>
#include <climits>
#include <deque>

namespace mcpp::world {
namespace {

struct Cell {
    int32_t x, y, z;
    uint8_t level;
};

// Down is index 3, which matters: it is the one direction sky light may travel
// at full strength.
constexpr std::array<std::array<int32_t, 3>, 6> kSides{{
    {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}};
constexpr size_t kDown = 3;

// One light layer seen across chunk boundaries. A chunk that is not loaded is
// absent, never written, which keeps a fill inside the world the client has.
class Field {
public:
    Field(ChunkMap& world, bool sky) : world_(world), sky_(sky) {}

    // -1 where there is nothing to read.
    int get(int32_t x, int32_t y, int32_t z) {
        Chunk* chunk = find(x, z);
        if (chunk == nullptr) return -1;
        LightLayer& layer = sky_ ? chunk->sky_light : chunk->block_light;
        if (layer.sections.empty()) return -1;
        const int32_t index = chunk->index_of(y);
        // Above the top of the world is open sky, which is where sky light
        // comes from in the first place.
        if (index >= static_cast<int32_t>(chunk->sections.size()) * 16) return sky_ ? 15 : 0;
        if (index < 0) return -1;
        return layer.at(static_cast<size_t>(index / 16) + 1, x & 15, index % 16, z & 15);
    }

    void set(int32_t x, int32_t y, int32_t z, int level) {
        Chunk* chunk = find(x, z);
        if (chunk == nullptr) return;
        LightLayer& layer = sky_ ? chunk->sky_light : chunk->block_light;
        const int32_t index = chunk->index_of(y);
        if (layer.sections.empty() || index < 0 ||
            index >= static_cast<int32_t>(chunk->sections.size()) * 16) {
            return;
        }
        layer.set(static_cast<size_t>(index / 16) + 1, x & 15, index % 16, z & 15,
                  static_cast<uint8_t>(level));
        // A cell on a seam is read by the neighbour's mesh as well.
        const int32_t cx = x >> 4;
        const int32_t cz = z >> 4;
        touched.insert({cx, cz});
        if ((x & 15) == 0) touched.insert({cx - 1, cz});
        if ((x & 15) == 15) touched.insert({cx + 1, cz});
        if ((z & 15) == 0) touched.insert({cx, cz - 1});
        if ((z & 15) == 15) touched.insert({cx, cz + 1});
    }

    int32_t state(int32_t x, int32_t y, int32_t z) {
        Chunk* chunk = find(x, z);
        if (chunk == nullptr) return 0;
        const int32_t index = chunk->index_of(y);
        if (index < 0 || index >= static_cast<int32_t>(chunk->sections.size()) * 16) return 0;
        return chunk->block_state(x & 15, index, z & 15);
    }

    std::set<std::pair<int32_t, int32_t>> touched;

private:
    Chunk* find(int32_t x, int32_t z) {
        const std::pair<int32_t, int32_t> key{x >> 4, z >> 4};
        if (key != cached_key_) {
            const auto found = world_.find(key);
            cached_key_ = key;
            cached_ = found == world_.end() ? nullptr : &found->second;
        }
        return cached_;
    }

    ChunkMap& world_;
    bool sky_;
    std::pair<int32_t, int32_t> cached_key_{INT32_MIN, INT32_MIN};
    Chunk* cached_ = nullptr;
};

} // namespace

int LightEngine::opacity(int32_t state) {
    if (state <= 0) return 0;
    const auto index = static_cast<size_t>(state);
    if (index >= opacity_.size()) opacity_.resize(index + 1, -1);
    int8_t& cached = opacity_[index];
    if (cached < 0) {
        if (table_.occludes(state)) {
            cached = 15;
        } else if (table_.fluid(state)) {
            cached = 1;
        } else {
            // Leaves and cobwebs thin the light without stopping it, which is
            // why the ground under a tree is dim rather than dark.
            const auto name = table_.block_name(state);
            cached = name.ends_with("_leaves") || name == "minecraft:cobweb" ? 1 : 0;
        }
    }
    return cached;
}

std::set<std::pair<int32_t, int32_t>> LightEngine::relight(ChunkMap& world, int32_t x, int32_t y,
                                                           int32_t z) {
    std::set<std::pair<int32_t, int32_t>> touched;

    for (const bool sky : {true, false}) {
        Field field(world, sky);
        const int before = field.get(x, y, z);
        if (before < 0) continue;

        std::deque<Cell> removal;
        std::deque<Cell> addition;

        // Take away everything that leaned on this cell. Whatever was lit
        // from somewhere else is queued to spread again instead.
        field.set(x, y, z, 0);
        removal.push_back({x, y, z, static_cast<uint8_t>(before)});
        while (!removal.empty()) {
            const Cell cell = removal.front();
            removal.pop_front();
            for (size_t side = 0; side < kSides.size(); ++side) {
                const int32_t nx = cell.x + kSides[side][0];
                const int32_t ny = cell.y + kSides[side][1];
                const int32_t nz = cell.z + kSides[side][2];
                const int level = field.get(nx, ny, nz);
                if (level <= 0) continue;
                const bool falling = sky && side == kDown && cell.level == 15 && level == 15;
                if (level < cell.level || falling) {
                    field.set(nx, ny, nz, 0);
                    removal.push_back({nx, ny, nz, static_cast<uint8_t>(level)});
                } else {
                    addition.push_back({nx, ny, nz, static_cast<uint8_t>(level)});
                }
            }
        }

        // Spread back out. Every step costs a level, or the cell's opacity if
        // that is more, except sky light at full strength going straight down.
        while (!addition.empty()) {
            const Cell cell = addition.front();
            addition.pop_front();
            const int level = field.get(cell.x, cell.y, cell.z);
            if (level <= 0) continue;
            for (size_t side = 0; side < kSides.size(); ++side) {
                const int32_t nx = cell.x + kSides[side][0];
                const int32_t ny = cell.y + kSides[side][1];
                const int32_t nz = cell.z + kSides[side][2];
                const int stops = opacity(field.state(nx, ny, nz));
                if (stops >= 15) continue;
                const int current = field.get(nx, ny, nz);
                if (current < 0) continue;
                const int reached = sky && side == kDown && level == 15 && stops == 0
                                        ? 15
                                        : level - std::max(1, stops);
                if (reached > current) {
                    field.set(nx, ny, nz, reached);
                    addition.push_back({nx, ny, nz, static_cast<uint8_t>(reached)});
                }
            }
        }

        touched.insert(field.touched.begin(), field.touched.end());
    }
    return touched;
}

} // namespace mcpp::world
