// Block E: collision shapes, player movement, the mouse and digging.
// Expected numbers are the game's published behaviour (walking 4.317 m/s,
// sprinting 5.612 m/s, a jump 1.2522 blocks high and 12 ticks long, ladders
// 2.35 m/s up), not values read back from this code.
#include "client/player_rules.hpp"
#include "mcpp/world/physics.hpp"
#include "mcpp/world/shapes.hpp"

#include <chrono>
#include <cmath>
#include <format>
#include <iostream>
#include <map>
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

bool near(double a, double b, double tolerance) { return std::abs(a - b) <= tolerance; }

const w::BlockShapes& shapes() { return w::BlockShapes::game(); }

int32_t state(std::string_view name,
              std::initializer_list<std::pair<std::string_view, std::string_view>> set = {}) {
    int32_t s = shapes().find(name, set);
    if (s < 0) std::cerr << "no state " << name << '\n';
    // Dry unless a test asks otherwise: the first value of waterlogged is true.
    if (const int32_t dry = shapes().with(s, "waterlogged", "false"); dry >= 0) s = dry;
    return s;
}

// A flat stone world with its top at y 64, and whatever a test puts on it.
struct TestWorld {
    std::map<std::tuple<int32_t, int32_t, int32_t>, int32_t> blocks;
    int32_t ground = state("minecraft:stone");
    w::BlockTags tags;
    w::BlockAt at = [this](int32_t x, int32_t y, int32_t z) {
        if (const auto found = blocks.find({x, y, z}); found != blocks.end()) return found->second;
        return y < 64 ? ground : 0;
    };
    w::World world() const { return {at, shapes(), &tags, -64}; }
    void set(int32_t x, int32_t y, int32_t z, int32_t s) { blocks[{x, y, z}] = s; }
};

w::Body body_at(double x, double y, double z) {
    w::Body b;
    b.x = b.xo = x;
    b.y = b.yo = y;
    b.z = b.zo = z;
    b.on_ground = true;
    return b;
}

double horizontal_speed(const w::Body& b) {
    return std::hypot(b.x - b.xo, b.z - b.zo);
}

void test_table() {
    check(shapes().size() > 29000, "every block state is in the table");
    const auto& stone = shapes().state(state("minecraft:stone"));
    check(stone.hardness == 1.5f && (stone.flags & w::kNeedsTool) != 0, "stone: 1.5, needs a pickaxe");
    check(shapes().block(state("minecraft:ice")).friction == 0.98f, "ice slides at 0.98");
    check(shapes().block(state("minecraft:soul_sand")).speed == 0.4f, "soul sand slows to 0.4");
    check(shapes().block(state("minecraft:honey_block")).jump == 0.5f, "honey halves a jump");
    const int32_t top = state("minecraft:oak_slab", {{"type", "top"}});
    check(shapes().property(top, "type") == "top", "a slab's half reads back");
    const auto& v = shapes().voxels(shapes().state(top).collision);
    check(v.top == 1.0 && shapes().edges(v, 1)[0] == 0.0, "a top slab's cells reach the top");
    const int32_t fence = state("minecraft:oak_fence");
    check(shapes().voxels(shapes().state(fence).collision).top == 1.5,
          "a fence collides to a block and a half");
    check((shapes().state(fence).flags & w::kLargeCollision) != 0, "and reaches out of its cell");
    const int32_t water = state("minecraft:water");
    check(shapes().state(water).fluid == w::Fluid::Water && shapes().state(water).fluid_amount == 8,
          "a water source holds eight eighths");
}

void test_mouse() {
    const double scale = client::mouse_scale(0.5);
    w::Body b;
    client::turn(b, 100.0 * scale, 0.0);
    check(near(b.yaw, 15.0, 1e-4), "at the default sensitivity a pixel turns 0.15 degrees");
    client::turn(b, 0.0, 1000.0 * scale);
    check(b.pitch == 90.0f, "looking down stops at 90 degrees");
    check(near(client::mouse_scale(1.0) * 0.15, 0.6 * 0.8 * 0.8 * 0.8 * 8.0 * 0.15 * 1.0, 1e-6) ||
              near(client::mouse_scale(1.0), 4.096, 1e-5),
          "the curve is cubic in the sensitivity");
}

