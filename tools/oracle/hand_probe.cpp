// The C++ half of HandOracle: every pose the first person hand builds
// (client/hand.cpp), as the sixteen floats of the matrix. Reads the oracle's
// lines on stdin and answers one line each, in order.
#include "client/hand.hpp"

#include <bit>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

using mcpp::client::Arm;
using mcpp::client::Matrix;

std::string packed(const Matrix& pose) {
    std::string out;
    for (size_t i = 0; i < pose.size(); ++i) {
        if (i > 0) out += ',';
        out += std::to_string(std::bit_cast<int32_t>(pose[i]));
    }
    return out;
}

Arm arm_of(const std::string& name) { return name == "right" ? Arm::Right : Arm::Left; }

} // namespace

int main() {
    int use_duration = 32;
    for (std::string line; std::getline(std::cin, line);) {
        if (line.empty()) continue;
        std::istringstream fields(line);
        std::string what;
        fields >> what;
        mcpp::client::Pose pose;
        if (what == "useduration") {
            fields >> use_duration;
            std::cout << use_duration << '\n';
            continue;
        }
        if (what == "armrest") {
            std::string side;
            float equipped = 0.0f;
            fields >> side >> equipped;
            mcpp::client::apply_item_arm_transform(pose, arm_of(side), equipped);
        } else if (what == "armattack") {
            std::string side;
            float swing = 0.0f;
            fields >> side >> swing;
            mcpp::client::apply_item_arm_attack_transform(pose, arm_of(side), swing);
        } else if (what == "swing") {
            std::string side;
            float swing = 0.0f;
            fields >> side >> swing;
            const Arm arm = arm_of(side);
            mcpp::client::swing_arm(pose, swing, arm == Arm::Right ? 1 : -1, arm);
        } else if (what == "playerarm") {
            std::string side;
            float equipped = 0.0f;
            float swing = 0.0f;
            fields >> side >> equipped >> swing;
            mcpp::client::player_arm_transform(pose, equipped, swing, arm_of(side));
        } else if (what == "eat") {
            std::string side;
            int remaining = 0;
            float partial = 0.0f;
            fields >> side >> remaining >> partial;
            mcpp::client::apply_eat_transform(pose, partial, arm_of(side), remaining,
                                              use_duration);
        } else if (what == "brush") {
            std::string side;
            int remaining = 0;
            float partial = 0.0f;
            fields >> side >> remaining >> partial;
            mcpp::client::apply_brush_transform(pose, partial, arm_of(side), remaining);
        } else if (what == "hurt") {
            int hurt_time = 0;
            float partial = 0.0f;
            fields >> hurt_time >> partial;
            mcpp::client::bob_hurt(pose, partial, hurt_time, 10, 37.5f, 1.0, false, 0);
        } else if (what == "bobview") {
            float walk = 0.0f;
            float amount = 0.0f;
            fields >> walk >> amount;
            mcpp::client::bob_view(pose, walk, amount);
        } else if (what == "heldarm" || what == "held") {
            const bool with_display = what == "held";
            std::string side;
            float swing = 0.0f;
            float equipped = 0.0f;
            float rotation[3]{};
            float translation[3]{};
            float uniform = 1.0f;
            fields >> side >> swing >> equipped;
            if (with_display) {
                fields >> rotation[0] >> rotation[1] >> rotation[2] >> translation[0]
                    >> translation[1] >> translation[2] >> uniform;
            }
            const Arm arm = arm_of(side);
            mcpp::client::HandPlayer player;
            player.partial_tick = 1.0f;
            player.main_item = 1;
            player.main_arm = arm;
            player.attack_anim = swing;
            player.view_bobbing = false;
            // A fresh clock leaves the hand all the way down, so the swap
            // scale is what the equipped height comes out as.
            player.main_swap_animation_scale = equipped;
            mcpp::client::HandBob bob;
            // One tick is what it takes for the hand to be holding the item
            // at all; the heights stay down, so equipped is the swap scale.
            bob.tick(player);
            mcpp::client::HandDraw draws[2];
            int count = 0;
            mcpp::client::hands_with_items(player, bob, draws, count);
            if (count == 0) {
                std::cout << "?\n";
                continue;
            }
            pose.matrix = draws[0].pose;
            pose.identity = false;
            pose.moved_only = false;
            if (with_display) {
                mcpp::client::ItemDisplay display;
                display.present = true;
                for (int i = 0; i < 3; ++i) {
                    display.rotation[i] = rotation[i];
                    display.translation[i] = translation[i] * 0.0625f;
                    display.scale[i] = uniform;
                }
                mcpp::client::apply_display_transform(pose, display, arm == Arm::Left);
            }
        } else if (what == "display") {
            float rotation[3]{};
            float translation[3]{};
            float uniform = 1.0f;
            int left = 0;
            fields >> rotation[0] >> rotation[1] >> rotation[2] >> translation[0]
                >> translation[1] >> translation[2] >> uniform >> left;
            mcpp::client::ItemDisplay display;
            display.present = true;
            for (int i = 0; i < 3; ++i) {
                display.rotation[i] = rotation[i];
                display.translation[i] = translation[i] * 0.0625f;
                display.scale[i] = uniform;
            }
            mcpp::client::apply_display_transform(pose, display, left != 0);
        } else {
            std::cout << "?\n";
            continue;
        }
        std::cout << packed(pose.matrix) << "\n";
    }
    return 0;
}
