// B: the parts of breaking a block the oracle does not reach: which crack a
// block shows when several people dig it, when a crack is forgotten, and what
// the particle pool does when it overflows.
#include "mcpp/world/destruction.hpp"
#include "mcpp/world/particles.hpp"

#include <format>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

using namespace mcpp::world;

int failures = 0;

void check(bool condition, std::string_view what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << '\n';
        ++failures;
    }
}

const double kHere[3] = {0.5, 0.5, 0.5};

void test_destruction_stages() {
    BlockDestruction cracks;
    const int32_t block[3] = {1, 2, 3};
    std::vector<BlockDestruction::Shown> shown;

    cracks.progress(7, block, 4);
    cracks.shown(kHere, shown);
    check(shown.size() == 1 && shown[0].stage == 4, "one breaker shows its own stage");

    // The game keeps every breaker's crack and draws the deepest.
    cracks.progress(8, block, 9);
    cracks.shown(kHere, shown);
    check(shown.size() == 1 && shown[0].stage == 9, "the deepest crack of two breakers wins");

    cracks.progress(8, block, -1);
    cracks.shown(kHere, shown);
    check(shown.size() == 1 && shown[0].stage == 4, "a breaker that stopped leaves the other's");

    cracks.progress(7, block, 10);
    cracks.shown(kHere, shown);
    check(shown.empty(), "a stage of ten is past the last texture and shows nothing");

    // One breaker breaks one block: moving on leaves nothing behind.
    cracks.progress(7, block, 2);
    const int32_t next[3] = {1, 2, 4};
    cracks.progress(7, next, 2);
    cracks.shown(kHere, shown);
    check(shown.size() == 1 && shown[0].pos[2] == 4, "a breaker moving on takes its crack with it");
}

void test_destruction_distance() {
    BlockDestruction cracks;
    std::vector<BlockDestruction::Shown> shown;
    // The game draws a crack whose block centre is within 1024 of the camera
    // squared, so 32 blocks away still counts and 33 does not.
    const int32_t near_block[3] = {32, 0, 0};
    const int32_t far_block[3] = {33, 0, 0};
    cracks.progress(1, near_block, 5);
    cracks.progress(2, far_block, 5);
    const double eye[3] = {0.5, 0.5, 0.5};
    cracks.shown(eye, shown);
    check(shown.size() == 1 && shown[0].pos[0] == 32, "the block 1024 away squared is drawn, the one past it is not");
}

void test_destruction_cleanup() {
    BlockDestruction cracks;
    const int32_t block[3] = {0, 0, 0};
    std::vector<BlockDestruction::Shown> shown;
    cracks.progress(1, block, 3);
    for (int i = 0; i < 400; ++i) cracks.tick();
    cracks.shown(kHere, shown);
    check(shown.size() == 1, "a crack still stands after 400 ticks");
    // The sweep runs every twenty ticks, so it goes on the next multiple.
    for (int i = 0; i < 20; ++i) cracks.tick();
    cracks.shown(kHere, shown);
    check(shown.empty(), "a crack nobody touched for over 400 ticks is forgotten");

    cracks.progress(1, block, 3);
    for (int i = 0; i < 1000; ++i) {
        cracks.tick();
        if (i % 100 == 0) cracks.progress(1, block, 3);
    }
    cracks.shown(kHere, shown);
    check(shown.size() == 1, "a crack kept up to date is never forgotten");
}

void test_particle_limit() {
    ParticleEngine engine;
    const BlockAt air = [](int32_t, int32_t, int32_t) { return 0; };
    const ParticleTable table;
    const ParticleLevel level{air, BlockShapes::game(), table,
                              [](int32_t, int32_t, int32_t) { return 0; },
                              [](ParticleTable::Colour, int32_t, int32_t, int32_t) { return 0u; }, -64};
    const size_t over = ParticleEngine::kGroupLimit + 100;
    for (size_t i = 0; i < over; ++i) {
        Particle p;
        p.lifetime = 1000;
        p.has_physics = false;
        p.set_pos(static_cast<double>(i), 0.0, 0.0);
        engine.add(p);
    }
    engine.tick(level);
    check(engine.count() == ParticleEngine::kGroupLimit, "the pool holds the game's 16384 and no more");
    double first = -1.0;
    engine.each([&](const Particle& p) {
        if (first < 0.0) first = p.x;
    });
    check(first == 100.0, "the oldest hundred were pushed out, not the newest");

    engine.clear();
    check(engine.count() == 0, "clearing empties the pool");

    Particle dying;
    dying.lifetime = 2;
    dying.has_physics = false;
    engine.add(dying);
    // A new particle waits for the end of the tick it was made in, as the
    // game's queue does, and is first ticked the tick after.
    engine.tick(level);
    for (int i = 0; i < dying.lifetime; ++i) engine.tick(level);
    check(engine.count() == 1, "a particle lives out its lifetime");
    engine.tick(level);
    check(engine.count() == 0, "and is let go the tick after");
}

} // namespace

int main() {
    test_destruction_stages();
    test_destruction_distance();
    test_destruction_cleanup();
    test_particle_limit();
    if (failures == 0) {
        std::cout << "all breaking tests passed\n";
        return 0;
    }
    std::cerr << std::format("{} check(s) failed\n", failures);
    return 1;
}
