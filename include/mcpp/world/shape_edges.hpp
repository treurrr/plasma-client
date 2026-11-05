#pragma once
#include "mcpp/world/shapes.hpp"

#include <functional>
#include <span>

// The edges of a block shape the way the game strokes its outline.
namespace mcpp::world {

// One edge from (x0, y0, z0) to (x1, y1, z1), in the block's own coordinates.
using EdgeConsumer = std::function<void(double, double, double, double, double, double)>;

// Every edge of a shape, neighbouring cells merged into one line, in the
// game's order: the runs along z, then along y, then along x.
void for_all_edges(const BlockShapes& shapes, uint16_t shape, const EdgeConsumer& out);

} // namespace mcpp::world
