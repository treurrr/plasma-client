#include "mcpp/json.hpp"
#include "mcpp/session.hpp"
#include "mcpp/world/atlas.hpp"
#include "mcpp/world/atmosphere.hpp"
#include "mcpp/world/environment.hpp"
#include "mcpp/world/light.hpp"
#include "mcpp/world/render_table.hpp"
#include "mcpp/world/shapes.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

namespace {

namespace fs = std::filesystem;
using namespace mcpp::world;

int failures = 0;
int skipped = 0;

void check(bool condition, std::string_view what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << '\n';
        ++failures;
    }
}

bool near(double a, double b, double tolerance = 1e-3) { return std::abs(a - b) <= tolerance; }

std::string hex(uint32_t argb) { return std::format("#{:08x}", argb); }

fs::path data_directory() {
    fs::path base;
    if (const char* data_home = std::getenv("XDG_DATA_HOME")) {
        base = data_home;
    } else if (const char* home = std::getenv("HOME")) {
        base = fs::path(home) / ".local" / "share";
    }
    return base / "plasma-client" / "data";
}

mcpp::json::Value read_json(const fs::path& path) {
    std::ifstream in(path);
    std::stringstream text;
    text << in.rdbuf();
    return mcpp::json::parse(text.str());
}

// Keyframes written here rather than taken from the game, for the arithmetic
// on its own.
void test_tracks() {
    Environment environment;
    environment.add_timeline_json("test:steps", mcpp::json::parse(R"({
        "period_ticks": 100,
        "tracks": {
            "minecraft:visual/star_brightness": {
                "keyframes": [{"ticks": 10, "value": 0.0}, {"ticks": 60, "value": 1.0}]
            },
            "minecraft:visual/cloud_height": {
                "ease": "constant",
                "keyframes": [{"ticks": 10, "value": 100.0}, {"ticks": 60, "value": 200.0}]
            },
            "minecraft:visual/sun_angle": {
                "ease": {"cubic_bezier": [0.362, 0.241, 0.638, 0.759]},
                "keyframes": [{"ticks": 0, "value": 360.0}, {"ticks": 0, "value": 0.0}]
            },
            "minecraft:visual/fog_color": {
                "modifier": "multiply",
                "keyframes": [{"ticks": 0, "value": "#ffffff"}, {"ticks": 50, "value": "#000000"}]
            }
        }
    })"));
    environment.add_dimension_json("test:world", mcpp::json::parse(R"({
        "attributes": {"minecraft:visual/fog_color": "#804020",
                       "minecraft:visual/water_fog_end_distance": {"modifier": "multiply",
                                                                   "argument": 0.5}},
        "timelines": ["test:steps"]
    })"));
    check(environment.select("test:world"), "a dimension added from json can be selected");
    const auto at = [&](int64_t ticks) { return environment.evaluate(ticks, {}); };

    check(near(at(35).number(attribute::StarBrightness), 0.5), "linear halfway is half");
    check(near(at(10).number(attribute::StarBrightness), 0.0), "on a keyframe is its value");
    check(near(at(85).number(attribute::StarBrightness), 0.5),
          "past the last keyframe it runs into the first of the next period");
    check(near(at(135).number(attribute::StarBrightness), 0.5), "the period repeats");
    check(near(at(59).number(attribute::CloudHeight), 100.0), "constant holds the earlier value");
    check(near(at(60).number(attribute::CloudHeight), 200.0), "and changes on the keyframe");
    check(near(at(5).number(attribute::CloudHeight), 200.0),
          "before the first keyframe a constant track holds the last one");
    check(near(at(0).number(attribute::SunAngle), 0.0),
          "of two keyframes on one tick the later one is left");
    check(near(at(50).number(attribute::SunAngle), 180.0, 1e-3),
          "a symmetric bezier passes its middle at its middle");
    check(at(25).number(attribute::SunAngle) < 90.0,
          std::format("the bezier is slow at the ends ({})", at(25).number(attribute::SunAngle)));
    check(at(0).colour(attribute::FogColor) == 0xFF804020u,
          std::format("multiplied by white is unchanged ({})", hex(at(0).colour(attribute::FogColor))));
    check(at(50).colour(attribute::FogColor) == 0xFF000000u, "multiplied by black is black");
    check(near(at(0).number(attribute::WaterFogEndDistance), 48.0),
          "a modifier on a dimension combines with the default");
    check(near(at(0).number(attribute::FogEndDistance), 1024.0), "an attribute nobody sets is its default");
}

