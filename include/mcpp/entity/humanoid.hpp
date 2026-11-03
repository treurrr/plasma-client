#pragma once
#include "mcpp/entity/model.hpp"

#include <cstdint>

namespace mcpp::entity {

enum class ArmPose : uint8_t { Empty, Item, Block };

// What the humanoid's animation reads, for one frame.
struct HumanoidState {
    float x_rot = 0.0f;   // pitch, degrees
    float y_rot = 0.0f;   // head yaw minus body yaw, degrees
    float walk_position = 0.0f;
    float walk_speed = 0.0f;
    float age_in_ticks = 0.0f;
    float attack_time = 0.0f;
    float swim_amount = 0.0f;
    float speed_value = 1.0f;
    float age_scale = 1.0f;
    bool crouching = false;
    bool fall_flying = false;
    bool passenger = false;
    bool attack_left = false;   // the arm that swings is the left one
    bool main_arm_right = true;
    bool using_item = false;
    ArmPose left_arm = ArmPose::Empty;
    ArmPose right_arm = ArmPose::Empty;

    // Players only.
    bool spectator = false;
    bool show_hat = true;
    bool show_jacket = true;
    bool show_left_sleeve = true;
    bool show_right_sleeve = true;
    bool show_left_pants = true;
    bool show_right_pants = true;
};

// The humanoid's parts by index, found once per model.
struct HumanoidParts {
    int32_t head = -1, hat = -1, body = -1, right_arm = -1, left_arm = -1, right_leg = -1,
            left_leg = -1;
    int32_t jacket = -1, left_sleeve = -1, right_sleeve = -1, left_pants = -1, right_pants = -1;

    static HumanoidParts of(const Model& model);
    bool complete() const;
};

// The game's humanoid animation, step for step: head, walk, riding, arm
// poses, the attack swing, sneaking, the idle bob and swimming. The pose is
// reset to the layer's own first.
void animate_humanoid(const Model& model, const HumanoidParts& parts, const HumanoidState& state,
                      Pose& pose);
// The player's on top: which layers of the skin show, then the humanoid.
void animate_player(const Model& model, const HumanoidParts& parts, const HumanoidState& state,
                    Pose& pose);

} // namespace mcpp::entity
