#include "mcpp/world/shape_edges.hpp"

#include <array>

namespace mcpp::world {
namespace {

enum Cycle { kNone = 0, kForward = 1, kBackward = 2 };

// `AxisCycle.cycle`: one coordinate of a cycled triple.
int cycled(int cycle, int a, int b, int c, int axis) {
    const std::array<int, 3> picked = cycle == kNone      ? std::array<int, 3>{a, b, c}
                                      : cycle == kForward ? std::array<int, 3>{c, a, b}
                                                          : std::array<int, 3>{b, c, a};
    return picked[static_cast<size_t>(axis)];
}

// `AxisCycle.cycle`: where an axis goes.
int cycled_axis(int cycle, int axis) { return (axis + cycle) % 3; }

int inverse(int cycle) { return cycle == kNone ? kNone : cycle == kForward ? kBackward : kForward; }

} // namespace

// `VoxelShape.forAllEdges`, `DiscreteVoxelShape.forAllEdges`, `DiscreteVoxelShape.forAllAxisEdges`, `DiscreteVoxelShape.isFullWide`: every edge where the shape's surface folds, runs of cells merged.
void for_all_edges(const BlockShapes& shapes, uint16_t shape, const EdgeConsumer& out) {
    if (shapes.empty(shape)) return;
    const Voxels& v = shapes.voxels(shape);
    const int size[3] = {v.edges[0] - 1, v.edges[1] - 1, v.edges[2] - 1};
    const auto full_wide = [&](int x, int y, int z) {
        if (x < 0 || y < 0 || z < 0) return false;
        return x < size[0] && y < size[1] && z < size[2] && shapes.full(v, x, y, z);
    };
    const auto coordinate = [&](int axis, int index) {
        return shapes.edges(v, axis)[static_cast<size_t>(index)];
    };
    const auto emit = [&](int c, int a, int b, int from, int to) {
        out(coordinate(0, cycled(c, a, b, from, 0)), coordinate(1, cycled(c, a, b, from, 1)),
            coordinate(2, cycled(c, a, b, from, 2)), coordinate(0, cycled(c, a, b, to, 0)),
            coordinate(1, cycled(c, a, b, to, 1)), coordinate(2, cycled(c, a, b, to, 2)));
    };
    for (const int pass : {kNone, kForward, kBackward}) {
        const int c = inverse(pass);
        const int n4 = size[cycled_axis(c, 0)];
        const int n5 = size[cycled_axis(c, 1)];
        const int n6 = size[cycled_axis(c, 2)];
        for (int a = 0; a <= n4; ++a) {
            for (int b = 0; b <= n5; ++b) {
                int start = -1;
                for (int k = 0; k <= n6; ++k) {
                    int count = 0;
                    int parity = 0;
                    for (int da = 0; da <= 1; ++da) {
                        for (int db = 0; db <= 1; ++db) {
                            const int i = a + da - 1;
                            const int j = b + db - 1;
                            if (full_wide(cycled(c, i, j, k, 0), cycled(c, i, j, k, 1),
                                          cycled(c, i, j, k, 2))) {
                                ++count;
                                parity ^= da ^ db;
                            }
                        }
                    }
                    if (count == 1 || count == 3 || (count == 2 && (parity & 1) == 0)) {
                        if (start == -1) start = k;
                    } else if (start != -1) {
                        emit(c, a, b, start, k);
                        start = -1;
                    }
                }
            }
        }
    }
}

} // namespace mcpp::world
