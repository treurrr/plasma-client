#include "mcpp/entity/humanoid.hpp"
#include "mcpp/entity/model.hpp"

#include <bit>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

// The C++ half of ModelOracle: the layers from entity_models-774.txt baked
// by our cube, and our player animation on the same cases, values only.
namespace {

int32_t bits(float v) { return std::bit_cast<int32_t>(v); }

} // namespace

int main() {
    std::filesystem::path base;
    if (const char* data_home = std::getenv("XDG_DATA_HOME")) {
        base = data_home;
    } else if (const char* home = std::getenv("HOME")) {
        base = std::filesystem::path(home) / ".local" / "share";
    }
    const auto data = base / "plasma-client" / "data" / "entity_models-774.txt";
    std::ifstream input(data);
    if (!input) {
        std::cerr << "no " << data.string() << "; bake it with tools/bake_all.py\n";
        return 1;
    }
    std::stringstream text;
    text << input.rdbuf();
    mcpp::entity::ModelSet set;
    set.parse(text.str());

    for (const char* name : {"player", "player_slim", "zombie", "creeper", "pig", "chicken"}) {
        const auto* model = set.find(name);
        if (model == nullptr) return 1;
        for (const auto& part : model->parts) {
            for (const auto& box : part.boxes) {
                mcpp::entity::Model one;
                one.parts.push_back({"x", -1, {}, {box}});
                std::vector<mcpp::entity::Vertex> vertices;
                mcpp::entity::bake(one, vertices);
                for (size_t face = 0; face * 6 < vertices.size(); ++face) {
                    const auto* f = &vertices[face * 6];
                    std::cout << int(f[0].normal[0]) << ',' << int(f[0].normal[1]) << ','
                              << int(f[0].normal[2]) << '\n';
                    for (const int k : {0, 1, 2, 5}) {
                        const auto& v = f[k];
                        std::cout << bits(v.position[0]) << ',' << bits(v.position[1]) << ','
                                  << bits(v.position[2]) << ',' << bits(v.uv[0]) << ','
                                  << bits(v.uv[1]) << '\n';
                    }
                }
            }
        }
    }

    const float cases[][10] = {
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        {3.7f, 1, -30, 20, 37.5f, 0, 0, 0, 0, 0},
        {11.25f, 0.43f, 47, -61, 1234.6f, 0.3f, 0, 0, 0, 0},
        {5, 0.8f, 10, 5, 99.9f, 0.75f, 1, 0, 0, 0},
        {8, 0.2f, -5, -89, 3.3f, 0.5f, 0, 0, 1, 0},
        {6.5f, 1, 0, 40, 20, 0, 0, 0.6f, 0, 0},
        {17.2f, 1, 0, 0, 21, 0.2f, 0, 1, 0, 0},
        {24.9f, 0.9f, 15, 15, 22, 0, 1, 0.35f, 1, 0},
        {2, 0.5f, 0, 0, 5, 0, 0, 0, 0, 1},
    };
    for (const char* name : {"player", "player_slim"}) {
        const auto* model = set.find(name);
        const auto parts = mcpp::entity::HumanoidParts::of(*model);
        mcpp::entity::Pose pose(*model);
        for (const auto& c : cases) {
            mcpp::entity::HumanoidState s;
            s.walk_position = c[0];
            s.walk_speed = c[1];
            s.y_rot = c[2];
            s.x_rot = c[3];
            s.age_in_ticks = c[4];
            s.attack_time = c[5];
            s.crouching = c[6] != 0;
            s.swim_amount = c[7];
            s.attack_left = c[8] != 0;
            s.passenger = c[9] != 0;
            mcpp::entity::animate_player(*model, parts, s, pose);
            for (size_t i = 0; i < model->parts.size(); ++i) {
                if (model->parts[i].path == "root") continue;
                const auto& p = pose.parts[i];
                std::cout << bits(p.x) << ',' << bits(p.y) << ',' << bits(p.z) << ','
                          << bits(p.x_rot) << ',' << bits(p.y_rot) << ',' << bits(p.z_rot) << ','
                          << (p.visible ? 1 : 0) << '\n';
            }
        }
    }
}
