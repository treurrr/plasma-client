// Block E: collision shapes, player physics, digging.
#include "mcpp/world/physics.hpp"
#include "mcpp/world/shapes.hpp"

#include <cmath>
#include <cstdlib>
#include <format>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <string_view>
#include <tuple>

namespace {

namespace w = mcpp::world;

int failures = 0;

void check(bool condition, std::string_view what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << '\n';
        ++failures;
    }
}

bool near(double a, double b, double tolerance = 1e-6) { return std::abs(a - b) <= tolerance; }

// The union's extent along one axis, in sixteenths.
std::pair<double, double> extent(const std::vector<w::Box>& boxes, int axis) {
    double lo = 1e9;
    double hi = -1e9;
    for (const auto& b : boxes) {
        lo = std::min(lo, b.min[axis] * 16.0);
        hi = std::max(hi, b.max[axis] * 16.0);
    }
    return {lo, hi};
}

bool solid_at(const std::vector<w::Box>& boxes, double x, double y, double z) {
    for (const auto& b : boxes) {
        if (x > b.min[0] * 16 && x < b.max[0] * 16 && y > b.min[1] * 16 && y < b.max[1] * 16 &&
            z > b.min[2] * 16 && z < b.max[2] * 16) {
            return true;
        }
    }
    return false;
}

