#include "mcpp/entity/humanoid.hpp"
#include "mcpp/entity/model.hpp"
#include "mcpp/entity/mobs.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// The C++ half of ModelOracle: the layers from entity_models-774.txt baked by
// our cube, and our own models' animation on the cases in model_cases.txt,
// values only.
namespace {

using mcpp::entity::MobState;

int32_t bits(float v) { return std::bit_cast<int32_t>(v); }

std::vector<std::string> rows(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        std::cerr << "no " << path.string() << '\n';
        std::exit(1);
    }
    std::vector<std::string> out;
    std::string line;
    while (std::getline(input, line)) {
        const size_t from = line.find_first_not_of(" \t\r");
        if (from == std::string::npos || line[from] == '#') continue;
        const size_t to = line.find_last_not_of(" \t\r");
        out.push_back(line.substr(from, to - from + 1));
    }
    return out;
}

std::vector<std::string> words(std::string_view line) {
    std::vector<std::string> out;
    size_t at = 0;
    while (at < line.size()) {
        while (at < line.size() && (line[at] == ' ' || line[at] == '\t')) ++at;
        size_t end = at;
        while (end < line.size() && line[end] != ' ' && line[end] != '\t') ++end;
        if (end > at) out.emplace_back(line.substr(at, end - at));
        at = end;
    }
    return out;
}

