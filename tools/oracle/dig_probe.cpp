// The C++ half of DigOracle: each tick of digging through the client's own
// tool rules (client/game_mode.cpp) and destroy speed (client/player_rules.cpp).
// Reads the oracle's lines on stdin: the block tags first, then the cases.
#include "client/game_mode.hpp"
#include "client/player_rules.hpp"
#include "mcpp/jmath.hpp"

#include <bit>
#include <format>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

int main() {
    mcpp::Tags::Set block_tags;
    std::vector<std::string> lines;
    for (std::string line; std::getline(std::cin, line);) {
        if (line.empty()) continue;
        lines.push_back(line);
        if (!line.starts_with("tag ")) continue;
        std::stringstream in(line.substr(4));
        std::string name;
        std::string ids;
        in >> name >> ids;
        auto& set = block_tags[name];
        std::stringstream list(ids);
        for (std::string id; std::getline(list, id, ',');) {
            if (id != "-") set.push_back(std::stoi(id));
        }
    }
    mcpp::Tags tags;
    tags.set("minecraft:block", block_tags);
    const auto& shapes = mcpp::world::BlockShapes::game();

    float progress = 0.0f;
    float step = 0.0f;
    for (const auto& line : lines) {
        const auto value = line.substr(line.rfind(' ') + 1);
        if (line.starts_with("tag ")) {
            std::cout << value << '\n';
        } else if (line.starts_with("case ")) {
            std::stringstream in(value);
            std::vector<std::string> p;
            for (std::string part; std::getline(in, part, ',');) p.push_back(part);
            const int32_t state = std::stoi(p[0]);
            const auto tool = client::tool_on_block(client::ToolTable::game().find(std::stoi(p[1])), state, shapes, tags);
            client::DigContext dig;
            dig.tool_speed = tool.speed;
            dig.correct_tool = tool.correct;
            dig.haste = std::stoi(p[2]);
            dig.conduit_power = std::stoi(p[3]);
            dig.mining_fatigue = std::stoi(p[4]);
            dig.mining_efficiency = std::stod(p[5]);
            dig.eye_in_water = p[6] == "1";
            dig.on_ground = p[7] == "1";
            const auto& info = shapes.state(state);
            step = client::destroy_progress(info.hardness, (info.flags & mcpp::world::kNeedsTool) != 0, dig);
            progress = 0.0f;
            std::cout << value << '\n';
        } else if (line.starts_with("tick ")) {
            progress += step;
            const int stage = progress > 0.0f ? mcpp::jmath::to_int(progress * 10.0f) : -1;
            std::cout << std::format("{}:{}:{}\n", std::bit_cast<int32_t>(step), std::bit_cast<int32_t>(progress), stage);
        }
    }
    return 0;
}