bool load_datapack(Environment& environment) {
    const fs::path root = data_directory() / "datapack-774";
    if (!fs::exists(root / "timeline" / "day.json")) {
        std::cout << "skipped: no datapack at " << root << "; bake one with tools/bake_datapack.py\n";
        ++skipped;
        return false;
    }
    for (const auto& name : {"day", "moon", "early_game", "villager_schedule"}) {
        environment.add_timeline_json(std::string("minecraft:") + name,
                                      read_json(root / "timeline" / (std::string(name) + ".json")));
    }
    for (const auto& tag : {"universal", "in_overworld", "in_nether", "in_end"}) {
        const auto body = read_json(root / "tags" / "timeline" / (std::string(tag) + ".json"));
        std::vector<std::string> members;
        if (const auto* values = body.find("values"); values && values->as_array()) {
            for (const auto& value : *values->as_array()) members.push_back(value.string_or(""));
        }
        environment.add_timeline_tag(std::string("minecraft:") + tag, std::move(members));
    }
    for (const auto& name : {"overworld", "the_nether", "the_end"}) {
        environment.add_dimension_json(
            std::string("minecraft:") + name,
            read_json(root / "dimension_type" / (std::string(name) + ".json")));
    }
    for (const auto& name : {"plains", "swamp", "desert", "basalt_deltas"}) {
        environment.add_biome_json(std::string("minecraft:") + name,
                                   read_json(root / "worldgen" / "biome" / (std::string(name) + ".json")));
    }
    return true;
}

