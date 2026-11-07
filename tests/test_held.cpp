// Where a held item sits in the world. The expected matrices are worked out by
// hand from the operations vanilla's layers name, not from the port: a turn of
// -90 about x and one of 180 about y take a part's axes to (-x, -z, -y), so a
// move of (dx, dy, dz) in the hand lands at (-dx, -dz, -dy).
#include "mcpp/entity/held_items.hpp"

#include <cmath>
#include <iostream>
#include <string_view>

namespace {

int failures = 0;

void check(bool condition, std::string_view what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << '\n';
        ++failures;
    }
}

void close(const mcpp::entity::Matrix& got, const mcpp::entity::Matrix& want,
           std::string_view what) {
    for (size_t i = 0; i < got.size(); ++i) {
        if (std::fabs(got[i] - want[i]) > 1e-6f) {
            std::cerr << "FAIL: " << what << " at " << i << ": got " << got[i] << " want "
                      << want[i] << '\n';
            ++failures;
            return;
        }
    }
}

// `ItemInHandLayer.submitArmWithItem` with no display transform: the hand's own
// turn, a pixel out to the side, and the half block `ItemTransform.NO_TRANSFORM`
// shifts by. (1/16, 0.125, -0.625) and (-0.5, -0.5, -0.5) both land through the
// turn above, so x is -1/16 + 0.5 and the other two swap.
void hand_without_display() {
    using namespace mcpp::entity;
    const Matrix turned = {-1.0f, 0.0f,  0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f,
                           0.0f,  -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  1.0f};
    mcpp::gpu::HandRenderer::Display none{};
    Matrix right = turned;
    right[12] = 0.4375f;
    right[13] = 1.125f;
    right[14] = 0.375f;
    close(held_item_placement(identity(), false, none), right, "right hand, no display");
    Matrix left = right;
    left[12] = 0.5625f;
    close(held_item_placement(identity(), true, none), left, "left hand, no display");
}

// `ItemTransform.apply` for item/handheld's thirdperson_righthand - no pitch, a
// quarter turn the other way, 55 degrees of roll, up a quarter of a block and
// forward half a pixel, at 0.85 - and the mirror of it in the left hand.
void hand_with_display() {
    using namespace mcpp::entity;
    mcpp::gpu::HandRenderer::Display handheld{};
    handheld.present = true;
    handheld.rotation[1] = -90.0f;
    handheld.rotation[2] = 55.0f;
    handheld.translation[1] = 0.25f;
    handheld.translation[2] = 0.03125f;
    handheld.scale[0] = handheld.scale[1] = handheld.scale[2] = 0.85f;
    const Matrix right = {0.0f,  -0.4875400f, -0.6962792f, 0.0f,
                          0.0f,  0.6962792f,  -0.4875400f, 0.0f,
                          0.85f, 0.0f,        0.0f,        0.0f,
                          -0.4875f, 0.4893804f, 0.2169096f, 1.0f};
    close(held_item_placement(identity(), false, handheld), right, "right hand, handheld");
    const Matrix left = {0.0f,   0.4875400f, 0.6962792f,  0.0f,
                         0.0f,   0.6962792f, -0.4875400f, 0.0f,
                         -0.85f, 0.0f,       0.0f,        0.0f,
                         0.4875f, 0.0018404f, -0.4793696f, 1.0f};
    close(held_item_placement(identity(), true, handheld), left, "left hand, handheld");
}

// `AllayModel.translateToHand`, `VexModel.translateToHand`,
// `SkeletonModel.translateToHand`, `PlayerModel.translateToHand`.
void model_tails() {
    using namespace mcpp::entity;
    const Matrix allay = {0.7f,     0.0f,    0.0f,    0.0f, 0.0f, 0.7f, 0.0f, 0.0f,
                          0.0f,     0.0f,    0.7f,    0.0f, 0.04375f, 0.0625f, 0.1875f, 1.0f};
    close(hand_after_chain(identity(), HandModel::Allay, false, 0.0f), allay, "allay tail");
    const Matrix vex = {0.55f,        0.0f,         0.0f,         0.0f,
                        0.0f,         0.55f,        0.0f,         0.0f,
                        0.0f,         0.0f,         0.55f,        0.0f,
                        0.025781250f, -0.085937500f, 0.042968750f, 1.0f};
    close(hand_after_chain(identity(), HandModel::Vex, false, 0.0f), vex, "vex tail");
    close(hand_after_chain(identity(), HandModel::Plain, false, 0.3f), identity(),
          "a humanoid adds nothing after the chain");
    check(hand_arm_offset(HandModel::Skeleton, false) == 1.0f, "skeleton right arm");
    check(hand_arm_offset(HandModel::Skeleton, true) == -1.0f, "skeleton left arm");
    check(hand_arm_offset(HandModel::SlimAvatar, false) == 0.5f, "slim right arm");
    check(hand_arm_offset(HandModel::SlimAvatar, true) == -0.5f, "slim left arm");
    check(hand_arm_offset(HandModel::Plain, false) == 0.0f, "a wide arm is not moved");
}

// `PlayerItemInHandLayer.renderItemHeldToEye` and the clamp on the pitch the
// head is posed with while that matrix is taken.
void head_item() {
    using namespace mcpp::entity;
    mcpp::gpu::HandRenderer::Display none{};
    const Matrix want = {-0.625f,     0.0f,       0.0f,   0.0f, 0.0f, -0.625f, 0.0f, 0.0f,
                         0.0f,        0.0f,       0.625f, 0.0f,
                         0.21484375f, 0.1015625f, -0.3125f, 1.0f};
    close(head_item_placement(identity(), false, none), want, "head item");
    check(head_item_x_rot(-3.0f) == static_cast<float>(-M_PI / 6.0), "head pitch, low");
    check(head_item_x_rot(0.1f) == 0.1f, "head pitch, between");
    check(head_item_x_rot(3.0f) == static_cast<float>(M_PI / 2.0), "head pitch, high");
}

// `CrossedArmsItemLayer.applyTranslation`: 0.75 radians about x, a little
// bigger, forward and up, and then over onto its back.
void crossed_arms() {
    using namespace mcpp::entity;
    mcpp::gpu::HandRenderer::Display none{};
    const Matrix want = {1.07f,   0.0f,        0.0f,        0.0f,
                         0.0f,    -0.7829071f, -0.7293535f, 0.0f,
                         0.0f,    0.7293535f,  -0.7829071f, 0.0f,
                         -0.535f, 0.3765349f,  0.5847578f,  1.0f};
    close(crossed_arms_item_placement(identity(), none), want, "crossed arms");
}

} // namespace

int main() {
    hand_without_display();
    hand_with_display();
    model_tails();
    head_item();
    crossed_arms();
    if (failures == 0) std::cout << "held items ok\n";
    return failures == 0 ? 0 : 1;
}
