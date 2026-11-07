#pragma once
#include "mcpp/world/block_table.hpp"
#include "mcpp/world/chunk.hpp"

#include <cstdint>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace mcpp::world {

using ChunkMap = std::map<std::pair<int32_t, int32_t>, Chunk>;

// The server sends light with a chunk and then leaves it to the client: a
// block broken or placed afterwards is the client's to relight, the same way
// the game's own client does it. Without this a freshly dug hole keeps the
// darkness of the stone that was in it, and every face looking into it is
// drawn black.
//
// Two flood fills, one taking light away and one spreading it again, for sky
// and block light both. Sky light is the one exception to losing a level per
// step: at full strength it falls straight down without fading.
//
// Returns every chunk whose light changed, which are the ones to mesh again.
class LightEngine {
public:
    std::set<std::pair<int32_t, int32_t>> relight(ChunkMap& world, int32_t x, int32_t y,
                                                  int32_t z);

private:
    int opacity(int32_t state);

    std::vector<int8_t> opacity_;   // per block state, filled as states are met
};

} // namespace mcpp::world
