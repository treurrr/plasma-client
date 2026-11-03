#include "mcpp/world/shapes.hpp"

#include "mcpp/data/collision_shapes.hpp"
#include "mcpp/data/hardness.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <format>
#include <fstream>
#include <map>
#include <sstream>

namespace mcpp::world {
namespace {

using Sixteenths = std::array<double, 6>;

Box box(const Sixteenths& s) {
    return {{s[0] / 16.0, s[1] / 16.0, s[2] / 16.0}, {s[3] / 16.0, s[4] / 16.0, s[5] / 16.0}};
}

std::string_view get(const Properties& properties, std::string_view key) {
    for (const auto& [k, v] : properties) {
        if (k == key) return v;
    }
    return {};
}

int get_int(const Properties& properties, std::string_view key, int fallback) {
    const auto value = get(properties, key);
    int result = fallback;
    std::from_chars(value.data(), value.data() + value.size(), result);
    return result;
}

// Horizontal facings as quarter turns clockwise from north, seen from above.
int quarter_turns(std::string_view facing) {
    if (facing == "east") return 1;
    if (facing == "south") return 2;
    if (facing == "west") return 3;
    return 0;
}

// A box drawn for a block facing north, turned to face the other way.
Sixteenths turn(const Sixteenths& s, int turns) {
    Sixteenths r = s;
    for (int i = 0; i < turns; ++i) {
        const Sixteenths was = r;
        // (x, z) -> (16 - z, x): north-west goes to north-east.
        r[0] = 16.0 - was[5];
        r[3] = 16.0 - was[2];
        r[2] = was[0];
        r[5] = was[3];
    }
    return r;
}

// A box drawn for a block pointing up, pointed the other way: a rod, a bud,
// a piston head.
Sixteenths point(const Sixteenths& s, std::string_view facing) {
    const auto map = [&](auto fx, auto fy, auto fz) {
        double a[3];
        double b[3];
        fx(s[0], s[1], s[2], a[0]);
        fy(s[0], s[1], s[2], a[1]);
        fz(s[0], s[1], s[2], a[2]);
        fx(s[3], s[4], s[5], b[0]);
        fy(s[3], s[4], s[5], b[1]);
        fz(s[3], s[4], s[5], b[2]);
        return Sixteenths{std::min(a[0], b[0]), std::min(a[1], b[1]), std::min(a[2], b[2]),
                          std::max(a[0], b[0]), std::max(a[1], b[1]), std::max(a[2], b[2])};
    };
    const auto id_x = [](double x, double, double, double& o) { o = x; };
    const auto id_z = [](double, double, double z, double& o) { o = z; };
    const auto y_as = [](double, double y, double, double& o) { o = y; };
    const auto flip_y = [](double, double y, double, double& o) { o = 16.0 - y; };
    const auto z_as = [](double, double, double z, double& o) { o = z; };
    if (facing == "down") return map(id_x, flip_y, id_z);
    if (facing == "north") return map(id_x, z_as, flip_y);
    if (facing == "south") return map(id_x, z_as, y_as);
    if (facing == "west") return map(flip_y, z_as, id_x);
    if (facing == "east") {
        return map(y_as, z_as, [](double x, double, double, double& o) { o = x; });
    }
    return s;
}

void add(std::vector<Box>& out, const Sixteenths& s) { out.push_back(box(s)); }

std::vector<Box> parse_boxes(std::string_view text, bool& known) {
    std::vector<Box> out;
    while (!text.empty()) {
        const auto bar = text.find('|');
        const auto one = text.substr(0, bar);
        Sixteenths s{};
        size_t at = 0;
        for (int i = 0; i < 6; ++i) {
            const auto comma = one.find(',', at);
            const auto field = one.substr(at, comma == std::string_view::npos ? one.size() - at
                                                                              : comma - at);
            const auto parsed = std::from_chars(field.data(), field.data() + field.size(), s[i]);
            if (parsed.ec != std::errc{}) known = false;
            at = comma == std::string_view::npos ? one.size() : comma + 1;
        }
        add(out, s);
        if (bar == std::string_view::npos) break;
        text.remove_prefix(bar + 1);
    }
    return out;
}

std::vector<Box> stair(const Properties& p) {
    std::vector<Box> out;
    const bool top = get(p, "half") == "top";
    const double base_low = top ? 8 : 0;
    const double step_low = top ? 0 : 8;
    add(out, {0, base_low, 0, 16, base_low + 8, 16});
    const int turns = quarter_turns(get(p, "facing"));
    const auto shape = get(p, "shape");
    std::vector<Sixteenths> steps;
    if (shape == "outer_left") {
        steps.push_back({0, step_low, 0, 8, step_low + 8, 8});
    } else if (shape == "outer_right") {
        steps.push_back({8, step_low, 0, 16, step_low + 8, 8});
    } else {
        steps.push_back({0, step_low, 0, 16, step_low + 8, 8});
        if (shape == "inner_left") steps.push_back({0, step_low, 8, 8, step_low + 8, 16});
        if (shape == "inner_right") steps.push_back({8, step_low, 8, 16, step_low + 8, 16});
    }
    for (const auto& s : steps) add(out, turn(s, turns));
    return out;
}

// A post with an arm towards each side it connects to: fences, panes, bars.
std::vector<Box> cross(const Properties& p, double low, double high, double height) {
    std::vector<Box> out;
    const bool north = get(p, "north") == "true";
    const bool south = get(p, "south") == "true";
    const bool west = get(p, "west") == "true";
    const bool east = get(p, "east") == "true";
    add(out, {low, 0, low, high, height, high});
    if (north) add(out, {low, 0, 0, high, height, low});
    if (south) add(out, {low, 0, high, high, height, 16});
    if (west) add(out, {0, 0, low, low, height, high});
    if (east) add(out, {high, 0, low, 16, height, high});
    return out;
}

std::vector<Box> wall(const Properties& p, double height) {
    std::vector<Box> out;
    if (get(p, "up") == "true") add(out, {4, 0, 4, 12, height, 12});
    const auto side = [&](std::string_view key) {
        const auto value = get(p, key);
        return !value.empty() && value != "none";
    };
    if (side("north")) add(out, {5, 0, 0, 11, height, 11});
    if (side("south")) add(out, {5, 0, 5, 11, height, 16});
    if (side("west")) add(out, {0, 0, 5, 11, height, 11});
    if (side("east")) add(out, {5, 0, 5, 16, height, 11});
    return out;
}

// A thin slab against one side of the block, the side named for a block
// facing north being the south one: doors, ladders, open trapdoors.
Sixteenths against(int turns) {
    return turn({0, 0, 13, 16, 16, 16}, turns);
}

std::vector<Box> door(const Properties& p) {
    int turns = quarter_turns(get(p, "facing"));
    if (get(p, "open") == "true") turns += get(p, "hinge") == "right" ? -1 : 1;
    // Facing east the door stands on the west side: a block facing north has
    // it on the south.
    return {box(against(((turns % 4) + 4) % 4))};
}

std::vector<Box> chest(const Properties& p) {
    Sixteenths s{1, 0, 1, 15, 14, 15};
    const auto type = get(p, "type");
    if (type == "left" || type == "right") {
        const int facing = quarter_turns(get(p, "facing"));
        const int towards = (facing + (type == "left" ? 1 : 3)) % 4;
        switch (towards) {
            case 0: s[2] = 0; break;
            case 1: s[3] = 16; break;
            case 2: s[5] = 16; break;
            default: s[0] = 0; break;
        }
    }
    return {box(s)};
}

std::vector<Box> cauldron() {
    std::vector<Box> out;
    add(out, {0, 3, 0, 2, 16, 16});
    add(out, {14, 3, 0, 16, 16, 16});
    add(out, {2, 3, 0, 14, 16, 2});
    add(out, {2, 3, 14, 14, 16, 16});
    add(out, {2, 3, 2, 14, 4, 14});
    for (int corner = 0; corner < 4; ++corner) {
        add(out, turn({0, 0, 0, 4, 3, 2}, corner));
        add(out, turn({0, 0, 2, 2, 3, 4}, corner));
    }
    return out;
}

std::vector<Box> hopper(const Properties& p) {
    std::vector<Box> out;
    add(out, {0, 10, 0, 2, 16, 16});
    add(out, {14, 10, 0, 16, 16, 16});
    add(out, {2, 10, 0, 14, 16, 2});
    add(out, {2, 10, 14, 14, 16, 16});
    add(out, {2, 10, 2, 14, 11, 14});
    add(out, {4, 4, 4, 12, 10, 12});
    const auto facing = get(p, "facing");
    if (facing == "down") add(out, {6, 0, 6, 10, 4, 10});
    else add(out, turn({6, 4, 0, 10, 8, 4}, quarter_turns(facing)));
    return out;
}

std::vector<Box> amethyst(const Properties& p, std::string_view name) {
    double height = 7;
    double inset = 3;
    if (name.ends_with("large_amethyst_bud")) height = 5;
    else if (name.ends_with("medium_amethyst_bud")) height = 4;
    else if (name.ends_with("small_amethyst_bud")) { height = 3; inset = 4; }
    return {box(point({inset, 0, inset, 16 - inset, height, 16 - inset}, get(p, "facing")))};
}

std::vector<Box> by_family(std::string_view family, const Properties& p, std::string_view name,
                           bool& known) {
    known = true;
    if (family == "full") return {box({0, 0, 0, 16, 16, 16})};
    if (family == "empty") return {};
    if (family.starts_with("box:")) return parse_boxes(family.substr(4), known);
    if (family == "slab") {
        const auto type = get(p, "type");
        if (type == "top") return {box({0, 8, 0, 16, 16, 16})};
        if (type == "double") return {box({0, 0, 0, 16, 16, 16})};
        return {box({0, 0, 0, 16, 8, 16})};
    }
    if (family == "stair") return stair(p);
    if (family == "fence") return cross(p, 6, 10, 24);
    if (family == "fence_outline") return cross(p, 6, 10, 16);
    if (family == "pane") return cross(p, 7, 9, 16);
    if (family == "wall") return wall(p, 24);
    if (family == "wall_outline") return wall(p, 16);
    if (family == "fence_gate" || family == "fence_gate_outline") {
        if (family == "fence_gate" && get(p, "open") == "true") return {};
        const double height = family == "fence_gate" ? 24 : 16;
        const int turns = quarter_turns(get(p, "facing"));
        return {box(turns % 2 == 0 ? Sixteenths{0, 0, 6, 16, height, 10}
                                   : Sixteenths{6, 0, 0, 10, height, 16})};
    }
    if (family == "door") return door(p);
    if (family == "trapdoor") {
        if (get(p, "open") == "true") return {box(against(quarter_turns(get(p, "facing"))))};
        if (get(p, "half") == "top") return {box({0, 13, 0, 16, 16, 16})};
        return {box({0, 0, 0, 16, 3, 16})};
    }
    if (family == "ladder") return {box(against(quarter_turns(get(p, "facing"))))};
    if (family == "snow" || family == "snow_outline") {
        const int layers = get_int(p, "layers", 1) - (family == "snow" ? 1 : 0);
        if (layers <= 0) return {};
        return {box({0, 0, 0, 16, layers * 2.0, 16})};
    }
    if (family == "mossy_carpet") {
        if (get(p, "bottom") == "false") return {};
        return {box({0, 0, 0, 16, 1, 16})};
    }
    if (family == "bed") return {box({0, 0, 0, 16, 9, 16})};
    if (family == "chest") return chest(p);
    if (family == "cake") {
        return {box({1 + 2.0 * get_int(p, "bites", 0), 0, 1, 15, 8, 15})};
    }
    if (family == "candle") {
        switch (get_int(p, "candles", 1)) {
            case 2: return {box({5, 0, 6, 11, 6, 9})};
            case 3: return {box({5, 0, 6, 10, 6, 11})};
            case 4: return {box({5, 0, 5, 11, 6, 10})};
            default: return {box({7, 0, 7, 9, 6, 9})};
        }
    }
    if (family == "sea_pickle") {
        switch (get_int(p, "pickles", 1)) {
            case 2: return {box({3, 0, 3, 13, 6, 13})};
            case 3: return {box({2, 0, 2, 14, 6, 14})};
            case 4: return {box({2, 0, 2, 14, 7, 14})};
            default: return {box({6, 0, 6, 10, 6, 10})};
        }
    }
    if (family == "turtle_egg") {
        return {box(get_int(p, "eggs", 1) == 1 ? Sixteenths{3, 0, 3, 12, 7, 12}
                                                : Sixteenths{1, 0, 1, 15, 7, 15})};
    }
    if (family == "lantern") {
        if (get(p, "hanging") == "true") {
            return {box({5, 1, 5, 11, 8, 11}), box({6, 8, 6, 10, 10, 10})};
        }
        return {box({5, 0, 5, 11, 7, 11}), box({6, 7, 6, 10, 9, 10})};
    }
    if (family == "anvil") {
        if (quarter_turns(get(p, "facing")) % 2 == 1) {
            return {box({2, 0, 2, 14, 4, 14}), box({3, 4, 4, 13, 5, 12}),
                    box({4, 5, 6, 12, 10, 10}), box({0, 10, 3, 16, 16, 13})};
        }
        return {box({2, 0, 2, 14, 4, 14}), box({4, 4, 3, 12, 5, 13}),
                box({6, 5, 4, 10, 10, 12}), box({3, 10, 0, 13, 16, 16})};
    }
    if (family == "piston") {
        if (get(p, "extended") != "true") return {box({0, 0, 0, 16, 16, 16})};
        return {box(point({0, 0, 0, 16, 12, 16}, get(p, "facing")))};
    }
    if (family == "piston_head") {
        const auto facing = get(p, "facing");
        const double arm_low = get(p, "short") == "true" ? 0 : -4;
        return {box(point({0, 12, 0, 16, 16, 16}, facing)),
                box(point({6, std::max(0.0, arm_low), 6, 10, 12, 10}, facing))};
    }
    if (family == "end_portal_frame") {
        if (get(p, "eye") == "true") return {box({0, 0, 0, 16, 13, 16}), box({4, 13, 4, 12, 16, 12})};
        return {box({0, 0, 0, 16, 13, 16})};
    }
    if (family == "hopper") return hopper(p);
    if (family == "cauldron") return cauldron();
    if (family == "composter") {
        std::vector<Box> out;
        add(out, {0, 0, 0, 2, 16, 16});
        add(out, {14, 0, 0, 16, 16, 16});
        add(out, {2, 0, 0, 14, 16, 2});
        add(out, {2, 0, 14, 14, 16, 16});
        add(out, {2, 0, 2, 14, 2, 14});
        return out;
    }
    if (family == "chain") {
        const auto axis = get(p, "axis");
        if (axis == "x") return {box({0, 6.5, 6.5, 16, 9.5, 9.5})};
        if (axis == "z") return {box({6.5, 6.5, 0, 9.5, 9.5, 16})};
        return {box({6.5, 0, 6.5, 9.5, 16, 9.5})};
    }
    if (family == "rod") return {box(point({6, 0, 6, 10, 16, 10}, get(p, "facing")))};
    if (family == "amethyst") return amethyst(p, name);
    if (family == "pointed_dripstone") return {box({5, 0, 5, 11, 16, 11})};
    if (family == "bamboo") return {box({6.5, 0, 6.5, 9.5, 16, 9.5})};
    if (family == "scaffolding") {
        return {box({0, 14, 0, 16, 16, 16}), box({0, 0, 0, 2, 16, 2}), box({14, 0, 0, 16, 16, 2}),
                box({0, 0, 14, 2, 16, 16}), box({14, 0, 14, 16, 16, 16})};
    }
    if (family == "big_dripleaf") {
        const auto tilt = get(p, "tilt");
        if (tilt == "full") return {};
        if (tilt == "partial") return {box({0, 11, 0, 16, 13, 16})};
        return {box({0, 11, 0, 16, 15, 16})};
    }
    if (family == "cocoa") {
        const int age = get_int(p, "age", 0);
        const Sixteenths sizes[] = {{6, 7, 1, 10, 12, 5}, {5, 5, 1, 11, 12, 7}, {4, 3, 1, 12, 12, 9}};
        return {box(turn(sizes[std::clamp(age, 0, 2)], quarter_turns(get(p, "facing"))))};
    }
    if (family == "chorus_plant") {
        std::vector<Box> out;
        add(out, {3, 3, 3, 13, 13, 13});
        if (get(p, "north") == "true") add(out, {3, 3, 0, 13, 13, 3});
        if (get(p, "south") == "true") add(out, {3, 3, 13, 13, 13, 16});
        if (get(p, "west") == "true") add(out, {0, 3, 3, 3, 13, 13});
        if (get(p, "east") == "true") add(out, {13, 3, 3, 16, 13, 13});
        if (get(p, "up") == "true") add(out, {3, 13, 3, 13, 16, 13});
        if (get(p, "down") == "true") add(out, {3, 0, 3, 13, 3, 13});
        return out;
    }
    if (family == "wall_skull") {
        return {box(turn({4, 4, 8, 12, 12, 16}, quarter_turns(get(p, "facing"))))};
    }
    if (family == "shulker_box") return {box({0, 0, 0, 16, 16, 16})};
    known = false;
    return {};
}

// The union of the model's elements: right for the blocks whose collision is
// what they look like, and a fair outline for the ones with no collision.
std::vector<Box> model_boxes(const BlockTable* models, int32_t state, bool bounds_only) {
    std::vector<Box> out;
    if (models == nullptr) return out;
    constexpr double kUnits = static_cast<double>(kUnitsPerBlock);
    const auto rotate = [](double p[3], int rx, int ry) {
        double x = p[0] - 0.5;
        double y = p[1] - 0.5;
        double z = p[2] - 0.5;
        const auto quarter = [](double& a, double& b, int turns) {
            for (int i = 0; i < turns; ++i) {
                const double old_a = a;
                a = b;
                b = -old_a;
            }
        };
        quarter(y, z, rx);
        quarter(z, x, ry);
        p[0] = x + 0.5;
        p[1] = y + 0.5;
        p[2] = z + 0.5;
    };
    for (const auto& quad : models->quads(state)) {
        double a[3];
        double b[3];
        for (int i = 0; i < 3; ++i) {
            a[i] = quad.from[i] / kUnits;
            b[i] = quad.to[i] / kUnits;
        }
        rotate(a, quad.rotation_x, quad.rotation_y);
        rotate(b, quad.rotation_x, quad.rotation_y);
        Box candidate{};
        for (int i = 0; i < 3; ++i) {
            candidate.min[i] = std::clamp(std::min(a[i], b[i]), 0.0, 1.0);
            candidate.max[i] = std::clamp(std::max(a[i], b[i]), 0.0, 1.0);
        }
        // A flat face of a cross or a pane is not something to stand on.
        bool flat = false;
        for (int i = 0; i < 3; ++i) flat = flat || candidate.max[i] - candidate.min[i] < 1e-6;
        if (flat && !bounds_only) continue;
        const bool seen = std::ranges::any_of(out, [&](const Box& other) {
            for (int i = 0; i < 3; ++i) {
                if (std::abs(other.min[i] - candidate.min[i]) > 1e-9 ||
                    std::abs(other.max[i] - candidate.max[i]) > 1e-9) {
                    return false;
                }
            }
            return true;
        });
        if (!seen) out.push_back(candidate);
    }
    if (bounds_only && !out.empty()) {
        Box all = out.front();
        for (const auto& b : out) {
            for (int i = 0; i < 3; ++i) {
                all.min[i] = std::min(all.min[i], b.min[i]);
                all.max[i] = std::max(all.max[i], b.max[i]);
            }
        }
        out = {all};
    }
    return out;
}

struct Row {
    std::string shape;
    std::string outline;
    BlockPhysics physics;
};

// The hand tables, read once.
struct Tables {
    std::map<std::string, Row, std::less<>> shapes;
    std::map<std::string, DigInfo, std::less<>> dig;