void test_shape_families() {
    using P = w::Properties;
    {
        const auto bottom = w::family_shape("slab", P{{"type", "bottom"}});
        const auto top = w::family_shape("slab", P{{"type", "top"}});
        const auto both = w::family_shape("slab", P{{"type", "double"}});
        check(extent(bottom, 1) == std::pair{0.0, 8.0}, "a bottom slab is half a block high");
        check(extent(top, 1) == std::pair{8.0, 16.0}, "a top slab fills the upper half");
        check(extent(both, 1) == std::pair{0.0, 16.0}, "a double slab is a full block");
    }
    {
        const auto straight = w::family_shape(
            "stair", P{{"facing", "north"}, {"half", "bottom"}, {"shape", "straight"}});
        check(solid_at(straight, 8, 12, 4) && !solid_at(straight, 8, 12, 12) &&
                  solid_at(straight, 8, 4, 12),
              "a stair facing north climbs towards the north");
        const auto east = w::family_shape(
            "stair", P{{"facing", "east"}, {"half", "bottom"}, {"shape", "straight"}});
        check(solid_at(east, 12, 12, 8) && !solid_at(east, 4, 12, 8),
              "and facing east, towards the east");
        const auto outer = w::family_shape(
            "stair", P{{"facing", "north"}, {"half", "bottom"}, {"shape", "outer_left"}});
        check(solid_at(outer, 4, 12, 4) && !solid_at(outer, 12, 12, 4) &&
                  !solid_at(outer, 4, 12, 12),
              "an outer left corner facing north keeps only the north-west quarter");
        const auto inner = w::family_shape(
            "stair", P{{"facing", "north"}, {"half", "bottom"}, {"shape", "inner_right"}});
        check(solid_at(inner, 4, 12, 4) && solid_at(inner, 12, 12, 12) &&
                  !solid_at(inner, 4, 12, 12),
              "an inner right corner adds the south-east quarter");
        const auto upside = w::family_shape(
            "stair", P{{"facing", "north"}, {"half", "top"}, {"shape", "straight"}});
        check(solid_at(upside, 8, 12, 12) && solid_at(upside, 8, 4, 4) &&
                  !solid_at(upside, 8, 4, 12),
              "an upside-down stair hangs its step below");
    }
    {
        const auto post = w::family_shape(
            "fence", P{{"north", "false"}, {"east", "false"}, {"south", "false"}, {"west", "false"}});
        check(extent(post, 1).second == 24.0, "a fence is a block and a half high");
        check(extent(post, 0) == std::pair{6.0, 10.0}, "and a post four sixteenths wide");
        const auto arm = w::family_shape(
            "fence", P{{"north", "true"}, {"east", "false"}, {"south", "false"}, {"west", "false"}});
        check(extent(arm, 2) == std::pair{0.0, 10.0}, "a north arm reaches the north edge");
        const auto pane = w::family_shape(
            "pane", P{{"north", "false"}, {"east", "true"}, {"south", "false"}, {"west", "true"}});
        check(extent(pane, 0) == std::pair{0.0, 16.0} && extent(pane, 2) == std::pair{7.0, 9.0} &&
                  extent(pane, 1).second == 16.0,
              "a pane joined east and west is a sheet two sixteenths thick");
        const auto wall = w::family_shape(
            "wall", P{{"up", "true"}, {"north", "low"}, {"south", "none"}, {"east", "none"},
                      {"west", "tall"}});
        check(extent(wall, 1).second == 24.0 && solid_at(wall, 8, 20, 1) &&
                  solid_at(wall, 1, 20, 8) && !solid_at(wall, 15, 20, 8),
              "a wall stops a jump on every side it reaches");
    }
    {
        const auto gate = w::family_shape("fence_gate", P{{"facing", "north"}, {"open", "false"}});
        check(extent(gate, 1).second == 24.0 && extent(gate, 2) == std::pair{6.0, 10.0},
              "a closed gate facing north spans east to west");
        check(w::family_shape("fence_gate", P{{"facing", "north"}, {"open", "true"}}).empty(),
              "an open gate lets you through");
        const auto door = w::family_shape("door", P{{"facing", "east"}, {"open", "false"},
                                                    {"hinge", "left"}});
        check(extent(door, 0) == std::pair{0.0, 3.0}, "a closed door facing east stands on the west");
        const auto open = w::family_shape("door", P{{"facing", "east"}, {"open", "true"},
                                                    {"hinge", "left"}});
        check(extent(open, 2) == std::pair{0.0, 3.0}, "and swung open on a left hinge, on the north");
        const auto right = w::family_shape("door", P{{"facing", "east"}, {"open", "true"},
                                                     {"hinge", "right"}});
        check(extent(right, 2) == std::pair{13.0, 16.0}, "and on a right hinge, on the south");
        const auto hatch = w::family_shape("trapdoor", P{{"facing", "north"}, {"open", "false"},
                                                         {"half", "bottom"}});
        check(extent(hatch, 1) == std::pair{0.0, 3.0}, "a shut trapdoor is three sixteenths");
        const auto raised = w::family_shape("trapdoor", P{{"facing", "north"}, {"open", "true"},
                                                          {"half", "bottom"}});
        check(extent(raised, 2) == std::pair{13.0, 16.0}, "and open, a wall on the south side");
        const auto ladder = w::family_shape("ladder", P{{"facing", "west"}});
        check(extent(ladder, 0) == std::pair{13.0, 16.0}, "a ladder facing west hangs on the east");
    }
    {
        check(w::family_shape("snow", P{{"layers", "1"}}).empty(),
              "one layer of snow is walked through");
        check(extent(w::family_shape("snow", P{{"layers", "8"}}), 1).second == 14.0,
              "eight layers stop two sixteenths short of a block");
        check(extent(w::family_shape("cake", P{{"bites", "3"}}), 0).first == 7.0,
              "a cake gets shorter by two sixteenths a bite");
        check(extent(w::family_shape("box:0,0,0,16,14,16", {}), 1).second == 14.0,
              "a fixed box reads as written");
        check(w::family_shape("box:0,0,0,16,1.5,16", {}).front().max[1] == 1.5 / 16.0,
              "in fractions of a sixteenth too");
        const auto rod = w::family_shape("rod", P{{"facing", "east"}});
        check(extent(rod, 0) == std::pair{0.0, 16.0} && extent(rod, 1) == std::pair{6.0, 10.0},
              "a rod pointing east lies along x");
        const auto head = w::family_shape("piston_head", P{{"facing", "north"}, {"short", "false"}});
        check(extent(head, 2).first == 0.0 && solid_at(head, 8, 8, 2), "a piston head facing north");
        bool known = true;
        w::family_shape("no_such_family", {}, &known);
        check(!known, "an unknown family says so");
    }
    {
        double dx = 0.0;
        double dz = 0.0;
        double was_x = 0.0;
        bool varies = false;
        for (int i = 0; i < 20; ++i) {
            w::random_offset(i * 7 - 50, i * 13 + 3, 0.25f, dx, dz);
            check(std::abs(dx) <= 0.25 && std::abs(dz) <= 0.25, "an offset stays inside its block");
            if (i > 0 && dx != was_x) varies = true;
            was_x = dx;
        }
        check(varies, "and differs from block to block");
    }
}