// One field of `MobState` by the name the case table gives it, which is the
// field's own name; the oracle sets the matching field of the layer's vanilla
// render state.
void set(MobState& s, const std::string& key, float v) {
    using mcpp::entity::ArmPose;
    using mcpp::entity::SwingAnimation;
    const bool flag = v != 0.0f;
    const auto whole = static_cast<int32_t>(v);
    if (key == "age_in_ticks") {
        s.age_in_ticks = v;
        s.tick = static_cast<int64_t>(std::floor(v));
        s.partial = v - std::floor(v);
    } else if (key == "bounding_box_width") { s.bounding_box_width = v;
    } else if (key == "bounding_box_height") { s.bounding_box_height = v;
    } else if (key == "body_rot") { s.body_rot = v;
    } else if (key == "y_rot") { s.y_rot = v;
    } else if (key == "x_rot") { s.x_rot = v;
    } else if (key == "death_time") { s.death_time = v;
    } else if (key == "walk_position") { s.walk_position = v;
    } else if (key == "walk_speed") { s.walk_speed = v;
    } else if (key == "scale") { s.scale = v;
    } else if (key == "age_scale") { s.age_scale = v;
    } else if (key == "baby") { s.baby = flag;
    } else if (key == "in_water") { s.in_water = flag;
    } else if (key == "upside_down") { s.upside_down = flag;
    } else if (key == "fully_frozen") { s.fully_frozen = flag;
    } else if (key == "auto_spin_attack") { s.auto_spin_attack = flag;
    } else if (key == "entity_id") { s.entity_id = whole;
    } else if (key == "main_arm_right") { s.main_arm_right = flag;
    } else if (key == "right_arm_pose") { s.right_arm_pose = static_cast<ArmPose>(whole);
    } else if (key == "left_arm_pose") { s.left_arm_pose = static_cast<ArmPose>(whole);
    } else if (key == "swing_animation") { s.swing_animation = static_cast<SwingAnimation>(whole);
    } else if (key == "attack_time") { s.attack_time = v;
    } else if (key == "right_hand_item_empty") { s.right_hand_item_empty = flag;
    } else if (key == "left_hand_item_empty") { s.left_hand_item_empty = flag;
    } else if (key == "swim_amount") { s.swim_amount = v;
    } else if (key == "speed_value") { s.speed_value = v;
    } else if (key == "max_crossbow_charge_duration") { s.max_crossbow_charge_duration = v;
    } else if (key == "ticks_using_item") { s.ticks_using_item = v;
    } else if (key == "attack_arm_left") { s.attack_arm_left = flag;
    } else if (key == "use_item_off_hand") { s.use_item_off_hand = flag;
    } else if (key == "crouching") { s.crouching = flag;
    } else if (key == "fall_flying") { s.fall_flying = flag;
    } else if (key == "visually_swimming") { s.visually_swimming = flag;
    } else if (key == "passenger") { s.passenger = flag;
    } else if (key == "using_item") { s.using_item = flag;
    } else if (key == "fall_flying_ticks") { s.fall_flying_ticks = v;
    } else if (key == "spectator") { s.spectator = flag;
    } else if (key == "show_hat") { s.show_hat = flag;
    } else if (key == "show_jacket") { s.show_jacket = flag;
    } else if (key == "show_left_sleeve") { s.show_left_sleeve = flag;
    } else if (key == "show_right_sleeve") { s.show_right_sleeve = flag;
    } else if (key == "show_left_pants") { s.show_left_pants = flag;
    } else if (key == "show_right_pants") { s.show_right_pants = flag;
    } else if (key == "aggressive") { s.aggressive = flag;
    } else if (key == "converting") { s.converting = flag;
    } else if (key == "shaking") { s.shaking = flag;
    } else if (key == "holding_bow") { s.holding_bow = flag;
    } else if (key == "sheared") { s.sheared = flag;
    } else if (key == "swelling") { s.swelling = v;
    } else if (key == "powered") { s.powered = flag;
    } else if (key == "squish") { s.squish = v;
    } else if (key == "slime_size") { s.slime_size = whole;
    } else if (key == "creepy") { s.creepy = flag;
    } else if (key == "carries_block") { s.carries_block = flag;
    } else if (key == "head_eat_position_scale") { s.head_eat_position_scale = v;
    } else if (key == "head_eat_angle_scale") { s.head_eat_angle_scale = v;
    } else if (key == "flap") { s.flap = v;
    } else if (key == "flap_speed") { s.flap_speed = v;
    } else if (key == "saddled") { s.saddled = flag;
    } else if (key == "ridden") { s.ridden = flag;
    } else if (key == "animate_tail") { s.animate_tail = flag;
    } else if (key == "eat_animation") { s.eat_animation = v;
    } else if (key == "stand_animation") { s.stand_animation = v;
    } else if (key == "feeding_animation") { s.feeding_animation = v;
    } else if (key == "angry") { s.angry = flag;
    } else if (key == "sitting") { s.sitting = flag;
    } else if (key == "tail_angle") { s.tail_angle = v;
    } else if (key == "head_roll_angle") { s.head_roll_angle = v;
    } else if (key == "shake_anim") { s.shake_anim = v;
    } else if (key == "wet_shade") { s.wet_shade = v;
    } else if (key == "lying_on_top_of_sleeping_player") {
        s.lying_on_top_of_sleeping_player = flag;
    } else if (key == "has_chest") { s.has_chest = flag;
    } else if (key == "sprinting") { s.sprinting = flag;
    } else if (key == "lie_down_amount") { s.lie_down_amount = v;
    } else if (key == "lie_down_amount_tail") { s.lie_down_amount_tail = v;
    } else if (key == "relax_state_one_amount") { s.relax_state_one_amount = v;
    } else if (key == "unhappy") { s.unhappy = flag;
    } else if (key == "holding_item") { s.holding_item = flag;
    } else if (key == "holding_potion") { s.holding_potion = flag;
    } else if (key == "attack_ticks_remaining") { s.attack_ticks_remaining = v;
    } else if (key == "offer_flower_tick") { s.offer_flower_tick = whole;
    } else if (key == "roll_amount") { s.roll_amount = v;
    } else if (key == "has_stinger") { s.has_stinger = flag;
    } else if (key == "on_ground") { s.on_ground = flag;
    } else if (key == "has_nectar") { s.has_nectar = flag;
    } else if (key == "crouch_amount") { s.crouch_amount = v;
    } else if (key == "sleeping") { s.sleeping = flag;
    } else if (key == "faceplanted") { s.faceplanted = flag;
    } else if (key == "pouncing") { s.pouncing = flag;
    } else if (key == "playing_dead_factor") { s.playing_dead_factor = v;
    } else if (key == "moving_factor") { s.moving_factor = v;
    } else if (key == "in_water_factor") { s.in_water_factor = v;
    } else if (key == "on_ground_factor") { s.on_ground_factor = v;
    } else if (key == "dancing") { s.dancing = flag;
    } else if (key == "spinning") { s.spinning = flag;
    } else if (key == "spinning_progress") { s.spinning_progress = v;
    } else if (key == "holding_animation_progress") { s.holding_animation_progress = v;
    } else if (key == "hiding_in_shell") { s.hiding_in_shell = flag;
    } else if (key == "roll_out") { s.roll_out.start(whole);
    } else if (key == "roll_up") { s.roll_up.start(whole);
    } else if (key == "peek") { s.peek.start(whole);
    } else if (key == "breeze_idle") { s.breeze_idle.start(whole);
    } else if (key == "breeze_shoot") { s.breeze_shoot.start(whole);
    } else if (key == "breeze_slide") { s.breeze_slide.start(whole);
    } else if (key == "breeze_slide_back") { s.breeze_slide_back.start(whole);
    } else if (key == "breeze_inhale") { s.breeze_inhale.start(whole);
    } else if (key == "breeze_long_jump") { s.breeze_long_jump.start(whole);
    } else if (key == "golem_idle") { s.golem_idle.start(whole);
    } else if (key == "golem_get_item") { s.golem_get_item.start(whole);
    } else if (key == "golem_get_no_item") { s.golem_get_no_item.start(whole);
    } else if (key == "golem_drop_item") { s.golem_drop_item.start(whole);
    } else if (key == "golem_drop_no_item") { s.golem_drop_no_item.start(whole);
    } else if (key == "facing") { s.facing = static_cast<int8_t>(whole);
    } else if (key == "wiggle") { s.wiggle = v;
    } else if (key == "marker") { s.marker = flag;
    } else if (key == "small") { s.baby = flag;
    } else if (key == "show_arms") { s.show_arms = flag;
    } else if (key == "show_base_plate") { s.show_base_plate = flag;
    } else if (key.starts_with("stand_")) {
        // `ArmorStandRenderState`'s six `Rotations`, one key per axis:
        // "stand_<limb>_<axis>".
        const std::string rest = key.substr(6);
        const std::string limb = rest.substr(0, rest.rfind('_'));
        const size_t at = limb == "head"        ? 0
                          : limb == "body"      ? 1
                          : limb == "left_arm"  ? 2
                          : limb == "right_arm" ? 3
                          : limb == "left_leg"  ? 4
                                                : 5;
        const char axis = rest.back();
        s.stand_poses[at][axis == 'x' ? 0 : axis == 'y' ? 1 : 2] = v;
    } else {
        std::cerr << "MobState has no field for " << key << '\n';
        std::exit(1);
    }
}

