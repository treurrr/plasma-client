#include "mcpp/world/shapes.hpp"

#include "mcpp/protocol_versions.hpp"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <format>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace mcpp::world {
namespace {

std::vector<std::string_view> split(std::string_view text, char by) {
    std::vector<std::string_view> out;
    size_t start = 0;
    while (true) {
        const size_t at = text.find(by, start);
        out.push_back(text.substr(start, at == std::string_view::npos ? at : at - start));
        if (at == std::string_view::npos) break;
        start = at + 1;
    }
    return out;
}

double to_double(std::string_view text) {
    double value = 0.0;
    std::from_chars(text.data(), text.data() + text.size(), value);
    return value;
}

int64_t to_int(std::string_view text) {
    int64_t value = 0;
    std::from_chars(text.data(), text.data() + text.size(), value);
    return value;
}

// "a,b*3,c" into a, b, b, b, c.
std::vector<std::string_view> runs(std::string_view text) {
    std::vector<std::string_view> out;
    for (const auto item : split(text, ',')) {
        const size_t star = item.rfind('*');
        if (star == std::string_view::npos) {
            out.push_back(item);
            continue;
        }
        const auto count = to_int(item.substr(star + 1));
        for (int64_t i = 0; i < count; ++i) out.push_back(item.substr(0, star));
    }
    return out;
}

Special special_of(std::string_view name) {
    struct Entry {
        std::string_view name;
        Special special;
    };
    static constexpr Entry kSpecial[] = {
        {"minecraft:scaffolding", Special::Scaffolding},
        {"minecraft:powder_snow", Special::PowderSnow},
        {"minecraft:cobweb", Special::Cobweb},
        {"minecraft:sweet_berry_bush", Special::SweetBerryBush},
        {"minecraft:honey_block", Special::Honey},
        {"minecraft:slime_block", Special::Slime},
        {"minecraft:bubble_column", Special::BubbleColumn},
        {"minecraft:ice", Special::Ice},
        {"minecraft:frosted_ice", Special::Ice},
        {"minecraft:ladder", Special::Ladder},
        {"minecraft:water", Special::Water},
        {"minecraft:lava", Special::Lava},
    };
    for (const auto& entry : kSpecial) {
        if (entry.name == name) return entry.special;
    }
    if (name.ends_with("_bed")) return Special::Bed;
    if (name.ends_with("_trapdoor")) return Special::Trapdoor;
    return Special::None;
}

} // namespace

std::filesystem::path BlockShapes::default_path(int32_t protocol) {
    std::filesystem::path base;
    if (const char* data_home = std::getenv("XDG_DATA_HOME")) {
        base = data_home;
    } else if (const char* home = std::getenv("HOME")) {
        base = std::filesystem::path(home) / ".local" / "share";
    }
    return base / "plasma-client" / "data" / std::format("collision_shapes-{}.txt", protocol);
}

const BlockShapes& BlockShapes::game() {
    static const BlockShapes table = [] {
        BlockShapes shapes;
        const auto path = default_path(protocol::kProtocol_1_21_11);
        std::ifstream input(path);
        if (input) {
            std::stringstream text;
            text << input.rdbuf();
            if (!shapes.parse(text.str())) {
                throw std::runtime_error(std::format("{} is damaged; bake it again", path.string()));
            }
            return shapes;
        }
        std::cerr << std::format("no collision shapes at {}; every block is air to the player's "
                                 "own movement until tools/bake_all.py bakes them\n",
                                 path.string());
        shapes.parse("shape\t0\t-\nminecraft:air\t0\t0\t1\t-\t0.6\t1\t1\t-\t0\t0\t-\t0\t0\t0\n");
        return shapes;
    }();
    return table;
}