// A small world: each name a state, laid out by a function.
struct TestWorld {
    w::BlockShapes shapes;
    std::map<std::string, int32_t, std::less<>> ids;
    int32_t next = 0;

    int32_t add(std::string_view name, w::Properties properties = {}) {
        const int32_t id = next++;
        shapes.define(id, name, properties);
        ids[std::string(name) + std::to_string(id)] = id;
        return id;
    }
};

struct Run {
    w::Body body;
    w::Intent intent;
};

void ticks(Run& run, int count, const w::BlockAt& at, const w::BlockShapes& shapes) {
    for (int i = 0; i < count; ++i) w::tick(run.body, run.intent, at, shapes);
}

Run standing(double x, double y, double z) {
    Run run;
    run.body.x = x;
    run.body.y = y;
    run.body.z = z;
    run.body.on_ground = true;
    run.intent.yaw_degrees = 270.0f;   // east, +x
    return run;
}

void test_movement(TestWorld& t) {
    const int32_t air = t.add("minecraft:air");
    const int32_t stone = t.add("minecraft:stone");
    const auto& shapes = t.shapes;
    const auto flat = [=](int32_t, int32_t y, int32_t) { return y < 64 ? stone : air; };

    // Jumping: 0.42 up, then gravity and drag, measured tick by tick.
    {
        Run run = standing(0.5, 64.0, 0.5);
        run.intent.jump = true;
        w::tick(run.body, run.intent, flat, shapes);
        run.intent.jump = false;
        double vy = 0.42;
        double y = 64.0 + 0.42;
        check(near(run.body.y, y), std::format("a jump rises 0.42 in its first tick: {:.5f}", run.body.y - 64));
        double apex = run.body.y;
        int apex_tick = 1;
        int landed = -1;
        for (int i = 2; i <= 20; ++i) {
            vy = (vy - 0.08) * 0.98;
            w::tick(run.body, run.intent, flat, shapes);
            if (landed < 0 && run.body.y > 64.0) {
                y += vy;
                check(near(run.body.y, y, 1e-9),
                      std::format("jump tick {}: y {:.5f}, expected {:.5f}", i, run.body.y, y));
            }
            if (run.body.y > apex) {
                apex = run.body.y;
                apex_tick = i;
            }
            if (landed < 0 && run.body.on_ground) landed = i;
        }
        check(near(apex - 64.0, 1.2522, 0.0001),
              std::format("a jump tops out at 1.2522 blocks: {:.4f}", apex - 64.0));
        check(apex_tick == 6, std::format("six ticks up: {}", apex_tick));
        check(landed == 12, std::format("and lands on the twelfth tick: {}", landed));
        std::cout << std::format("  jump: apex {:.4f} at tick {}, back on the ground at tick {}\n",
                                 apex - 64.0, apex_tick, landed);
    }

    // Walking and sprinting from standing, and coming to a stop.
    for (const bool sprint : {false, true}) {
        Run run = standing(0.5, 64.0, 0.5);
        run.intent.forward = 1.0;
        run.intent.sprint = sprint;
        const double top = (sprint ? 0.13 : 0.1) * 0.98 / (1.0 - 0.546);
        int to_speed = -1;
        double last_x = run.body.x;
        for (int i = 1; i <= 60; ++i) {
            w::tick(run.body, run.intent, flat, shapes);
            const double moved = run.body.x - last_x;
            last_x = run.body.x;
            if (to_speed < 0 && moved >= top * 0.99) to_speed = i;
        }
        const double speed = (run.body.x - run.body.previous_x) * 20.0;
        check(near(speed, top * 20.0, 1e-4),
              std::format("{} settles at {:.3f} blocks a second", sprint ? "sprinting" : "walking", speed));
        check(sprint == run.body.sprinting, "sprinting is on exactly when asked for");
        run.intent.forward = 0.0;
        const double from = run.body.x;
        int stopped = -1;
        for (int i = 1; i <= 40 && stopped < 0; ++i) {
            w::tick(run.body, run.intent, flat, shapes);
            if (run.body.x == run.body.previous_x) stopped = i;
        }
        std::cout << std::format(
            "  {}: {:.3f} b/s, 99% of it after {} ticks; stops {} ticks after the key, "
            "{:.3f} blocks on\n",
            sprint ? "sprint" : "walk", speed, to_speed, stopped - 1, run.body.x - from);
        check(!run.body.sprinting, "letting go of forward ends the sprint");
        check(stopped >= 0 && stopped <= 10, "and the body stops within half a second");
    }

    // A sprint jump lands further than sprinting covers in the same time.
    {
        Run run = standing(0.5, 64.0, 0.5);
        run.intent.forward = 1.0;
        run.intent.sprint = true;
        ticks(run, 40, flat, shapes);
        const double start = run.body.x;
        run.intent.jump = true;
        ticks(run, 120, flat, shapes);
        const double speed = (run.body.x - start) / 6.0;
        check(speed > 6.9 && speed < 7.2,
              std::format("sprint jumping averages {:.3f} blocks a second", speed));
        std::cout << std::format("  sprint jump: {:.3f} b/s over six seconds\n", speed);
    }

    // Sneaking: slower, and it will not walk you off an edge.
    {
        const auto ledge = [=](int32_t x, int32_t y, int32_t) {
            return (y < 64 && x <= 0) || y < 60 ? stone : air;
        };
        Run run = standing(0.5, 64.0, 0.5);
        run.intent.forward = 1.0;
        run.intent.sneak = true;
        ticks(run, 100, ledge, shapes);
        check(run.body.on_ground && near(run.body.y, 64.0) && run.body.x < 1.3 + 1e-9 &&
                  run.body.x > 1.2,
              std::format("sneaking stops at the edge: x {:.3f} y {:.3f}", run.body.x, run.body.y));
        check(run.body.pose == w::Pose::Crouching, "and crouches");
        Run open = standing(0.5, 64.0, 0.5);
        open.intent.forward = 1.0;
        open.intent.sneak = true;
        ticks(open, 60, flat, shapes);
        const double speed = (open.body.x - open.body.previous_x) * 20.0;
        check(near(speed, 0.1 * 0.3 * 0.98 / 0.454 * 20.0, 1e-3),
              std::format("sneaking walks at {:.3f} blocks a second", speed));
    }

    // A low ceiling: standing up under a slab-high gap keeps you crouched.
    {
        const auto tunnel = [=](int32_t, int32_t y, int32_t) {
            return y < 64 || y == 65 + 0 ? stone : air;
        };
        Run run = standing(0.5, 64.0, 0.5);
        ticks(run, 2, tunnel, shapes);
        check(run.body.pose == w::Pose::Swimming,
              "a one block gap leaves only the swimming pose");
    }
}

