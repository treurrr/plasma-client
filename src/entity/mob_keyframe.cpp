#include "mob_factory.hpp"

#include "mcpp/entity/animation.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace mcpp::entity {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegrees = kPi / 180.0f;

// `ArmadilloModel`: the shell it rolls into, the body it unrolls back out of,
// and the four keyframe animations that carry it between them.
class Armadillo : public MobModel {
public:
    explicit Armadillo(const Model& model)
        : MobModel(model), body_(part("body")), right_hind_leg_(part("right_hind_leg")),
          left_hind_leg_(part("left_hind_leg")), head_(part("body/head")),
          tail_(part("body/tail")), cube_(part("cube")),
          walk_("ArmadilloAnimation.ARMADILLO_WALK", model),
          roll_out_("ArmadilloAnimation.ARMADILLO_ROLL_OUT", model),
          roll_up_("ArmadilloAnimation.ARMADILLO_ROLL_UP", model),
          peek_("ArmadilloAnimation.ARMADILLO_PEEK", model) {}

    // `ArmadilloModel.setupAnim`.
    void animate(const MobState& s, Pose& pose) const override {
        reset(pose);
        if (s.hiding_in_shell) {
            pose[body_].skip_draw = true;
            pose[left_hind_leg_].visible = false;
            pose[right_hind_leg_].visible = false;
            pose[tail_].visible = false;
            pose[cube_].visible = true;
        } else {
            pose[body_].skip_draw = false;
            pose[left_hind_leg_].visible = true;
            pose[right_hind_leg_].visible = true;
            pose[tail_].visible = true;
            pose[cube_].visible = false;
            pose[head_].x_rot = std::clamp(s.x_rot, -22.5f, 25.0f) * kDegrees;
            pose[head_].y_rot = std::clamp(s.y_rot, -32.5f, 32.5f) * kDegrees;
        }

        walk_.apply_walk(s.walk_position, s.walk_speed, 16.5f, 2.5f, pose);
        if (s.roll_out.running()) roll_out_.apply(s.roll_out.elapsed(s.tick, s.partial), pose);
        if (s.roll_up.running()) roll_up_.apply(s.roll_up.elapsed(s.tick, s.partial), pose);
        if (s.peek.running()) peek_.apply(s.peek.elapsed(s.tick, s.partial), pose);
    }

private:
    int32_t body_, right_hind_leg_, left_hind_leg_, head_, tail_, cube_;
    KeyframeAnimation walk_, roll_out_, roll_up_, peek_;
};

// `BreezeModel`: the same six animations whichever of the three layers - the
// body, its eyes, the wind around it - this model was built for, since every
// one of them keeps the whole part tree.
class Breeze : public MobModel {
public:
    explicit Breeze(const Model& model)
        : MobModel(model), idle_("BreezeAnimation.IDLE", model),
          shoot_("BreezeAnimation.SHOOT", model), slide_("BreezeAnimation.SLIDE", model),
          slide_back_("BreezeAnimation.SLIDE_BACK", model),
          inhale_("BreezeAnimation.INHALE", model), jump_("BreezeAnimation.JUMP", model) {}

    // `BreezeModel.setupAnim`.
    void animate(const MobState& s, Pose& pose) const override {
        reset(pose);
        if (s.breeze_idle.running()) idle_.apply(s.breeze_idle.elapsed(s.tick, s.partial), pose);
        if (s.breeze_shoot.running()) shoot_.apply(s.breeze_shoot.elapsed(s.tick, s.partial), pose);
        if (s.breeze_slide.running()) slide_.apply(s.breeze_slide.elapsed(s.tick, s.partial), pose);
        if (s.breeze_slide_back.running()) {
            slide_back_.apply(s.breeze_slide_back.elapsed(s.tick, s.partial), pose);
        }
        if (s.breeze_inhale.running()) {
            inhale_.apply(s.breeze_inhale.elapsed(s.tick, s.partial), pose);
        }
        if (s.breeze_long_jump.running()) {
            jump_.apply(s.breeze_long_jump.elapsed(s.tick, s.partial), pose);
        }
    }

private:
    KeyframeAnimation idle_, shoot_, slide_, slide_back_, inhale_, jump_;
};

// `CopperGolemModel`: the head follows the look, the walk cycle depends on
// whether its hands are full, and five more animations play over the top.
class CopperGolem : public MobModel {
public:
    explicit CopperGolem(const Model& model)
        : MobModel(model), body_(part("body")), head_(part("body/head")),
          right_arm_(part("body/right_arm")), left_arm_(part("body/left_arm")),
          root_(part(kRootPath)),
          walk_("CopperGolemAnimation.COPPER_GOLEM_WALK", model),
          walk_with_item_("CopperGolemAnimation.COPPER_GOLEM_WALK_ITEM", model),
          idle_("CopperGolemAnimation.COPPER_GOLEM_IDLE", model),
          get_item_("CopperGolemAnimation.COPPER_GOLEM_CHEST_INTERACTION_NOITEM_GET", model),
          get_no_item_("CopperGolemAnimation.COPPER_GOLEM_CHEST_INTERACTION_NOITEM_NOGET", model),
          drop_item_("CopperGolemAnimation.COPPER_GOLEM_CHEST_INTERACTION_ITEM_DROP", model),
          drop_no_item_("CopperGolemAnimation.COPPER_GOLEM_CHEST_INTERACTION_ITEM_NODROP", model),
          right_chain_{root_, body_, right_arm_}, left_chain_{root_, body_, left_arm_} {}

