#include "mcpp/world/shape_edges.hpp"

#include <algorithm>
#include <array>
#include <vector>

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

// `AxisCycle.inverse`: the cycle that undoes this one.
int inverse(int cycle) { return cycle == kNone ? kNone : cycle == kForward ? kBackward : kForward; }

// The cells of an outline shape, which is baked as boxes: the grid is their
// own edges, and a cell is full when it lies in one of them. It holds the same
// space as the shape the game cut into cells, so the surface folds in the same
// places.
struct Grid {
    std::array<std::vector<double>, 3> coords;
    std::vector<char> full;

    int size(int axis) const { return static_cast<int>(coords[static_cast<size_t>(axis)].size()) - 1; }
    bool at(int x, int y, int z) const {
        if (x < 0 || y < 0 || z < 0 || x >= size(0) || y >= size(1) || z >= size(2)) return false;
        return full[static_cast<size_t>((static_cast<size_t>(z) * static_cast<size_t>(size(1)) +
                                         static_cast<size_t>(y)) *
                                            static_cast<size_t>(size(0)) +
                                        static_cast<size_t>(x))] != 0;
    }
};

Grid grid_of(std::span<const Aabb> boxes) {
    Grid grid;
    for (int axis = 0; axis < 3; ++axis) {
        auto& values = grid.coords[static_cast<size_t>(axis)];
        for (const auto& box : boxes) {
            values.push_back(box.min[axis]);
            values.push_back(box.max[axis]);
        }
        std::ranges::sort(values);
        values.erase(std::unique(values.begin(), values.end()), values.end());
    }
    if (grid.size(0) < 1 || grid.size(1) < 1 || grid.size(2) < 1) return grid;
    grid.full.assign(static_cast<size_t>(grid.size(0)) * static_cast<size_t>(grid.size(1)) *
                         static_cast<size_t>(grid.size(2)),
                     0);
    for (int z = 0; z < grid.size(2); ++z) {
        for (int y = 0; y < grid.size(1); ++y) {
            for (int x = 0; x < grid.size(0); ++x) {
                const double centre[3] = {
                    (grid.coords[0][static_cast<size_t>(x)] + grid.coords[0][static_cast<size_t>(x) + 1]) / 2.0,
                    (grid.coords[1][static_cast<size_t>(y)] + grid.coords[1][static_cast<size_t>(y) + 1]) / 2.0,
                    (grid.coords[2][static_cast<size_t>(z)] + grid.coords[2][static_cast<size_t>(z) + 1]) / 2.0};
                bool inside = false;
                for (const auto& box : boxes) {
                    inside = inside || (centre[0] > box.min[0] && centre[0] < box.max[0] &&
                                        centre[1] > box.min[1] && centre[1] < box.max[1] &&
                                        centre[2] > box.min[2] && centre[2] < box.max[2]);
                }
                const size_t index = (static_cast<size_t>(z) * static_cast<size_t>(grid.size(1)) +
                                      static_cast<size_t>(y)) *
                                         static_cast<size_t>(grid.size(0)) +
                                     static_cast<size_t>(x);
                grid.full[index] = inside ? 1 : 0;
            }
        }
    }
    return grid;
}

} // namespace

// `VoxelShape.forAllEdges`, `DiscreteVoxelShape.forAllEdges`, `DiscreteVoxelShape.forAllAxisEdges`, `DiscreteVoxelShape.isFullWide`: every edge where the shape's surface folds, runs of cells merged.
void for_all_edges(const BlockShapes& shapes, uint16_t shape, const EdgeConsumer& out) {
    if (shapes.empty(shape)) return;
    const Grid grid = grid_of(shapes.boxes(shape));
    if (grid.full.empty()) return;
    const auto coordinate = [&](int axis, int index) {
        return grid.coords[static_cast<size_t>(axis)][static_cast<size_t>(index)];
    };
    const auto emit = [&](int c, int a, int b, int from, int to) {
        out(coordinate(0, cycled(c, a, b, from, 0)), coordinate(1, cycled(c, a, b, from, 1)),
            coordinate(2, cycled(c, a, b, from, 2)), coordinate(0, cycled(c, a, b, to, 0)),
            coordinate(1, cycled(c, a, b, to, 1)), coordinate(2, cycled(c, a, b, to, 2)));
    };
    for (const int pass : {kNone, kForward, kBackward}) {
        const int c = inverse(pass);
        const int n4 = grid.size(cycled_axis(c, 0));
        const int n5 = grid.size(cycled_axis(c, 1));
        const int n6 = grid.size(cycled_axis(c, 2));
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
                            if (grid.at(cycled(c, i, j, k, 0), cycled(c, i, j, k, 1),
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
