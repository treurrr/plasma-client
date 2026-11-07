#pragma once
#include "mcpp/entity/mobs.hpp"

#include <array>
#include <cstdint>

namespace mcpp::entity {

// `HumanoidModel`: the body the player, every zombie, every skeleton, the
// villager, the witch and the armour layers all animate from.
class Humanoid : public MobModel {
public:
    explicit Humanoid(const Model& model);

    void animate(const MobState& state, Pose& pose) const override;
    std::span<const int32_t> hand_chain(bool left) const override;

    // `HumanoidModel.setAllVisible`.
    void set_all_visible(Pose& pose, bool visible) const;

    int32_t head() const { return head_; }
    int32_t hat() const { return hat_; }
    int32_t body() const { return body_; }
    int32_t arm(bool left) const { return left ? left_arm_ : right_arm_; }
    int32_t leg(bool left) const { return left ? left_leg_ : right_leg_; }

protected:
    // `HumanoidModel.setupAttackAnimation`: the zombie and the iron golem
    // replace it wholesale.
    virtual void setup_attack(const MobState& state, Pose& pose) const;
    // What `setupAnim` does before the arm poses, for a subclass that wants
    // the whole thing and then its own on top.
    void humanoid_anim(const MobState& state, Pose& pose) const;

    int32_t head_ = -1, hat_ = -1, body_ = -1;
    int32_t right_arm_ = -1, left_arm_ = -1, right_leg_ = -1, left_leg_ = -1;

private:
    void pose_right_arm(const MobState& state, Pose& pose) const;
    void pose_left_arm(const MobState& state, Pose& pose) const;
    void pose_blocking_arm(PartPose& arm, const PartPose& head, bool right) const;

    std::array<int32_t, 1> right_chain_{};
    std::array<int32_t, 1> left_chain_{};
};

// `AvatarModel`: the player, whose skin carries a second layer over every
// part and who can be a spectator.
class Avatar : public Humanoid {
public:
    Avatar(const Model& model, bool slim);

    void animate(const MobState& state, Pose& pose) const override;
    void scale(const MobState& state, Placement& place) const override;
    void setup_rotations(const MobState& state, Placement& place, float body_rot,
                         float scale) const override;

    bool slim() const { return slim_; }

private:
    int32_t jacket_ = -1, left_sleeve_ = -1, right_sleeve_ = -1;
    int32_t left_pants_ = -1, right_pants_ = -1;
    bool slim_ = false;
};

// `SpearAnimations.thirdPersonHandUse` and `.thirdPersonAttackHand`: what a
// spear does to the arm that holds it and to the arm that swings it. The
// sway the item's `minecraft:kinetic_weapon` component drives is not ported:
// nothing here reads item components yet.
void spear_hand_use(PartPose& arm, const PartPose& head, bool right, const MobState& state);
void spear_attack_hand(PartPose& right_arm, PartPose& left_arm, const PartPose& body,
                       const MobState& state);

} // namespace mcpp::entity