bool BlockShapes::parse(std::string_view text) {
    shapes_.clear();
    edge_pool_.clear();
    cell_pool_.clear();
    box_pool_.clear();
    blocks_.clear();
    states_.clear();
    named_.clear();

    std::vector<std::string_view> rows;
    for (const auto line : split(text, '\n')) {
        if (line.empty() || line.front() == '#') continue;
        const auto fields = split(line, '\t');
        if (fields[0] == "shape") {
            if (fields.size() != 3) return false;
            Shape shape;
            const auto parts = split(fields[2], ' ');
            if (parts[0] == "v" && parts.size() == 5) {
                shape.empty = false;
                for (int axis = 0; axis < 3; ++axis) {
                    const auto edges = split(parts[1 + axis], ',');
                    shape.voxels.first[axis] = static_cast<uint32_t>(edge_pool_.size());
                    shape.voxels.edges[axis] = static_cast<uint8_t>(edges.size());
                    for (const auto edge : edges) edge_pool_.push_back(to_double(edge));
                }
                const auto cells = parts[4];
                const size_t bit0 = cell_pool_.size() * 8;
                shape.voxels.cells = static_cast<uint32_t>(bit0);
                cell_pool_.resize((bit0 + cells.size() + 7) / 8, 0);
                const size_t nx = shape.voxels.edges[0] - 1u;
                const size_t ny = shape.voxels.edges[1] - 1u;
                const auto ys = edges(shape.voxels, 1);
                for (size_t i = 0; i < cells.size(); ++i) {
                    if (cells[i] != '1') continue;
                    cell_pool_[(bit0 + i) / 8] |= static_cast<uint8_t>(1u << ((bit0 + i) % 8));
                    const size_t y = (i / nx) % ny;
                    shape.voxels.top = std::max(shape.voxels.top, ys[y + 1]);
                }
            } else if (parts[0] == "b") {
                shape.empty = false;
                shape.box_first = static_cast<uint32_t>(box_pool_.size());
                for (size_t i = 1; i < parts.size(); ++i) {
                    const auto n = split(parts[i], ',');
                    if (n.size() != 6) return false;
                    box_pool_.push_back({{to_double(n[0]), to_double(n[1]), to_double(n[2])},
                                         {to_double(n[3]), to_double(n[4]), to_double(n[5])}});
                }
                shape.box_count = static_cast<uint32_t>(box_pool_.size()) - shape.box_first;
            }
            shapes_.push_back(shape);
        } else if (fields[0] == "named") {
            if (fields.size() != 3) return false;
            named_.emplace_back(std::string(fields[1]), static_cast<uint16_t>(to_int(fields[2])));
        } else {
            rows.push_back(line);
        }
    }

    for (const auto line : rows) {
        const auto f = split(line, '\t');
        if (f.size() != 15) return false;
        BlockInfo block;
        block.name = std::string(f[0]);
        block.registry_id = static_cast<int32_t>(to_int(f[1]));
        block.first = static_cast<int32_t>(to_int(f[2]));
        block.count = static_cast<int32_t>(to_int(f[3]));
        if (f[4] != "-") {
            for (const auto property : split(f[4], ';')) {
                const size_t eq = property.find('=');
                BlockInfo::Property p;
                p.name = std::string(property.substr(0, eq));
                for (const auto value : split(property.substr(eq + 1), '|')) p.values.emplace_back(value);
                block.properties.push_back(std::move(p));
            }
        }
        block.friction = static_cast<float>(to_double(f[5]));
        block.speed = static_cast<float>(to_double(f[6]));
        block.jump = static_cast<float>(to_double(f[7]));
        if (f[8] != "-") {
            const auto o = split(f[8], ':');
            block.offset = o[0] == "xyz" ? 2 : 1;
            block.max_horizontal_offset = static_cast<float>(to_double(o[1]));
            block.max_vertical_offset = static_cast<float>(to_double(o[2]));
        }
        block.special = special_of(block.name);

        const auto hardness = runs(f[9]);
        const auto flags = runs(f[10]);
        const auto fluids = runs(f[11]);
        const auto collision = runs(f[12]);
        const auto outline = runs(f[13]);
        const auto interaction = runs(f[14]);
        const auto count = static_cast<size_t>(block.count);
        if (hardness.size() != count || flags.size() != count || fluids.size() != count ||
            collision.size() != count || outline.size() != count || interaction.size() != count) {
            return false;
        }
        const auto block_index = static_cast<uint16_t>(blocks_.size());
        if (states_.size() < count + static_cast<size_t>(block.first)) {
            states_.resize(count + static_cast<size_t>(block.first));
        }
        for (size_t i = 0; i < count; ++i) {
            StateInfo& s = states_[static_cast<size_t>(block.first) + i];
            s.block = block_index;
            s.hardness = static_cast<float>(to_double(hardness[i]));
            s.flags = static_cast<uint16_t>(to_int(flags[i]));
            s.collision = static_cast<uint16_t>(to_int(collision[i]));
            s.outline = static_cast<uint16_t>(to_int(outline[i]));
            s.interaction = static_cast<uint16_t>(to_int(interaction[i]));
            const auto fluid = fluids[i];
            if (fluid != "-") {
                s.fluid = fluid[0] == 'w' ? Fluid::Water : Fluid::Lava;
                s.fluid_amount = static_cast<uint8_t>(fluid[1] - '0');
                s.fluid_falling = fluid.find('f') != std::string_view::npos;
                s.fluid_source = fluid.find('s') != std::string_view::npos;
            }
            if (s.collision >= shapes_.size() || s.outline >= shapes_.size() ||
                s.interaction >= shapes_.size()) {
                return false;
            }
        }
        blocks_.push_back(std::move(block));
    }
    return !states_.empty() && !blocks_.empty();
}

