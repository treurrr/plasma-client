#pragma once
// Blocks being broken, ours and everyone else's: which crack each shows, the
// way the level renderer keeps them, and the cracked block's own faces with
// the crack texture laid over them.
#include "mcpp/world/block_table.hpp"

#include <cstdint>
#include <map>
#include <set>
#include <unordered_map>
#include <vector>

namespace mcpp::world {

class BlockDestruction {
public:
    // `id` is who is breaking (an entity id); a stage outside 0..9 stops it.
    void progress(int32_t id, const int32_t pos[3], int32_t stage);
    // One client tick: progress nobody has updated for 400 ticks goes.
    void tick();
    void clear();

    struct Shown {
        int32_t pos[3];
        int32_t stage;
    };
    // Every cracked block within 32 blocks of the camera, with the deepest
    // crack anyone has put in it.
    void shown(const double camera[3], std::vector<Shown>& out) const;
    size_t size() const { return by_id_.size(); }
    int32_t ticks() const { return ticks_; }

private:
    struct Progress {
        int32_t id;
        int32_t pos[3];
        int32_t stage;
        int32_t updated;
    };
    using Key = int64_t;
    static Key key_of(const int32_t pos[3]);
    void remove(const Progress& p);

    std::unordered_map<int32_t, Progress> by_id_;
    // Per block, (stage, id) in the order the game sorts them.
    std::map<Key, std::set<std::pair<int32_t, int32_t>>> by_block_;
    int32_t ticks_ = 0;
};

// One corner of the crack laid over a block, relative to the camera.
struct CrackVertex {
    float position[3];
    float uv[2];
    uint32_t stage;
};
static_assert(sizeof(CrackVertex) == 24, "the vertex layout must match the pipeline");

// The block's faces that are not hidden, four corners each, with the crack
// texture's coordinates as the game projects them along each face.
// `hidden` says, per face (down, up, north, south, west, east), whether the
// neighbour covers it.
void crack_quads(const BlockTable& table, int32_t state, const int32_t pos[3], const double offset[3],
                 const bool hidden[6], const double camera[3], uint32_t stage,
                 std::vector<CrackVertex>& out);

} // namespace mcpp::world