// The game's own day, from the game's own files.
void test_datapack() {
    Environment environment;
    if (!load_datapack(environment)) return;
    check(environment.select("minecraft:overworld"), "the overworld is there");
    check(environment.active_timelines().size() == 4,
          std::format("in_overworld is universal, day, moon and early_game ({})",
                      environment.active_timelines().size()));

    const BiomeWeight plains[] = {{0, 1.0}};
    const BiomeWeight swamp[] = {{1, 1.0}};
    const BiomeWeight desert[] = {{2, 1.0}};
    const auto at = [&](int64_t ticks, std::span<const BiomeWeight> biome) {
        return environment.evaluate(ticks, biome);
    };

    check(near(at(6000, plains).number(attribute::SunAngle), 0.0), "the sun is overhead at noon");
    check(near(at(18000, plains).number(attribute::SunAngle), 180.0, 1e-3),
          "and underfoot at midnight");
    check(near(at(18000, plains).number(attribute::MoonAngle), 360.0, 1e-3),
          std::format("the moon is overhead at midnight ({})",
                      at(18000, plains).number(attribute::MoonAngle)));
    check(at(6000, plains).colour(attribute::SkyColor) == 0xFF78A7FFu,
          std::format("the plains sky at noon is #78a7ff ({})",
                      hex(at(6000, plains).colour(attribute::SkyColor))));
    check(at(6000, desert).colour(attribute::SkyColor) == 0xFF6EB1FFu, "the desert's is #6eb1ff");
    check(at(13670, plains).colour(attribute::SkyColor) == 0xFF000000u, "at 13670 it is black");
    check(near(at(627, plains).number(attribute::StarBrightness), 0.0), "no stars at 627");
    check(near(at(18000, plains).number(attribute::StarBrightness), 0.5), "half bright at night");
    // 0xc0 * 0x0f / 255 floors to 0x0b, 0xd8 * 0x0f / 255 to 0x0c.
    check(at(18000, plains).colour(attribute::FogColor) == 0xFF0B0C16u,
          std::format("the night fog is #c0d8ff times #0f0f16 ({})",
                      hex(at(18000, plains).colour(attribute::FogColor))));
    check(at(6000, plains).colour(attribute::CloudColor) == 0xCCFFFFFFu, "clouds at noon");
    check(at(18000, plains).colour(attribute::CloudColor) == 0xCC191926u,
          std::format("clouds at night ({})", hex(at(18000, plains).colour(attribute::CloudColor))));
    check(near(at(6000, plains).number(attribute::CloudHeight), 192.33), "the cloud height");
    check(near(at(18000, plains).number(attribute::SkyLightFactor), 0.24), "the night's sky light");
    check(at(18000, plains).colour(attribute::SkyLightColor) == 0xFF7A7AFFu, "and its colour");
    check(at(6000, plains).text(attribute::MoonPhase) == "full_moon", "the first night is full");
    check(at(24000 + 6000, plains).text(attribute::MoonPhase) == "waning_gibbous", "then waning");
    check(at(7 * 24000 + 6000, plains).text(attribute::MoonPhase) == "waxing_gibbous",
          "and the eighth night waxing gibbous");
    check(near(at(6000, swamp).number(attribute::WaterFogEndDistance), 96.0 * 0.85),
          "a swamp's water fog is the default times 0.85");
    check(at(6000, swamp).colour(attribute::WaterFogColor) == 0xFF232317u, "and its colour");

    const auto sunrise = at(23757, plains).colour(attribute::SunriseSunsetColor);
    check(sunrise == 0xB1E78733u, std::format("a dawn keyframe ({})", hex(sunrise)));
    const auto dusk = at(12000, plains).colour(attribute::SunriseSunsetColor);
    check((dusk >> 24) > 0x5Fu && (dusk >> 24) < 0xB1u,
          std::format("dusk between two keyframes is between them ({})", hex(dusk)));

    const BiomeWeight edge[] = {{0, 0.5}, {2, 0.5}};
    const auto blended = at(6000, edge).colour(attribute::SkyColor);
    check(blended == 0xFF73ACFFu, std::format("a biome edge blends the sky ({})", hex(blended)));

    // Rain greys the sky: grey (int)(120*.3+167*.59+255*.11) = 162, times 0.6
    // is 97, and three quarters of the way there floors to 102, 114, 136.
    const auto rainy = environment.evaluate(6000, plains, SIZE_MAX, {1.0f, 0.0f});
    check(rainy.colour(attribute::SkyColor) == 0xFF667288u,
          std::format("rain greys the sky ({})", hex(rainy.colour(attribute::SkyColor))));
    check(near(rainy.number(attribute::StarBrightness), 0.0), "rain hides the stars");
    check(near(rainy.number(attribute::SkyLightFactor), 1.0f + 0.3125f * (0.24f - 1.0f)),
          "and dims the sky light by 5/16 of the way to night");
    check(rainy.colour(attribute::FogColor) == 0xFF5F6B99u,
          std::format("and darkens the fog to 127, 127, 153 of 255 ({})", hex(rainy.colour(attribute::FogColor))));

    check(environment.select("minecraft:the_nether"), "the nether is there");
    const BiomeWeight deltas[] = {{3, 1.0}};
    const auto nether = at(6000, deltas);
    check(near(nether.number(attribute::FogEndDistance), 96.0) &&
              near(nether.number(attribute::FogStartDistance), 10.0),
          "nether fog from 10 to 96");
    check(nether.colour(attribute::FogColor) == 0xFF685F70u, "basalt deltas' fog colour");
    check(near(nether.number(attribute::SkyLightFactor), 0.0), "no sky light in the nether");
    check(near(at(13000, deltas).number(attribute::StarBrightness), 0.0), "and no night");

    check(environment.select("minecraft:the_end"), "the end is there");
    check(at(18000, plains).colour(attribute::FogColor) == 0xFF181318u, "the end's fog");
}

// The same evaluation from what a real server sent, recorded.
void test_recording() {
    fs::path file;
    for (const auto* candidate : {"corpus/spawn-200.plrc", "../corpus/spawn-200.plrc",
                                  "../../corpus/spawn-200.plrc"}) {
        if (fs::exists(candidate)) file = candidate;
    }
    if (file.empty()) {
        std::cout << "skipped: no corpus/spawn-200.plrc\n";
        ++skipped;
        return;
    }
    mcpp::SessionOptions options;
    options.replay = file;
    options.username = "PlasmaTest";
    mcpp::SessionEvents events;
    bool in_world = false;
    events.world.on_chunk = [&](const Chunk&) { in_world = true; };
    mcpp::Session session(options, events);
    session.connect();
    while (!in_world && session.pump()) {}
    check(in_world, "the recording reaches a chunk");

    const auto& environment = session.environment();
    check(environment.timeline_count() == 4,
          std::format("the server sent four timelines ({})", environment.timeline_count()));
    check(environment.selected() == "minecraft:overworld", "and put us in the overworld");
    check(environment.active_timelines().size() == 4, "with its four timelines");
    const auto plains = session.registries().index_of("minecraft:worldgen/biome", "minecraft:plains");
    check(plains >= 0, "plains is a biome the server sent");
    const BiomeWeight here[] = {{static_cast<size_t>(std::max(plains, 0)), 1.0}};
    const auto noon = environment.evaluate(6000, here);
    check(noon.colour(attribute::SkyColor) == 0xFF78A7FFu,
          std::format("the server's plains at noon ({})", hex(noon.colour(attribute::SkyColor))));
    check(environment.evaluate(13670, here).colour(attribute::SkyColor) == 0xFF000000u,
          "the server's night");
    check(near(environment.evaluate(627, here).number(attribute::StarBrightness), 0.0),
          "the server's stars at 627");
    check(session.time().day_time == 32120 || session.time().day_time > 0,
          std::format("set_time arrived ({})", session.time().day_time));
}

