// The C++ half of TimelineOracle: the same datapack files, ticks and weathers
// through world::Environment, one value per line in the oracle's order. The
// overworld's own timeline tag also names the villager and early-game
// timelines; neither touches what is printed here.
#include "mcpp/json.hpp"
#include "mcpp/world/environment.hpp"

#include <bit>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

namespace fs = std::filesystem;
using namespace mcpp::world;

mcpp::json::Value read(const fs::path& path) {
    std::ifstream in(path);
    std::stringstream text;
    text << in.rdbuf();
    return mcpp::json::parse(text.str());
}

} // namespace

int main() {
    fs::path data;
    if (const char* home = std::getenv("XDG_DATA_HOME")) {
        data = home;
    } else {
        data = fs::path(std::getenv("HOME")) / ".local" / "share";
    }
    const fs::path root = data / "plasma-client" / "data" / "datapack-774";
    Environment env;
    for (const auto* name : {"day", "moon", "early_game", "villager_schedule"}) {
        env.add_timeline_json(std::string("minecraft:") + name,
                              read(root / "timeline" / (std::string(name) + ".json")));
    }
    for (const auto* tag : {"universal", "in_overworld"}) {
        const auto body = read(root / "tags" / "timeline" / (std::string(tag) + ".json"));
        std::vector<std::string> members;
        if (const auto* values = body.find("values"); values && values->as_array()) {
            for (const auto& value : *values->as_array()) members.push_back(value.string_or(""));
        }
        env.add_timeline_tag(std::string("minecraft:") + tag, std::move(members));
    }
    env.add_dimension_json("minecraft:overworld", read(root / "dimension_type" / "overworld.json"));
    env.add_biome_json("minecraft:plains", read(root / "worldgen" / "biome" / "plains.json"));
    env.select("minecraft:overworld", true);

    const int64_t ticks[] = {0, 71, 133, 500, 627, 1000, 6000, 11000, 11867, 12000, 12300, 12500, 13000,
                             13300, 13670, 15000, 18000, 21000, 22330, 23000, 23400, 23757, 23999, 30000,
                             100000, 192000 - 1};
    const float weathers[][2] = {{0.0f, 0.0f}, {0.4f, 0.0f}, {1.0f, 0.0f}, {1.0f, 0.6f}};
    const BiomeWeight plains[] = {{0, 1.0}};
    for (const auto& weather : weathers) {
        for (const int64_t tick : ticks) {
            const auto values = env.evaluate(tick, plains, 0, {weather[0], weather[1]});
            for (const auto id : {attribute::SkyColor, attribute::FogColor, attribute::CloudColor,
                                  attribute::SunriseSunsetColor, attribute::SkyLightColor}) {
                std::cout << values.colour(id) << '\n';
            }
            for (const auto id : {attribute::SunAngle, attribute::MoonAngle, attribute::StarAngle,
                                  attribute::StarBrightness, attribute::SkyLightFactor,
                                  attribute::SkyLightLevel}) {
                std::cout << std::bit_cast<int32_t>(values.number(id)) << '\n';
            }
            std::cout << values.text(attribute::MoonPhase) << '\n';
        }
    }
    return 0;
}