    Tables() {
        std::istringstream lines{std::string(kCollisionShapes)};
        std::string line;
        while (std::getline(lines, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream fields(line);
            std::string name;
            Row row;
            fields >> name >> row.shape;
            std::string extra;
            while (fields >> extra) {
                const auto equals = extra.find('=');
                const std::string key = extra.substr(0, equals);
                const std::string value =
                    equals == std::string::npos ? std::string{} : extra.substr(equals + 1);
                const float number = std::strtof(value.c_str(), nullptr);
                if (key == "outline") row.outline = value;
                else if (key == "friction") row.physics.friction = number;
                else if (key == "speed") row.physics.speed = number;
                else if (key == "jump") row.physics.jump = number;
                else if (key == "climb") row.physics.climbable = true;
                else if (key == "bounce") {
                    row.physics.slime = value == "slime";
                    row.physics.bed = value == "bed";
                } else if (key == "slide") row.physics.honey = true;
                else if (key == "offset") row.physics.offset = number;
                else if (key == "stuck") {
                    std::sscanf(value.c_str(), "%f,%f,%f", &row.physics.stuck[0],
                                &row.physics.stuck[1], &row.physics.stuck[2]);
                }
            }
            shapes.emplace(std::move(name), std::move(row));
        }

        std::istringstream dig_lines{std::string(kHardness)};
        while (std::getline(dig_lines, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream fields(line);
            std::string name;
            DigInfo info;
            std::string tool;
            fields >> name >> info.hardness >> tool;
            info.needs_tool = tool == "tool";
            dig.emplace(std::move(name), info);
        }
    }
};

const Tables& tables() {
    static const Tables kTables;
    return kTables;
}

const BlockPhysics kNothing{};
const DigInfo kNoDig{};

bool waterlogged(std::string_view name, const Properties& p) {
    if (get(p, "waterlogged") == "true") return true;
    return name == "minecraft:kelp" || name == "minecraft:kelp_plant" ||
           name == "minecraft:seagrass" || name == "minecraft:tall_seagrass" ||
           name == "minecraft:bubble_column";
}

} // namespace

std::vector<Box> family_shape(std::string_view family, const Properties& properties,
                              bool* known) {
    bool ok = true;
    auto out = by_family(family, properties, {}, ok);
    if (known != nullptr) *known = ok;
    return out;
}

void random_offset(int32_t x, int32_t z, float max, double& dx, double& dz) {
    // Wrapping arithmetic, as the game's own integers wrap.
    const auto spread_x = static_cast<int32_t>(static_cast<uint32_t>(x) * 3129871u);
    uint64_t mixed = static_cast<uint64_t>(static_cast<int64_t>(spread_x)) ^
                     (static_cast<uint64_t>(static_cast<int64_t>(z)) * 116129781ull);
    mixed = mixed * mixed * 42317861ull + mixed * 11ull;
    const int64_t seed = static_cast<int64_t>(mixed) >> 16;
    dx = std::clamp((static_cast<double>(seed & 15) / 15.0 - 0.5) * 0.5,
                    -static_cast<double>(max), static_cast<double>(max));
    dz = std::clamp((static_cast<double>((seed >> 8) & 15) / 15.0 - 0.5) * 0.5,
                    -static_cast<double>(max), static_cast<double>(max));
}

void BlockShapes::define(int32_t state, std::string_view name, const Properties& properties,
                         const BlockTable* models, int32_t block) {
    if (state < 0) return;
    if (static_cast<size_t>(state) >= states_.size()) states_.resize(static_cast<size_t>(state) + 1);
    auto& entry = states_[static_cast<size_t>(state)];
    entry.name = name;
    entry.properties = properties;
    entry.block = block;

    const auto& t = tables();
    const auto found = t.shapes.find(name);
    std::string_view family = found != t.shapes.end() ? std::string_view(found->second.shape)
                                                      : std::string_view("full");
    if (found != t.shapes.end()) entry.physics = found->second.physics;
    bool known = true;
    std::vector<Box> collision = family == "model" ? model_boxes(models, state, false)
                                                   : by_family(family, properties, name, known);
    if (!known) collision = {box({0, 0, 0, 16, 16, 16})};

    std::vector<Box> outline;
    std::string_view outline_family =
        found != t.shapes.end() ? std::string_view(found->second.outline) : std::string_view{};
    if (outline_family.empty()) {
        if (family == "fence") outline_family = "fence_outline";
        else if (family == "wall") outline_family = "wall_outline";
        else if (family == "fence_gate") outline_family = "fence_gate_outline";
        else if (family == "snow") outline_family = "snow_outline";
        else if (family == "scaffolding") outline_family = "full";
    }
    if (!outline_family.empty()) {
        outline = outline_family == "model" ? model_boxes(models, state, true)
                                            : by_family(outline_family, properties, name, known);
    } else if (!collision.empty()) {
        outline = collision;
    } else if (name != "minecraft:air" && name != "minecraft:cave_air" &&
               name != "minecraft:void_air" && name != "minecraft:water" &&
               name != "minecraft:lava" && name != "minecraft:bubble_column" &&
               name != "minecraft:light" && name != "minecraft:structure_void" &&
               name != "minecraft:moving_piston") {
        outline = model_boxes(models, state, true);
    }

    entry.collision_first = static_cast<uint32_t>(boxes_.size());
    entry.collision_count = static_cast<uint32_t>(collision.size());
    boxes_.insert(boxes_.end(), collision.begin(), collision.end());
    entry.outline_first = static_cast<uint32_t>(boxes_.size());
    entry.outline_count = static_cast<uint32_t>(outline.size());
    boxes_.insert(boxes_.end(), outline.begin(), outline.end());

    auto& physics = entry.physics;
    physics.scaffolding = family == "scaffolding";
    physics.powder_snow = name == "minecraft:powder_snow";
    if (name == "minecraft:bamboo") physics.offset = 0.25f;
    if (name == "minecraft:pointed_dripstone") physics.offset = 0.125f;
    if (name == "minecraft:water" || name == "minecraft:lava") {
        physics.fluid = name == "minecraft:water" ? FluidKind::Water : FluidKind::Lava;
        const int level = get_int(properties, "level", 0);
        physics.fluid_amount = static_cast<uint8_t>(level == 0 || level >= 8 ? 8 : 8 - level);
    } else if (waterlogged(name, properties)) {
        physics.fluid = FluidKind::Water;
        physics.fluid_amount = 8;
    }
    if (name == "minecraft:bubble_column") {
        physics.bubbles = get(properties, "drag") == "true" ? -1 : 1;
    }

    const auto dig = t.dig.find(name);
    if (dig != t.dig.end()) entry.dig = dig->second;
}

bool BlockShapes::open(const std::filesystem::path& path, const BlockTable* models) {
    std::ifstream in(path);
    if (!in) return false;
    states_.clear();
    boxes_.clear();
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream fields(line);
        int32_t state = -1;
        int32_t block = -1;
        std::string name;
        std::string props;
        fields >> state >> block >> name;
        std::getline(fields >> std::ws, props);
        Properties properties;
        size_t at = 0;
        while (at < props.size()) {
            const auto comma = props.find(',', at);
            const auto pair = props.substr(at, comma == std::string::npos ? std::string::npos
                                                                         : comma - at);
            const auto equals = pair.find('=');
            if (equals != std::string::npos) {
                properties.emplace_back(pair.substr(0, equals), pair.substr(equals + 1));
            }
            if (comma == std::string::npos) break;
            at = comma + 1;
        }
        define(state, name, properties, models, block);
    }
    return !states_.empty();
}

std::filesystem::path BlockShapes::default_path(int32_t protocol) {
    return BlockTable::default_path(protocol).parent_path() / std::format("states-{}.txt", protocol);
}

std::span<const Box> BlockShapes::collision(int32_t state) const {
    if (state < 0 || static_cast<size_t>(state) >= states_.size()) return {};
    const auto& s = states_[static_cast<size_t>(state)];
    return {boxes_.data() + s.collision_first, s.collision_count};
}

std::span<const Box> BlockShapes::outline(int32_t state) const {
    if (state < 0 || static_cast<size_t>(state) >= states_.size()) return {};
    const auto& s = states_[static_cast<size_t>(state)];
    return {boxes_.data() + s.outline_first, s.outline_count};
}

const BlockPhysics& BlockShapes::physics(int32_t state) const {
    if (state < 0 || static_cast<size_t>(state) >= states_.size()) return kNothing;
    return states_[static_cast<size_t>(state)].physics;
}

const DigInfo& BlockShapes::dig(int32_t state) const {
    if (state < 0 || static_cast<size_t>(state) >= states_.size()) return kNoDig;
    return states_[static_cast<size_t>(state)].dig;
}

std::string_view BlockShapes::name(int32_t state) const {
    if (state < 0 || static_cast<size_t>(state) >= states_.size()) return {};
    return states_[static_cast<size_t>(state)].name;
}

std::string_view BlockShapes::property(int32_t state, std::string_view key) const {
    if (state < 0 || static_cast<size_t>(state) >= states_.size()) return {};
    return get(states_[static_cast<size_t>(state)].properties, key);
}

int32_t BlockShapes::block(int32_t state) const {
    if (state < 0 || static_cast<size_t>(state) >= states_.size()) return -1;
    return states_[static_cast<size_t>(state)].block;
}

int32_t BlockShapes::with(int32_t state, std::string_view key, std::string_view value) const {
    if (state < 0 || static_cast<size_t>(state) >= states_.size()) return -1;
    const auto& from = states_[static_cast<size_t>(state)];
    // A block's states are numbered next to each other.
    int32_t first = state;
    while (first > 0 && states_[static_cast<size_t>(first - 1)].name == from.name) --first;
    for (auto i = static_cast<size_t>(first); i < states_.size() && states_[i].name == from.name;
         ++i) {
        bool match = get(states_[i].properties, key) == value;
        for (const auto& [k, v] : from.properties) {
            if (k != key && get(states_[i].properties, k) != v) match = false;
        }
        if (match) return static_cast<int32_t>(i);
    }
    return -1;
}

int32_t BlockShapes::find(std::string_view block_name) const {
    for (size_t i = 0; i < states_.size(); ++i) {
        if (states_[i].name == block_name) return static_cast<int32_t>(i);
    }
    return -1;
}

bool BlockShapes::full_collision(int32_t state) const {
    const auto boxes = collision(state);
    if (boxes.size() != 1) return false;
    const auto& b = boxes.front();
    return b.min[0] == 0.0 && b.min[1] == 0.0 && b.min[2] == 0.0 && b.max[0] == 1.0 &&
           b.max[1] == 1.0 && b.max[2] == 1.0;
}

} // namespace mcpp::world
