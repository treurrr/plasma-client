#pragma once
// What something in the world holds: where the item sits once the model has
// said where its hand is. `ItemInHandLayer`, `PlayerItemInHandLayer` and
// `CrossedArmsItemLayer` translated, as matrix arithmetic and nothing else -
// the geometry and the texture belong to the hand renderer, the part chain to
// the model, and every function here is pure.
#include "mcpp/entity/model.hpp"
#include "mcpp/gpu/hand.hpp"

#include <cstdint>

namespace mcpp::entity {

// `ArmedModel.translateToHand`: whose version of it applies.
// `MobModel::hand_chain` gives the parts an item hangs from; a few models put a
// nudge, a turn or a shrink around that chain, and this names which.
enum class HandModel : uint8_t {
    // `HumanoidModel`, `IllagerModel`, `ArmorStandModel`, `VillagerModel`.
    Plain,
    // `SkeletonModel`.
    Skeleton,
    // `PlayerModel` with a slim skin; a wide one is `Plain`.
    SlimAvatar,
    // `AllayModel`.
    Allay,
    // `VexModel`.
    Vex,
    // `CopperGolemModel`, standing still.
    CopperGolemIdle,
    // `CopperGolemModel`, in an interaction.
    CopperGolemBusy,
};

// `SkeletonModel.translateToHand`, `PlayerModel.translateToHand`: the arm part
// is moved sideways for as long as the chain is posed and put back after, so
// this is added to that part's own `x` before `pose_matrices` runs. Pixels, the
// unit a part's offsets are in.
float hand_arm_offset(HandModel model, bool left_hand);

// `AllayModel.translateToHand`, `VexModel.translateToHand`,
// `CopperGolemModel.translateToHand`: what the model puts on the pose stack
// after the chain. `arm_x_rot` is the right arm's own pitch, which only the
// allay turns by. `arm` is the matrix `pose_matrices` wrote for the last part
// of `MobModel::hand_chain`.
Matrix hand_after_chain(const Matrix& arm, HandModel model, bool left_hand, float arm_x_rot);

// `ItemTransform.apply`: where one display context puts a model, and the half
// block every model shifts by so its own middle lands where the context asked.
void apply_display(Placement& place, const gpu::HandRenderer::Display& display, bool left_hand);

// `ItemInHandLayer.submit`, `ItemInHandLayer.submitArmWithItem`: where the item
// in one hand of one entity sits, given the matrix that already takes that
// arm's part space to camera-relative space. Returns the placement the item's
// own space - a block from corner to corner, with its display transform already
// applied - is drawn in.
Matrix held_item_placement(const Matrix& arm, bool left_hand,
                           const gpu::HandRenderer::Display& display);

// `CrossedArmsItemLayer.applyTranslation`: the villager's and the allay's own
// item, held in both arms, given the matrix of the part `translateToArms` ends
// on. The context is `GROUND`, not either hand's, so it never mirrors.
Matrix crossed_arms_item_placement(const Matrix& arms,
                                   const gpu::HandRenderer::Display& display);

// `PlayerItemInHandLayer.renderItemHeldToEye`, `CustomHeadLayer.translateToHead`:
// an item held up to the eye - a spyglass, a horn - hangs off the head rather
// than off the hand. `head` is the head part's matrix, posed with the pitch
// below.
Matrix head_item_placement(const Matrix& head, bool left_hand,
                           const gpu::HandRenderer::Display& display);
// `PlayerItemInHandLayer`: the head keeps its own pitch, but the matrix that
// item is taken from clamps it.
float head_item_x_rot(float x_rot);

} // namespace mcpp::entity