std::string_view BlockShapes::property(int32_t id, std::string_view key) const {
    const auto& b = block(id);
    int32_t local = std::max(0, id - b.first);
    for (auto p = b.properties.rbegin(); p != b.properties.rend(); ++p) {
        const auto n = static_cast<int32_t>(p->values.size());
        if (p->name == key) return p->values[static_cast<size_t>(local % n)];
        local /= n;
    }
    return {};
}

int32_t BlockShapes::with(int32_t id, std::string_view key, std::string_view value) const {
    const auto& b = block(id);
    const int32_t local = std::max(0, id - b.first);
    int32_t stride = 1;
    for (auto p = b.properties.rbegin(); p != b.properties.rend(); ++p) {
        const auto n = static_cast<int32_t>(p->values.size());
        if (p->name == key) {
            const auto found = std::ranges::find(p->values, value);
            if (found == p->values.end()) return -1;
            const auto wanted = static_cast<int32_t>(found - p->values.begin());
            const int32_t current = (local / stride) % n;
            return b.first + local + (wanted - current) * stride;
        }
        stride *= n;
    }
    return -1;
}

int32_t BlockShapes::named(std::string_view name) const {
    for (const auto& [key, shape] : named_) {
        if (key == name) return shape;
    }
    return -1;
}

int32_t BlockShapes::find(std::string_view block_name) const {
    for (const auto& b : blocks_) {
        if (b.name == block_name) return b.first;
    }
    return -1;
}

int32_t BlockShapes::find(
    std::string_view block_name,
    std::initializer_list<std::pair<std::string_view, std::string_view>> set) const {
    int32_t id = find(block_name);
    for (const auto& [key, value] : set) {
        if (id < 0) return -1;
        id = with(id, key, value);
    }
    return id;
}

void BlockShapes::offset(int32_t id, int32_t x, int32_t z, double out[3]) const {
    out[0] = out[1] = out[2] = 0.0;
    const auto& b = block(id);
    if (b.offset == 0) return;
    int64_t seed = static_cast<int64_t>(static_cast<int32_t>(static_cast<uint32_t>(x) * 3129871u)) ^
                   static_cast<int64_t>(z) * 116129781LL;
    seed = static_cast<int64_t>(static_cast<uint64_t>(seed) * static_cast<uint64_t>(seed) *
                                    42317861ull +
                                static_cast<uint64_t>(seed) * 11ull);
    seed >>= 16;
    const auto nibble = [&](int shift) {
        return static_cast<float>((seed >> shift) & 15) / 15.0f;
    };
    const double h = b.max_horizontal_offset;
    const auto clamp = [h](double v) { return v < -h ? -h : std::min(v, h); };
    out[0] = clamp((nibble(0) - 0.5) * 0.5);
    out[2] = clamp((nibble(8) - 0.5) * 0.5);
    if (b.offset == 2) out[1] = (nibble(4) - 1.0) * static_cast<double>(b.max_vertical_offset);
}

} // namespace mcpp::world