// The game's sprite ticker: each frame for its own number of ticks, then the
// next, round and round; an interpolating one fades towards the next.
void test_texture_animation() {
    Atlas::Animation animation;
    animation.frames = {{5, 2}, {7, 3}};
    animation.length = 5;
    const uint32_t expected[] = {5, 5, 7, 7, 7, 5, 5};
    for (uint64_t tick = 0; tick < 7; ++tick) {
        check(Atlas::frame_at(animation, tick).layer == expected[tick],
              std::format("frame at tick {}", tick));
    }
    animation.interpolate = true;
    const auto halfway = Atlas::frame_at(animation, 1);
    check(halfway.next == 7 && near(halfway.fade, 0.5), "half way through a two-tick frame");
    check(near(Atlas::frame_at(animation, 3).fade, 1.0 / 3.0), "a third through a three-tick frame");
}

// `BlockLightEngine.checkNode`, `BlockBehaviour.BlockStateBase.getLightBlock`:
// the two things a relight reads from the block itself. A torch gives off 14,
// which is the game's own light_torch value, and every step away from it costs
// max(1, light_block). Ice stops one level, which is why water and ice get
// darker with depth; a rule over block names has it at nothing.
void test_relight() {
    const auto& shapes = BlockShapes::game();
    RenderTable render;
    if (!shapes.loaded() || !render.open(RenderTable::default_path(774))) {
        std::cout << "skipped: no baked block tables\n";
        ++skipped;
        return;
    }
    const int32_t torch = shapes.find("minecraft:torch");
    const int32_t ice = shapes.find("minecraft:ice");
    check(render.at(torch).emission == 14, "the game says a torch gives off 14");
    check(render.at(ice).light_block == 1, "and that ice stops one level");

    mcpp::ByteBuffer air;
    air.write_u8(0);
    air.write_varint(0);
    mcpp::ByteBuffer biomes;
    biomes.write_u8(0);
    biomes.write_varint(0);
    ChunkSection section;
    section.blocks = PalettedContainer::read(air, 4096, 8);
    section.biomes = PalettedContainer::read(biomes, 64, 3);

    ChunkMap world;
    auto& chunk = world[{0, 0}];
    chunk.min_y = 0;
    chunk.sections.push_back(std::move(section));
    chunk.sky_light.sections.assign(3, std::vector<uint8_t>(2048, 0));
    chunk.block_light.sections.assign(3, std::vector<uint8_t>(2048, 0));
    for (int32_t y = 0; y < 16; ++y) {
        for (int32_t z = 0; z < 16; ++z) {
            for (int32_t x = 0; x < 16; ++x) chunk.sky_light.set(1, x, y, z, 15);
        }
    }
    LightEngine light;

    chunk.set_block_state(8, 8, 8, torch);
    light.relight(world, 8, 8, 8);
    check(chunk.block_light_at(8, 8, 8) == 14, "a torch lights its own cell to 14");
    check(chunk.block_light_at(9, 8, 8) == 13, "the cell beside it to 13");
    check(chunk.block_light_at(11, 8, 8) == 11, "and three cells away to 11");
    check(chunk.block_light_at(8, 8, 8 + 14) == 0, "fourteen away there is nothing left");

    // A shaft of air in stone, sky light falling down it: only what the shaft
    // itself lets through reaches the bottom, so what ice stops is visible.
    ChunkMap shaft_world;
    auto& shaft = shaft_world[{0, 0}];
    shaft.min_y = 0;
    ChunkSection stone_section;
    mcpp::ByteBuffer air2;
    air2.write_u8(0);
    air2.write_varint(0);
    mcpp::ByteBuffer biomes2;
    biomes2.write_u8(0);
    biomes2.write_varint(0);
    stone_section.blocks = PalettedContainer::read(air2, 4096, 8);
    stone_section.biomes = PalettedContainer::read(biomes2, 64, 3);
    shaft.sections.push_back(std::move(stone_section));
    shaft.sky_light.sections.assign(3, std::vector<uint8_t>(2048, 0));
    shaft.block_light.sections.assign(3, std::vector<uint8_t>(2048, 0));
    const int32_t stone = shapes.find("minecraft:stone");
    for (int32_t y = 0; y < 16; ++y) {
        for (int32_t z = 0; z < 16; ++z) {
            for (int32_t x = 0; x < 16; ++x) {
                const bool open = x == 8 && z == 8;
                if (!open) shaft.set_block_state(x, y, z, stone);
                shaft.sky_light.set(1, x, y, z, open ? 15 : 0);
            }
        }
    }
    shaft.set_block_state(8, 10, 8, ice);
    light.relight(shaft_world, 8, 10, 8);
    check(shaft.sky_light_at(8, 10, 8) == 14, "ice takes one level out of the sky light");
    check(shaft.sky_light_at(8, 9, 8) == 13, "and the cell under it one more");
    check(shaft.sky_light_at(8, 7, 8) == 11, "three blocks under it, three more");
}

