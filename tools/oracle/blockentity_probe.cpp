#include "mcpp/entity/block_entities.hpp"
#include "mcpp/entity/model.hpp"
#include "mcpp/protocol_versions.hpp"

#include <bit>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

// The C++ half of BlockEntityOracle: the same block states through our own
// placement, printing the matrix each model is submitted on and the pose of
// every part the renderer's animation left behind.
namespace {

int32_t bits(float v) { return std::bit_cast<int32_t>(v); }

std::filesystem::path data(const char* name) {
    const char* home = std::getenv("XDG_DATA_HOME");
    std::filesystem::path base =
        home != nullptr ? std::filesystem::path(home)
                        : std::filesystem::path(std::getenv("HOME")) / ".local" / "share";
    return base / "plasma-client" / "data" / name;
}

std::string read(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        std::cerr << "no " << path.string() << "; bake it with tools/bake_all.py\n";
        std::exit(1);
    }
    std::stringstream text;
    text << input.rdbuf();
    return text.str();
}

void print_matrix(const mcpp::entity::Matrix& m) {
    for (int i = 0; i < 16; ++i) std::cout << (i ? "," : "") << bits(m[static_cast<size_t>(i)]);
    std::cout << '\n';
}

} // namespace

int main() {
    using namespace mcpp::entity;
    ModelSet models;
    models.parse(read(data("entity_models-774.txt")));
    BlockEntityTable table;
    table.parse(read(data("block_entities-774.txt")));

    std::string line;
    std::vector<BlockEntityScene::Piece> pieces;
    const BlockEntityRow* row = nullptr;
    const BlockEntity entity;
    while (std::getline(std::cin, line)) {
        std::istringstream words(line);
        std::string what;
        std::string where;
        words >> what >> where;
        if (what == "state") {
            int32_t state = 0;
            words >> state;
            std::cout << state << '\n';
            row = table.find(state);
            pieces.clear();
            if (row != nullptr) {
                Placement root;
                BlockEntityScene::build(*row, table, entity, models, root, 0.0f, 0, pieces);
            }
            continue;
        }
        // "<what> <case>/<piece>[/<path>]"
        const size_t first = where.find('/');
        const size_t second = where.find('/', first + 1);
        const size_t index = static_cast<size_t>(std::atoi(where.c_str() + first + 1));
        if (index >= pieces.size()) {
            std::cout << "no piece " << index << '\n';
            continue;
        }
        const BlockEntityScene::Piece& piece = pieces[index];
        if (what == "place") {
            print_matrix(piece.root);
            continue;
        }
        const std::string path = second == std::string::npos ? std::string("root")
                                                             : where.substr(second + 1);
        // A renderer that submits one part calls it the root of what it draws.
        std::string wanted = path;
        if (!piece.part.empty()) {
            wanted = path == "root" ? std::string(piece.part)
                                    : std::string(piece.part) + "/" + path;
        }
        const int32_t at = piece.model->find(wanted);
        if (at < 0) {
            std::cout << "no part " << wanted << '\n';
            continue;
        }
        const PartPose& p = piece.pose[at];
        std::cout << bits(p.x) << ',' << bits(p.y) << ',' << bits(p.z) << ',' << bits(p.x_rot)
                  << ',' << bits(p.y_rot) << ',' << bits(p.z_rot) << ',' << bits(p.x_scale) << ','
                  << bits(p.y_scale) << ',' << bits(p.z_scale) << '\n';
    }
}
