// J: the parts of the particle types the oracle does not reach: the sprite
// table, the type names, the gaussian the level_particles cloud scatters by,
// and the counts the level events show.
#include "mcpp/jmath.hpp"
#include "mcpp/world/particles.hpp"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

namespace {

using namespace mcpp::world;

int failures = 0;

void check(bool condition, std::string_view what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << '\n';
        ++failures;
    }
}

std::filesystem::path scratch_file(const char* name) {
    const char* base = std::getenv("TMPDIR");
    return std::filesystem::path(base != nullptr ? base : "/tmp") / name;
}

void test_sprite_table() {
    const auto path = scratch_file("plasma-test-sprites.txt");
    {
        std::ofstream out(path);
        // Two sets over three textures, the second sharing the first's, the
        // way portal and reverse_portal share generic_0 in the game's own.
        out << "T minecraft:particle/a\nT minecraft:particle/b\nT minecraft:particle/c\n";
        out << "S minecraft:one 0 1 2\nS minecraft:two 2 0\n";
    }
    ParticleSprites sprites;
    check(sprites.open(path, 100), "the table opens");
    std::filesystem::remove(path);

    const int32_t one = sprites.find("minecraft:one");
    const int32_t two = sprites.find("minecraft:two");
    check(one >= 0 && two >= 0, "both sets are found by name");
    check(sprites.find("minecraft:missing") < 0, "a set the game has not got is -1");
    check(sprites.size(one) == 3 && sprites.size(two) == 2, "each set has its own size");
    // The layer is the block texture count plus the index, so a set's layers
    // need not be one run.
    check(sprites.layer(one, 0) == 100 && sprites.layer(one, 2) == 102, "layers sit behind the blocks");
    check(sprites.layer(two, 0) == 102 && sprites.layer(two, 1) == 100, "a set keeps its own order");
    check(sprites.first(two) == 102, "first is the set's first, not the lowest layer");

    // `SpriteSet.get(int, int)`: age * (size - 1) / lifetime, clamped by the
    // table so a particle past its lifetime still has a sprite.
    check(sprites.at_age(one, 0, 10) == 100, "a new particle shows the first sprite");
    check(sprites.at_age(one, 5, 10) == 101, "half way through it shows the middle one");
    check(sprites.at_age(one, 10, 10) == 102, "at the end it shows the last one");
    check(sprites.at_age(one, 20, 10) == 102, "past the end it stays on the last one");
    check(sprites.at_age(one, 3, 0) == 100, "a lifetime of zero does not divide by zero");
    check(sprites.layer(-1, 0) == 0 && sprites.size(-1) == 0, "no set is no sprite");
}

void test_type_names() {
    check(particle_type_from_name("minecraft:flame") == ParticleType::Flame, "flame is found");
    check(particle_type_from_name("minecraft:dripping_dripstone_lava") ==
              ParticleType::DrippingDripstoneLava,
          "the last of the drips is found");
    check(particle_type_from_name("minecraft:nothing_like_it") == ParticleType::None,
          "a type we do not make is None");
    check(particle_type_name(ParticleType::None).empty(), "None has no name");
    check(particle_type_name(ParticleType::Count).empty(), "nor has the end of the list");
    // Every name round-trips, which is what the packet reader relies on.
    for (int32_t i = 0; i < static_cast<int32_t>(ParticleType::Count); ++i) {
        const auto type = static_cast<ParticleType>(i);
        if (particle_type_from_name(particle_type_name(type)) != type) {
            std::cerr << "FAIL: " << particle_type_name(type) << " does not round-trip\n";
            ++failures;
        }
    }
}

// `MarsagliaPolarGaussian.nextGaussian`: the first six values of seed 42 and
// the pair of seed 7, as the game's own RandomSource printed them. It is what
// the level_particles cloud scatters a particle by.
void test_gaussian() {
    mcpp::jmath::LegacyRandom random(42);
    const double expected[] = {
        1.141905315473055,   0.919407948982788,  -0.9498666368908959,
        -1.1069902863993377, 0.2809776380727795, 0.6846227956326554,
    };
    for (size_t i = 0; i < std::size(expected); ++i) {
        const double got = random.next_gaussian();
        if (std::abs(got - expected[i]) > 1e-12) {
            std::cerr << "FAIL: gaussian " << i << " is " << got << ", expected " << expected[i] << '\n';
            ++failures;
        }
    }
    // The second of a pair is handed out without drawing again.
    mcpp::jmath::LegacyRandom paired(7);
    check(std::abs(paired.next_gaussian() - 0.8452060657049847) < 1e-12, "the pair's first value");
    check(std::abs(paired.next_gaussian() - 0.9128761787534405) < 1e-12, "the pair's second value");
}

ParticleLevel air_level(const ParticleTable& table, const ParticleSprites& sprites) {
    static const BlockAt air = [](int32_t, int32_t, int32_t) { return 0; };
    return ParticleLevel{air,
                         BlockShapes::game(),
                         table,
                         [](int32_t, int32_t, int32_t) { return 0; },
                         [](ParticleTable::Colour, int32_t, int32_t, int32_t) { return 0xFFFFFFu; },
                         -64,
                         &sprites};
}

