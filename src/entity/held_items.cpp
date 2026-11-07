#include "mcpp/entity/held_items.hpp"

#include <algorithm>
#include <cmath>

namespace mcpp::entity {
namespace {

constexpr float kPi = 3.14159265358979323846f;

// A model's parts arrive turned and scaled, so the matrix they arrive in is
// affine and no more, which is the route `PoseStack.Pose.pose` takes through every
// operation below.
Placement from_matrix(const Matrix& m) { return Placement{m, Placement::kAffine}; }

} // namespace

// `SkeletonModel.translateToHand`, `PlayerModel.translateToHand`: the arm the
// bow or the sword hangs from stands a pixel - half of one on a slim player -
// further out than the arm that is drawn.
float hand_arm_offset(HandModel model, bool left_hand) {
    const float side = left_hand ? -1.0f : 1.0f;
    switch (model) {
    case HandModel::Skeleton:
        return side;
    case HandModel::SlimAvatar:
        return 0.5f * side;
    default:
        return 0.0f;
    }
}

// `AllayModel.translateToHand`, `VexModel.translateToHand`,
// `CopperGolemModel.translateToHand`.
Matrix hand_after_chain(const Matrix& arm, HandModel model, bool left_hand, float arm_x_rot) {
    Placement place = from_matrix(arm);
    switch (model) {
    case HandModel::Allay:
        place.translate(0.0f, 0.0625f, 0.1875f);
        place.rotate(quaternion_x(arm_x_rot));
        place.scale(0.7f, 0.7f, 0.7f);
        place.translate(0.0625f, 0.0f, 0.0f);
        break;
    case HandModel::Vex:
        place.scale(0.55f, 0.55f, 0.55f);
        // `VexModel.offsetStackPosition`.
        place.translate(left_hand ? -0.046875f : 0.046875f, -0.15625f, 0.078125f);
        break;
    case HandModel::CopperGolemIdle:
        place.rotate(quaternion_y(to_radians(left_hand ? 90.0f : -90.0f)));
        place.translate(0.0f, 0.0f, 0.125f);
        break;
    case HandModel::CopperGolemBusy:
        place.scale(0.55f, 0.55f, 0.55f);
        place.translate(-0.125f, 0.3125f, -0.1875f);
        break;
    default:
        return arm;
    }
    return place.matrix;
}

// `ItemTransform.apply`: the context a model names, mirrored for the left hand,
// and the half block that puts the model's own middle where it asked to be.
void apply_display(Placement& place, const gpu::HandRenderer::Display& display, bool left_hand) {
    if (!display.present) {
        place.translate(-0.5f, -0.5f, -0.5f);
        return;
    }
    const float x = left_hand ? -display.translation[0] : display.translation[0];
    const float y_rot = left_hand ? -display.rotation[1] : display.rotation[1];
    const float z_rot = left_hand ? -display.rotation[2] : display.rotation[2];
    place.translate(x, display.translation[1], display.translation[2]);
    place.rotate(quaternion_xyz(to_radians(display.rotation[0]), to_radians(y_rot),
                                to_radians(z_rot)));
    place.scale(display.scale[0], display.scale[1], display.scale[2]);
    place.translate(-0.5f, -0.5f, -0.5f);
}

// `ItemInHandLayer.submitArmWithItem`: the hand turned to face the way an item
// is held, a pixel out to the side, and the model's own third person transform.
// `SpearAnimations.thirdPersonAttackItem` and `ArmPose.animateUseItem` both
// read the `minecraft:kinetic_weapon` component, which nothing here resolves
// yet, so neither is in this.
Matrix held_item_placement(const Matrix& arm, bool left_hand,
                           const gpu::HandRenderer::Display& display) {
    Placement place = from_matrix(arm);
    place.rotate(quaternion_x(to_radians(-90.0f)));
    place.rotate(quaternion_y(to_radians(180.0f)));
    place.translate((left_hand ? -1.0f : 1.0f) / 16.0f, 0.125f, -0.625f);
    apply_display(place, display, left_hand);
    return place.matrix;
}

// `CrossedArmsItemLayer.applyTranslation`.
Matrix crossed_arms_item_placement(const Matrix& arms,
                                   const gpu::HandRenderer::Display& display) {
    Placement place = from_matrix(arms);
    place.rotate(quaternion_x(0.75f));
    place.scale(1.07f, 1.07f, 1.07f);
    place.translate(0.0f, 0.13f, -0.34f);
    place.rotate(quaternion_x(kPi));
    apply_display(place, display, false);
    return place.matrix;
}

// `PlayerItemInHandLayer.renderItemHeldToEye`, `CustomHeadLayer.translateToHead`
// with `CustomHeadLayer.translateToHead`: the item sits where a worn head would, out to the
// side of the eye it is held to.
Matrix head_item_placement(const Matrix& head, bool left_hand,
                           const gpu::HandRenderer::Display& display) {
    Placement place = from_matrix(head);
    place.translate(0.0f, -0.25f, 0.0f);
    place.rotate(quaternion_y(to_radians(180.0f)));
    place.scale(0.625f, -0.625f, -0.625f);
    place.translate((left_hand ? -2.5f : 2.5f) / 16.0f, -0.0625f, 0.0f);
    apply_display(place, display, left_hand);
    return place.matrix;
}

float head_item_x_rot(float x_rot) {
    return std::clamp(x_rot, static_cast<float>(-M_PI / 6.0), static_cast<float>(M_PI / 2.0));
}

} // namespace mcpp::entity