void test_shapes_underfoot(TestWorld& t) {
    const int32_t air = t.add("minecraft:air");
    const int32_t stone = t.add("minecraft:stone");
    const int32_t slab = t.add("minecraft:oak_slab", {{"type", "bottom"}});
    const int32_t stair = t.add("minecraft:oak_stairs", {{"facing", "east"}, {"half", "bottom"},
                                                         {"shape", "straight"}});
    const int32_t fence = t.add("minecraft:oak_fence", {{"north", "true"}, {"east", "false"},
                                                        {"south", "true"}, {"west", "false"}});
    const int32_t carpet = t.add("minecraft:white_carpet");
    const int32_t snow = t.add("minecraft:snow", {{"layers", "5"}});
    const int32_t deep_snow = t.add("minecraft:snow", {{"layers", "8"}});
    const int32_t grass = t.add("minecraft:short_grass");
    const auto& shapes = t.shapes;

    // A row of things to walk east into, one per x, on a stone floor.
    const auto course = [=](int32_t lane) {
        return [=](int32_t x, int32_t y, int32_t) {
            if (y < 64) return stone;
            if (y == 64 && x >= 3) return lane;
            return air;
        };
    };
    struct Case {
        const char* what;
        int32_t state;
        double height;   // where the feet should end up, or -1 for "stopped"
    };
    for (const Case c : {Case{"a slab is stepped onto", slab, 64.5},
                         Case{"a row of stairs, from below, up to its steps", stair, 65.0},
                         Case{"a carpet", carpet, 64.0625},
                         Case{"five layers of snow", snow, 64.5},
                         Case{"eight layers are too deep to step", deep_snow, -1.0},
                         Case{"grass is walked through", grass, 64.0},
                         Case{"a stone block stops you", stone, -1.0},
                         Case{"and so does a fence", fence, -1.0}}) {
        Run run = standing(0.5, 64.0, 0.5);
        run.intent.forward = 1.0;
        const auto at = course(c.state);
        double highest = run.body.y;
        for (int i = 0; i < 25; ++i) {
            w::tick(run.body, run.intent, at, shapes);
            highest = std::max(highest, run.body.y);
        }
        if (c.height < 0) {
            const double face = c.state == fence ? 3.375 : 3.0;
            check(near(run.body.x, face - 0.3, 1e-6) && near(highest, 64.0),
                  std::format("{}: x {:.4f}", c.what, run.body.x));
        } else {
            check(run.body.x > 3.6 && near(highest, c.height, 1e-6) && run.body.on_ground,
                  std::format("{}: x {:.3f} y {:.4f}", c.what, run.body.x, highest));
        }
    }

    // A staircase, block by block: walking straight up it.
    {
        const auto stairs = [=](int32_t x, int32_t y, int32_t) {
            if (y < 64) return stone;
            const int32_t step = x - 2;
            if (step >= 0 && y - 64 == step) return stair;
            if (step >= 0 && y - 64 < step) return stone;
            return air;
        };
        Run run = standing(0.5, 64.0, 0.5);
        run.intent.forward = 1.0;
        ticks(run, 80, stairs, shapes);
        check(run.body.y > 68.0 && run.body.x > 7.0,
              std::format("a staircase is walked up without jumping: x {:.2f} y {:.2f}",
                          run.body.x, run.body.y));
    }

    // A fence cannot be jumped: its top is a block and a half up.
    {
        const auto at = course(fence);
        Run run = standing(0.5, 64.0, 0.5);
        run.intent.forward = 1.0;
        run.intent.jump = true;
        ticks(run, 60, at, shapes);
        check(run.body.x < 3.1, std::format("a fence holds a jumping player back: x {:.3f}", run.body.x));
        Run wall = standing(0.5, 64.0, 0.5);
        wall.intent.forward = 1.0;
        wall.intent.jump = true;
        ticks(wall, 60, course(stone), shapes);
        check(wall.body.x > 3.5, "a block is jumped onto");
    }

    // Standing on a slab: on the ground, half a block up.
    {
        const auto at = [=](int32_t, int32_t y, int32_t) {
            return y < 64 ? stone : (y == 64 ? slab : air);
        };
        Run run = standing(0.5, 66.0, 0.5);
        run.body.on_ground = false;
        ticks(run, 40, at, shapes);
        check(run.body.on_ground && near(run.body.y, 64.5), "a body falls onto a slab and stands on it");
    }
}

