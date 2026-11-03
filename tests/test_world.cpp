#include "mcpp/json.hpp"
#include "mcpp/session.hpp"
#include "mcpp/world/environment.hpp"

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

} // namespace

int main() {
    test_tracks();
    test_datapack();
    test_recording();
    if (failures == 0) {
        std::cout << std::format("all world tests passed ({} skipped)\n", skipped);
        return 0;
    }
    std::cerr << std::format("{} check(s) failed\n", failures);
    return 1;
}