void test_walking() {
    TestWorld t;
    w::Body b = body_at(0.5, 64.0, 0.5);
    w::Keys keys;
    keys.forward = true;
    const w::Stats stats;
    int to_full = -1;
    for (int i = 0; i < 40; ++i) {
        w::tick(b, keys, stats, t.world());
        if (to_full < 0 && horizontal_speed(b) > 0.2158 * 0.99) to_full = i + 1;
    }
    check(near(horizontal_speed(b) * 20.0, 4.317, 0.005), "walking settles at 4.317 m/s");
    check(b.y == 64.0 && b.on_ground, "and stays on the ground");
    keys.forward = false;
    int to_stop = -1;
    for (int i = 0; i < 20; ++i) {
        w::tick(b, keys, stats, t.world());
        if (to_stop < 0 && horizontal_speed(b) == 0.0) to_stop = i + 1;
    }
    check(to_stop == 8, "letting go stops in 8 ticks, friction 0.546 a tick");
    std::cout << std::format("walking: {} ticks to 99% speed, {} ticks to standstill\n", to_full,
                             to_stop);

    w::Body s = body_at(0.5, 64.0, 0.5);
    keys = {};
    keys.forward = true;
    keys.sprint = true;
    int sprint_full = -1;
    for (int i = 0; i < 40; ++i) {
        w::tick(s, keys, stats, t.world());
        if (sprint_full < 0 && horizontal_speed(s) > 0.2806 * 0.99) sprint_full = i + 1;
    }
    check(s.sprinting, "the sprint key sprints");
    check(near(horizontal_speed(s) * 20.0, 5.612, 0.005), "sprinting settles at 5.612 m/s");
    std::cout << std::format("sprinting: {} ticks to 99% speed\n", sprint_full);
}

void test_jump() {
    TestWorld t;
    w::Body b = body_at(0.5, 64.0, 0.5);
    w::Keys keys;
    keys.jump = true;
    const w::Stats stats;
    double apex = 0.0;
    int airtime = 0;
    w::tick(b, keys, stats, t.world());
    keys.jump = false;
    for (int i = 0; i < 40 && !b.on_ground; ++i) {
        apex = std::max(apex, b.y - 64.0);
        ++airtime;
        w::tick(b, keys, stats, t.world());
    }
    check(near(apex, 1.2522, 0.0001), "a jump rises 1.2522 blocks");
    check(airtime + 1 == 12, "and lands 12 ticks after it left");
    std::cout << std::format("jump: apex {:.4f}, {} ticks in the air\n", apex, airtime + 1);
}

void test_steps_and_walls() {
    TestWorld t;
    for (int z = 1; z < 6; ++z) t.set(0, 64, z, state("minecraft:oak_slab", {{"type", "bottom"}}));
    t.set(0, 64, 6, state("minecraft:stone"));
    t.set(0, 65, 6, state("minecraft:stone"));
    t.set(0, 66, 6, state("minecraft:stone"));
    w::Body b = body_at(0.5, 64.0, 0.5);
    w::Keys keys;
    keys.forward = true;
    const w::Stats stats;
    for (int i = 0; i < 30; ++i) w::tick(b, keys, stats, t.world());
    check(b.y == 64.5, "walking onto a slab steps up half a block");
    check(near(b.z, 6.0 - 0.30000001192092896, 1e-9), "and a full block stops the body at its face");

    TestWorld f;
    f.set(0, 64, 2, state("minecraft:oak_fence"));
    w::Body c = body_at(0.5, 64.0, 0.5);
    keys.jump = true;
    for (int i = 0; i < 30; ++i) w::tick(c, keys, stats, f.world());
    check(c.z < 2.0, "a fence cannot be jumped");
}

void test_sneak_edge() {
    TestWorld t;
    for (int z = 0; z < 2; ++z) t.set(0, 64, z, state("minecraft:stone"));
    // A ledge one block up, ending at z 2, with a drop beyond it.
    w::Body b = body_at(0.5, 65.0, 0.5);
    w::Keys keys;
    keys.forward = true;
    keys.shift = true;
    const w::Stats stats;
    for (int i = 0; i < 60; ++i) w::tick(b, keys, stats, t.world());
    check(b.y == 65.0, "sneaking does not walk off a ledge");
    check(b.z > 2.0 && b.z < 2.0 + 0.31, "it stops with the box just over the edge");
}