std::filesystem::path data_dir() {
    std::filesystem::path base;
    if (const char* data_home = std::getenv("XDG_DATA_HOME")) {
        base = data_home;
    } else if (const char* home = std::getenv("HOME")) {
        base = std::filesystem::path(home) / ".local" / "share";
    }
    return base / "plasma-client" / "data";
}

} // namespace

int main() {
    const auto table = data_dir() / "entity_models-774.txt";
    std::ifstream input(table);
    if (!input) {
        std::cerr << "no " << table.string() << "; bake it with tools/bake_all.py\n";
        return 1;
    }
    std::stringstream text;
    text << input.rdbuf();
    mcpp::entity::ModelSet models;
    models.parse(text.str());

    const std::filesystem::path here("tools/oracle");
    std::vector<std::string> layers;
    for (const std::string& row : rows(here / "model_layers.txt")) {
        layers.push_back(words(row)[0]);
    }

    for (const std::string& name : layers) {
        const auto* model = models.find(name);
        if (model == nullptr) {
            std::cerr << "the baked table has no layer " << name << '\n';
            return 1;
        }
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

    std::string current;
    const mcpp::entity::Model* model = nullptr;
    std::unique_ptr<mcpp::entity::MobModel> mob;
    mcpp::entity::Pose pose;
    for (const std::string& row : rows(here / "model_cases.txt")) {
        const auto fields = words(row);
        if (fields[0] != current) {
            current = fields[0];
            model = models.find(current);
            if (model == nullptr) {
                std::cerr << "the baked table has no layer " << current << '\n';
                return 1;
            }
            mob = mcpp::entity::make_mob_model(current, *model);
            if (mob == nullptr) {
                std::cerr << "no model class animates " << current << '\n';
                return 1;
            }
            pose = mcpp::entity::Pose(*model);
        }
        MobState state;
        for (size_t i = 1; i < fields.size(); ++i) {
            const size_t equals = fields[i].find('=');
            set(state, fields[i].substr(0, equals),
                std::strtof(fields[i].c_str() + equals + 1, nullptr));
        }
        mob->animate(state, pose);
        for (size_t i = 0; i < model->parts.size(); ++i) {
            const auto& p = pose.parts[i];
            std::cout << bits(p.x) << ',' << bits(p.y) << ',' << bits(p.z) << ',' << bits(p.x_rot)
                      << ',' << bits(p.y_rot) << ',' << bits(p.z_rot) << ',' << bits(p.x_scale)
                      << ',' << bits(p.y_scale) << ',' << bits(p.z_scale) << ','
                      << (p.visible ? 1 : 0) << ',' << (p.skip_draw ? 1 : 0) << '\n';
        }
    }
}