    // `CopperGolemModel.setupAnim`.
    void animate(const MobState& s, Pose& pose) const override {
        reset(pose);
        pose[head_].x_rot = s.x_rot * kDegrees;
        pose[head_].y_rot = s.y_rot * kDegrees;
        if (s.right_hand_item_empty && s.left_hand_item_empty) {
            walk_.apply_walk(s.walk_position, s.walk_speed, 2.0f, 2.5f, pose);
        } else {
            walk_with_item_.apply_walk(s.walk_position, s.walk_speed, 2.0f, 2.5f, pose);
            pose_held_item_arms_if_still(pose);
        }

        if (s.golem_idle.running()) idle_.apply(s.golem_idle.elapsed(s.tick, s.partial), pose);
        if (s.golem_get_item.running()) {
            get_item_.apply(s.golem_get_item.elapsed(s.tick, s.partial), pose);
        }
        if (s.golem_get_no_item.running()) {
            get_no_item_.apply(s.golem_get_no_item.elapsed(s.tick, s.partial), pose);
        }
        if (s.golem_drop_item.running()) {
            drop_item_.apply(s.golem_drop_item.elapsed(s.tick, s.partial), pose);
        }
        if (s.golem_drop_no_item.running()) {
            drop_no_item_.apply(s.golem_drop_no_item.elapsed(s.tick, s.partial), pose);
        }
    }

    // `CopperGolemModel.translateToHand`: the chain only; the turn and the
    // shrink the game puts on top of it depend on `CopperGolemState`.
    std::span<const int32_t> hand_chain(bool left) const override {
        return left ? std::span<const int32_t>(left_chain_) : std::span<const int32_t>(right_chain_);
    }

private:
    // `CopperGolemModel.poseHeldItemArmsIfStill`: arms that carry something
    // never drop below the pose that holds it.
    void pose_held_item_arms_if_still(Pose& pose) const {
        PartPose& right = pose[right_arm_];
        PartPose& left = pose[left_arm_];
        right.x_rot = std::min(right.x_rot, -0.87266463f);
        left.x_rot = std::min(left.x_rot, -0.87266463f);
        right.y_rot = std::min(right.y_rot, -0.1134464f);
        left.y_rot = std::max(left.y_rot, 0.1134464f);
        right.z_rot = std::min(right.z_rot, -0.064577185f);
        left.z_rot = std::max(left.z_rot, 0.064577185f);
    }

    int32_t body_, head_, right_arm_, left_arm_, root_;
    KeyframeAnimation walk_, walk_with_item_, idle_, get_item_, get_no_item_, drop_item_,
        drop_no_item_;
    std::array<int32_t, 3> right_chain_, left_chain_;
};

// `CopperGolemStatueModel`: the three posed copies of the golem a statue
// block is built from, which only ever stand the one way up.
class CopperGolemStatue : public MobModel {
public:
    explicit CopperGolemStatue(const Model& model) : MobModel(model), root_(part(kRootPath)) {}

    // `CopperGolemStatueModel.setupAnim`.
    void animate(const MobState& s, Pose& pose) const override {
        reset(pose);
        pose[root_].y = 0.0f;
        pose[root_].y_rot = opposite_y_rot(s.facing) * kDegrees;
        pose[root_].z_rot = kPi;
    }

private:
    // `Direction.getOpposite`, `.toYRot`: north, south, west, east.
    static float opposite_y_rot(int8_t facing) {
        switch (facing) {
        case 2: return 0.0f;
        case 3: return 180.0f;
        case 4: return 270.0f;
        case 5: return 90.0f;
        default: return 0.0f;
        }
    }

    int32_t root_;
};

constexpr std::array<MobOverlay, 2> kBreezeOverlays = {{
    {"breeze#wind", "breeze/breeze_wind", Overlay::Plain,
     gpu::EntityRenderer::Type::Translucent},
    {"breeze#eyes", "breeze/breeze_eyes", Overlay::Eyes,
     gpu::EntityRenderer::Type::Translucent},
}};

constexpr std::array<MobOverlay, 1> kCopperGolemOverlays = {{
    {"", "copper_golem/copper_golem_eyes", Overlay::Eyes,
     gpu::EntityRenderer::Type::Translucent},
}};

// `ArmadilloRenderer`, `BreezeRenderer`, `CopperGolemRenderer`.
constexpr std::array<MobType, 3> kTypes = {{
    {"minecraft:armadillo", "armadillo", "armadillo", "armadillo_baby", 0.4f,
     gpu::EntityRenderer::Type::CutoutNoCull, false, false, {}},
    {"minecraft:breeze", "breeze", "breeze/breeze", "", 0.5f,
     gpu::EntityRenderer::Type::Translucent, false, false, kBreezeOverlays},
    {"minecraft:copper_golem", "copper_golem", "copper_golem/copper_golem", "", 0.5f,
     gpu::EntityRenderer::Type::CutoutNoCull, false, true, kCopperGolemOverlays},
}};

} // namespace

std::unique_ptr<MobModel> make_keyframe_model(std::string_view layer, const Model& model) {
    if (layer == "armadillo" || layer == "armadillo_baby") {
        return std::make_unique<Armadillo>(model);
    }
    if (layer == "breeze" || layer == "breeze#eyes" || layer == "breeze#wind") {
        return std::make_unique<Breeze>(model);
    }
    if (layer == "copper_golem" || layer == "copper_golem#eyes") {
        return std::make_unique<CopperGolem>(model);
    }
    if (layer == "copper_golem_running" || layer == "copper_golem_sitting" ||
        layer == "copper_golem_star") {
        return std::make_unique<CopperGolemStatue>(model);
    }
    return nullptr;
}

std::span<const MobType> keyframe_mob_types() { return kTypes; }

} // namespace mcpp::entity