// A frame's lightmap is kept between ticks, so every table a frame is drawn
// with has to be the table its own factors ask for, over a whole day and
// whatever the flicker, the gamma and the night vision do in between.
void test_lightmap() {
    Environment environment;
    if (!load_datapack(environment)) return;
    check(environment.select("minecraft:overworld"), "the overworld is there for the lightmap");
    const std::vector<BiomeWeight> plains{{1, 1.0}};
    AttributeProbe probe;
    probe.reset(environment.evaluate(0, plains, 1));
    int frames = 0;
    for (int64_t tick = 0; tick < 24000; tick += 37) {
        probe.tick(environment.evaluate(tick, plains, 1, {tick % 7 == 0 ? 1.0f : 0.0f, 0.0f}));
        for (const float partial : {0.0f, 0.3f, 0.6f, 0.95f}) {
            AtmosphereInputs in;
            in.block_flicker = static_cast<float>(tick % 11) * 0.003f;
            in.gamma = static_cast<float>(tick % 5) * 0.25f;
            in.night_vision = tick % 13 == 0 ? 0.7f : 0.0f;
            in.darken_world = tick % 17 == 0 ? 0.4f : 0.0f;
            in.ambient_light = tick % 19 == 0 ? 0.1f : 0.0f;
            in.eye[1] = 70.0 + static_cast<double>(tick % 23);
            in.forward[0] = static_cast<float>(tick % 3) - 1.0f;
            const auto air = atmosphere(probe, partial, in);
            for (int32_t sky = 0; sky < 16; ++sky) {
                for (int32_t block = 0; block < 16; ++block) {
                    float rgb[3];
                    lightmap_texel(air, block, sky, rgb);
                    const uint8_t* texel = &air.lightmap[static_cast<size_t>(sky * 16 + block) * 4];
                    for (int i = 0; i < 3; ++i) {
                        const auto want = static_cast<uint8_t>(
                            std::lround(std::clamp(rgb[i], 0.0f, 1.0f) * 255.0f));
                        if (texel[i] != want) {
                            check(false, std::format("lightmap texel {},{} channel {} at tick {} "
                                                     "partial {}: {} not {}",
                                                     block, sky, i, tick, partial, texel[i], want));
                            return;
                        }
                    }
                }
            }
            ++frames;
        }
    }
    check(frames == 2596, std::format("every frame's lightmap matches its factors ({})", frames));
}

} // namespace

int main() {
    test_texture_animation();
    test_tracks();
    test_datapack();
    test_recording();
    test_relight();
    test_lightmap();
    if (failures == 0) {
        std::cout << std::format("all world tests passed ({} skipped)\n", skipped);
        return 0;
    }
    std::cerr << std::format("{} check(s) failed\n", failures);
    return 1;
}