void test_cloud_and_events() {
    // A table with one set under every name the engine asks for, so the test
    // needs none of the player's own baked data.
    const auto path = scratch_file("plasma-test-every-sprite.txt");
    {
        std::ofstream out(path);
        out << "T minecraft:particle/a\n";
        for (int32_t i = 0; i < static_cast<int32_t>(ParticleType::Count); ++i) {
            out << "S " << particle_type_name(static_cast<ParticleType>(i)) << " 0\n";
        }
    }
    ParticleSprites sprites;
    check(sprites.open(path, 0), "a table of every type opens");
    std::filesystem::remove(path);
    ParticleTable table;
    const auto level = air_level(table, sprites);

    ParticleEngine engine;
    engine.next_seed = [] { return int64_t{1234}; };
    engine.level_random = mcpp::jmath::LegacyRandom(99);

    // `handleParticleEvent`: a count of zero is one particle aimed by the
    // speed, and the distances are what the speed is multiplied by.
    check(engine.spawn_cloud(level, ParticleType::Flame, {}, 0.0, 0.0, 0.0, 1.0f, 2.0f, 3.0f, 0.5f, 0) == 1,
          "a count of zero makes one particle");
    engine.tick(level);
    check(engine.count() == 1, "and it is in the pool after the tick");

    engine.clear();
    engine.tick(level);
    check(engine.spawn_cloud(level, ParticleType::Smoke, {}, 0.0, 64.0, 0.0, 0.1f, 0.1f, 0.1f, 0.0f, 17) == 17,
          "a count of seventeen makes seventeen");

    // A type with no provider of ours makes nothing and says so.
    engine.clear();
    engine.tick(level);
    check(!engine.spawn(level, ParticleType::None, {}, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0),
          "a type we do not make makes nothing");
    check(engine.spawn_cloud(level, ParticleType::None, {}, 0.0, 0.0, 0.0, 0.0f, 0.0f, 0.0f, 0.0f, 5) == 0,
          "nor does a whole cloud of it");

    // `BreakingItemParticle`: without a sprite for the item there is nothing
    // to draw, so none is made.
    check(!engine.spawn(level, ParticleType::Item, {}, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0),
          "an item with no icon makes no particle");
    ParticleOptions icon;
    icon.has_texture = true;
    icon.texture = 7;
    icon.texture_layer = QuadLayer::Items;
    check(engine.spawn(level, ParticleType::Item, icon, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0),
          "an item with an icon makes one");

    // `LevelEventHandler.levelEvent`: how many each event shows.
    const int32_t pos[3] = {0, 64, 0};
    engine.clear();
    engine.tick(level);
    check(engine.level_event(level, 1501, pos, 0) == 8, "lava put out shows eight large smokes");
    check(engine.level_event(level, 1502, pos, 0) == 5, "a torch burning out shows five smokes");
    check(engine.level_event(level, 1503, pos, 0) == 16, "a portal frame shows sixteen smokes");
    check(engine.level_event(level, 2004, pos, 0) == 40, "a spawner shows twenty smokes and flames");
    check(engine.level_event(level, 2008, pos, 0) == 1, "an explosion shows one ball");
    check(engine.level_event(level, 2000, pos, 1) == 10, "a dispenser shoots ten");
    check(engine.level_event(level, 2010, pos, 1) == 10, "a crafter shoots ten");
    // 2003 throws eight eyes, which have no icon here, and two portals for
    // each of the forty steps round the circle.
    check(engine.level_event(level, 2003, pos, 0) == 80, "an eye of ender rings the frame");
    check(engine.level_event(level, 9999, pos, 0) == 0, "an event with no particles shows none");
    // A face past the six a block has is not shot out of.
    check(engine.level_event(level, 2000, pos, 7) == 0, "a face that is not one of six shows none");
}

// `NoRenderParticle`: an emitter is ticked but never drawn, and each of its
// eight ticks asks for six explosions.
void test_emitter() {
    const auto path = scratch_file("plasma-test-emitter.txt");
    {
        std::ofstream out(path);
        out << "T minecraft:particle/a\nS minecraft:explosion 0\n";
    }
    ParticleSprites sprites;
    sprites.open(path, 0);
    std::filesystem::remove(path);
    ParticleTable table;
    const auto level = air_level(table, sprites);

    ParticleEngine engine;
    engine.next_seed = [] { return int64_t{4321}; };
    engine.level_random = mcpp::jmath::LegacyRandom(5);
    check(engine.spawn(level, ParticleType::ExplosionEmitter, {}, 0.0, 64.0, 0.0, 0.0, 0.0, 0.0),
          "the emitter is made");
    engine.tick(level);
    check(engine.count() == 1, "the emitter alone is in the pool");
    engine.tick(level);
    check(engine.count() == 7, "its first tick adds six explosions");

    ParticleView view{};
    view.camera[0] = view.camera[1] = view.camera[2] = 0.0;
    view.rotation[3] = 1.0f;
    for (auto& plane : view.planes) {
        plane[0] = plane[1] = plane[2] = 0.0f;
        plane[3] = 1.0f;   // everything is inside
    }
    std::vector<ParticleQuad> quads;
    engine.extract(view, level, quads);
    check(quads.size() == 6, "the emitter itself is not drawn");
}

} // namespace

int main() {
    test_sprite_table();
    test_type_names();
    test_gaussian();
    test_cloud_and_events();
    test_emitter();
    if (failures == 0) std::cout << "particles: ok\n";
    return failures == 0 ? 0 : 1;
}