void test_surfaces(TestWorld& t) {
    const int32_t air = t.add("minecraft:air");
    const int32_t stone = t.add("minecraft:stone");
    const int32_t ice = t.add("minecraft:ice");
    const int32_t blue_ice = t.add("minecraft:blue_ice");
    const int32_t soul_sand = t.add("minecraft:soul_sand");
    const int32_t slime = t.add("minecraft:slime_block");
    const int32_t honey = t.add("minecraft:honey_block");
    const int32_t cobweb = t.add("minecraft:cobweb");
    const auto& shapes = t.shapes;

    const auto floor_of = [=](int32_t block) {
        return [=](int32_t, int32_t y, int32_t) { return y < 63 ? stone : (y == 63 ? block : air); };
    };
    const auto settle = [&](int32_t block, bool sprint) {
        const double top = block == soul_sand ? 63.875 : (block == honey ? 63.9375 : 64.0);
        Run run = standing(0.5, top, 0.5);
        run.intent.forward = 1.0;
        run.intent.sprint = sprint;
        ticks(run, 200, floor_of(block), shapes);
        return std::pair{(run.body.x - run.body.previous_x) * 20.0, run};
    };
    const auto [stone_speed, on_stone] = settle(stone, false);
    const auto [ice_speed, on_ice] = settle(ice, false);
    const auto [blue_speed, on_blue] = settle(blue_ice, false);
    const auto [soul_speed, on_soul] = settle(soul_sand, false);
    const auto [honey_speed, on_honey] = settle(honey, false);
    // The push grows as the friction does, so the speed a walk settles at
    // hardly changes on ice; what changes is how long it takes to get there
    // and to stop.
    const auto settled = [](double slip, double factor) {
        const double push = 0.1 * 0.98 * 0.21600002 / (slip * slip * slip);
        const double keep = factor * slip * 0.91;
        return push / (1.0 - keep) * 20.0;
    };
    check(near(ice_speed, settled(0.98, 1.0), 1e-3),
          std::format("ice walks at {:.3f} blocks a second", ice_speed));
    check(near(blue_speed, settled(0.989, 1.0), 1e-3),
          std::format("blue ice at {:.3f}", blue_speed));
    const double slowed = 0.1 * 0.98 / (1.0 - 0.4 * 0.546) * 20.0;
    check(near(soul_speed, slowed, 1e-3), std::format("soul sand at {:.3f}", soul_speed));
    check(near(honey_speed, slowed, 1e-3), std::format("and honey at {:.3f}", honey_speed));
    std::cout << std::format("  surfaces: stone {:.3f}, ice {:.3f}, blue ice {:.3f}, soul sand "
                             "{:.3f}, honey {:.3f}\n",
                             stone_speed, ice_speed, blue_speed, soul_speed, honey_speed);

    // Letting go on ice: the slide.
    {
        auto run = on_ice;
        run.intent.forward = 0.0;
        const double from = run.body.x;
        ticks(run, 100, floor_of(ice), shapes);
        auto still = on_stone;
        still.intent.forward = 0.0;
        const double from_stone = still.body.x;
        ticks(still, 100, floor_of(stone), shapes);
        check(run.body.x - from > (still.body.x - from_stone) * 5.0,
              std::format("ice slides {:.2f} blocks, stone {:.2f}", run.body.x - from,
                          still.body.x - from_stone));
        std::cout << std::format("  coasting: stone {:.3f} blocks, ice {:.3f}\n",
                                 still.body.x - from_stone, run.body.x - from);
    }

    // Honey: half a jump.
    {
        Run run = standing(0.5, 63.9375, 0.5);
        run.intent.jump = true;
        w::tick(run.body, run.intent, floor_of(honey), shapes);
        check(near(run.body.y - 63.9375, 0.21), std::format("honey halves a jump: {:.4f}", run.body.y - 63.9375));
    }

    // Slime: you come back up from a fall.
    {
        Run run = standing(0.5, 70.0, 0.5);
        run.body.on_ground = false;
        double lowest = 70.0;
        double after = 0.0;
        for (int i = 0; i < 40; ++i) {
            w::tick(run.body, run.intent, floor_of(slime), shapes);
            lowest = std::min(lowest, run.body.y);
            if (lowest < 64.01) after = std::max(after, run.body.y);
        }
        check(after > 67.0, std::format("a slime block bounces a fall back up to {:.2f}", after));
        Run quiet = standing(0.5, 70.0, 0.5);
        quiet.body.on_ground = false;
        quiet.intent.sneak = true;
        for (int i = 0; i < 40; ++i) w::tick(quiet.body, quiet.intent, floor_of(slime), shapes);
        check(quiet.body.on_ground && near(quiet.body.y, 64.0), "unless you land sneaking");
    }

    // A cobweb: slow going.
    {
        const auto webbed = [=](int32_t x, int32_t y, int32_t) {
            if (y < 64) return stone;
            if (y == 64 && x >= 1) return cobweb;
            return air;
        };
        Run run = standing(0.5, 64.0, 0.5);
        run.intent.forward = 1.0;
        ticks(run, 100, webbed, shapes);
        const double speed = (run.body.x - run.body.previous_x) * 20.0;
        check(speed < 1.2, std::format("a cobweb holds a walk to {:.3f} blocks a second", speed));
    }
}