void test_ice() {
    TestWorld t;
    t.ground = state("minecraft:ice");
    w::Body b = body_at(0.5, 64.0, 0.5);
    w::Keys keys;
    keys.forward = true;
    const w::Stats stats;
    for (int i = 0; i < 60; ++i) w::tick(b, keys, stats, t.world());
    keys.forward = false;
    int slide = 0;
    for (; slide < 200 && horizontal_speed(b) > 0.0; ++slide) w::tick(b, keys, stats, t.world());
    check(slide >= 30, "ice keeps you sliding long after you let go");
    std::cout << std::format("ice: {} ticks sliding after letting go\n", slide);
}

void test_water() {
    TestWorld t;
    const int32_t water = state("minecraft:water");
    for (int x = -3; x <= 3; ++x) {
        for (int z = -3; z <= 30; ++z) {
            for (int y = 60; y < 64; ++y) t.set(x, y, z, water);
        }
    }
    w::Body b = body_at(0.5, 61.0, 0.5);
    b.on_ground = false;
    w::Keys keys;
    const w::Stats stats;
    w::tick(b, keys, stats, t.world());
    check(b.touching_water, "a body in water knows it");
    const double y0 = b.y;
    for (int i = 0; i < 20; ++i) w::tick(b, keys, stats, t.world());
    check(b.y < y0, "and slowly sinks");
    keys.forward = true;
    keys.sprint = true;
    for (int i = 0; i < 20; ++i) w::tick(b, keys, stats, t.world());
    check(b.swimming && b.pose == w::Pose::Swimming, "sprinting under water swims");
}

void test_ladder() {
    TestWorld t;
    t.tags.bits.assign(shapes().size(), 0);
    const int32_t ladder = state("minecraft:ladder", {{"facing", "south"}});
    t.tags.bits[shapes().state(ladder).block] |= w::BlockTags::kClimbable;
    for (int y = 64; y < 80; ++y) {
        t.set(0, y, 0, ladder);
        t.set(0, y, -1, state("minecraft:stone"));
    }
    w::Body b = body_at(0.5, 64.0, 0.5);
    b.yaw = 180.0f;
    w::Keys keys;
    keys.forward = true;
    const w::Stats stats;
    for (int i = 0; i < 30; ++i) w::tick(b, keys, stats, t.world());
    const double per_tick = b.y - b.yo;
    check(near(per_tick * 20.0, 2.352, 0.01), "a ladder climbs at 2.35 m/s");
}

void test_dig() {
    client::DigContext hand;
    const float stone = client::destroy_progress(1.5f, true, hand);
    check(near(1.0 / stone, 150.0, 0.5), "stone by hand takes 150 ticks");
    client::DigContext pick;
    pick.tool_speed = 2.0f;
    pick.correct_tool = true;
    check(near(1.0 / client::destroy_progress(1.5f, true, pick), 22.5, 0.01),
          "and 22.5 with a wooden pickaxe");
    pick.on_ground = false;
    check(near(1.0 / client::destroy_progress(1.5f, true, pick), 112.5, 0.05),
          "five times that off the ground");
}

void test_speed() {
    TestWorld t;
    for (int x = -8; x <= 8; ++x) t.set(x, 64, 3, state("minecraft:oak_fence"));
    w::Body b = body_at(0.5, 64.0, 0.5);
    w::Keys keys;
    keys.forward = true;
    keys.sprint = true;
    keys.jump = true;
    const w::Stats stats;
    constexpr int kTicks = 20000;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < kTicks; ++i) {
        if (i % 50 == 0) {
            b = body_at(0.5, 64.0, 0.5);
            b.yaw = static_cast<float>(i % 360);
        }
        w::tick(b, keys, stats, t.world());
    }
    const double us =
        std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() /
        kTicks;
    std::cout << std::format("one movement tick: {:.2f} us\n", us);
}

} // namespace

int main() {
    test_table();
    test_mouse();
    test_walking();
    test_jump();
    test_steps_and_walls();
    test_sneak_edge();
    test_ice();
    test_water();
    test_ladder();
    test_dig();
    test_speed();
    if (failures > 0) {
        std::cerr << failures << " failed\n";
        return 1;
    }
    std::cout << "player: all passed\n";
    return 0;
}