void test_water_and_climbing(TestWorld& t) {
    const int32_t air = t.add("minecraft:air");
    const int32_t stone = t.add("minecraft:stone");
    const int32_t water = t.add("minecraft:water", {{"level", "0"}});
    const int32_t ladder = t.add("minecraft:ladder", {{"facing", "west"}, {"waterlogged", "false"}});
    const int32_t lava = t.add("minecraft:lava", {{"level", "0"}});
    const auto& shapes = t.shapes;

    // A pool ten deep.
    const auto pool = [=](int32_t, int32_t y, int32_t) {
        if (y < 50) return stone;
        return y < 60 ? water : air;
    };
    {
        Run run = standing(0.5, 58.0, 0.5);
        run.body.on_ground = false;
        ticks(run, 100, pool, shapes);
        const double sink = (run.body.y - run.body.previous_y) * 20.0;
        check(run.body.in_water && sink < 0.0 && sink > -2.5,
              std::format("water lets you sink slowly: {:.3f} blocks a second", sink));
        run.intent.jump = true;
        ticks(run, 60, pool, shapes);
        check(run.body.y > 59.0, std::format("holding jump swims up to the surface: y {:.2f}", run.body.y));
    }
    {
        Run run = standing(0.5, 55.0, 0.5);
        run.body.on_ground = false;
        run.intent.forward = 1.0;
        ticks(run, 40, pool, shapes);
        const double walk = (run.body.x - run.body.previous_x) * 20.0;
        run.intent.sprint = true;
        ticks(run, 60, pool, shapes);
        const double swim = std::hypot(run.body.x - run.body.previous_x, run.body.y - run.body.previous_y) * 20.0;
        check(run.body.swimming && run.body.pose == w::Pose::Swimming,
              "sprinting under water swims, in the swimming pose");
        check(swim > walk * 1.5, std::format("and is faster: {:.3f} against {:.3f}", swim, walk));
        std::cout << std::format("  water: walk {:.3f} b/s, swim {:.3f} b/s\n", walk, swim);
    }
    {
        const auto in_lava = [=](int32_t, int32_t y, int32_t) {
            if (y < 50) return stone;
            return y < 60 ? lava : air;
        };
        Run run = standing(0.5, 55.0, 0.5);
        run.body.on_ground = false;
        run.intent.forward = 1.0;
        ticks(run, 40, in_lava, shapes);
        check(run.body.in_lava && (run.body.x - run.body.previous_x) * 20.0 < 1.0,
              "lava is thicker still");
    }

    // A ladder up a wall on the east: walking into it climbs.
    {
        const auto wall = [=](int32_t x, int32_t y, int32_t) {
            if (y < 64) return stone;
            if (x == 2) return stone;
            if (x == 1 && y < 74) return ladder;
            return air;
        };
        Run run = standing(0.5, 64.0, 0.5);
        run.intent.forward = 1.0;
        ticks(run, 40, wall, shapes);
        const double climb = (run.body.y - run.body.previous_y) * 20.0;
        check(near(climb, 0.1176 * 20.0, 0.01),
              std::format("a ladder climbs at {:.3f} blocks a second", climb));
        run.intent.forward = 0.0;
        ticks(run, 20, wall, shapes);
        const double down = (run.body.y - run.body.previous_y) * 20.0;
        check(near(down, -3.0, 0.01), std::format("and lets you down at {:.3f}", down));
        run.intent.sneak = true;
        const double held = run.body.y;
        ticks(run, 10, wall, shapes);
        check(near(run.body.y, held, 1e-9), "and holds you where you are while you sneak");
    }
}

} // namespace

int main() {
    test_shape_families();
    TestWorld movement;
    test_movement(movement);
    TestWorld underfoot;
    test_shapes_underfoot(underfoot);
    TestWorld surfaces;
    test_surfaces(surfaces);
    TestWorld water;
    test_water_and_climbing(water);

    if (failures == 0) {
        std::cout << "all player tests passed\n";
        return 0;
    }
    std::cerr << std::format("{} check(s) failed\n", failures);
    return 1;
}
