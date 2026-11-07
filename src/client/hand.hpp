#pragma once
#include "mcpp/entity/model.hpp"

#include <cstdint>

// The arm and the item in front of the camera: where they sit, how they swing
// and how they rise and fall as the hand changes what it holds.
//
// `ItemInHandRenderer` translated. The whole of it is matrix arithmetic on a
// pose stack, so it stays where it can be run without a window and compared
// with the game's own numbers (tools/oracle/HandOracle.java).
namespace mcpp::client {

using Matrix = mcpp::entity::Matrix;

enum class Arm : uint8_t { Left, Right };
enum class HandSide : uint8_t { Main, Off };

// `ItemUseAnimation`: what holding right-click does to the arm.
enum class UseAnimation : uint8_t {
    None, Eat, Drink, Block, Bow, Trident, Crossbow, Spyglass, TootHorn, Brush, Bundle, Spear
};
// `ItemUseAnimation.hasCustomArmTransform`.
bool has_custom_arm_transform(UseAnimation animation);

// `ItemStack.getSwingAnimation`: whack is every ordinary item, stab is a spear.
enum class SwingKind : uint8_t { None, Whack, Stab };

// `Axis.XP.rotationDegrees` and its five siblings, built the way the game
// builds them: a quaternion from the half angle, then JOML's matrix from it.
Matrix rotation_degrees_x(float degrees);
Matrix rotation_degrees_y(float degrees);
Matrix rotation_degrees_z(float degrees);

// `PoseStack.Pose`: a matrix built up by multiplying on the right. The flag
// is JOML's own: its first operation on an untouched matrix writes the result
// straight in rather than multiplying, which keeps a signed zero that a
// multiply would lose, and every later one fuses its multiply and add.
struct Pose {
    Matrix matrix = mcpp::entity::identity();
    bool identity = true;
    // Still nothing but a move: a turn then replaces the upper three by three
    // rather than multiplying through it.
    bool moved_only = true;
};
void translate(Pose& pose, float x, float y, float z);
void rotate(Pose& pose, const Matrix& rotation);
void scale(Pose& pose, float x, float y, float z);

// `Quaternionf.rotationXYZ`: the turn a display transform names, as a matrix.
Matrix rotation_xyz_degrees(float x, float y, float z);

// `ItemTransform`: where one display context puts a model. The translation is
// in blocks, as the model file's sixteenths clamped to five, and `present` is
// false for the context a model leaves out, which still shifts by half a block.
struct ItemDisplay {
    float rotation[3]{};
    float translation[3]{};
    float scale[3]{1.0f, 1.0f, 1.0f};
    bool present = false;
};
// `ItemTransform.apply`.
void apply_display_transform(Pose& pose, const ItemDisplay& display, bool left_handed);

// `ItemInHandRenderer.applyItemArmTransform`: the hand's resting place, lowered
// by how far through swapping what it holds it is.
void apply_item_arm_transform(Pose& pose, Arm arm, float equipped);
// `ItemInHandRenderer.applyItemArmAttackTransform`: the turn a swing adds.
void apply_item_arm_attack_transform(Pose& pose, Arm arm, float swing);
// `ItemInHandRenderer.swingArm`: the move and the turn of one swing together.
void swing_arm(Pose& pose, float swing, int sign, Arm arm);
// `ItemInHandRenderer.renderPlayerArm`: an empty hand, which is the arm itself.
void player_arm_transform(Pose& pose, float equipped, float swing, Arm arm);
// `ItemInHandRenderer.applyEatTransform`: the jiggle of eating and drinking.
void apply_eat_transform(Pose& pose, float partial_tick, Arm arm, int32_t use_remaining,
                         int32_t use_duration);
// `ItemInHandRenderer.applyBrushTransform`: brushing suspicious sand.
void apply_brush_transform(Pose& pose, float partial_tick, Arm arm, int32_t use_remaining);

// `GameRenderer.bobHurt`: the camera reels away from what hit you.
void bob_hurt(Pose& pose, float partial_tick, int32_t hurt_time, int32_t hurt_duration,
              float hurt_dir, double tilt_strength, bool dying, int32_t death_time);
// `GameRenderer.bobView`: the walk bob, which the hand rides along with.
void bob_view(Pose& pose, float walk_dist, float bob);

// What the player is and does, for one frame of the hand.
struct HandPlayer {
    float partial_tick = 1.0f;
    int32_t main_item = 0;
    int32_t off_item = 0;
    Arm main_arm = Arm::Right;
    bool invisible = false;
    bool scoping = false;
    bool auto_spin_attack = false;
    bool hands_busy = false;
    // `LocalPlayer.getItemSwapScale` and `ItemModelResolver.swapAnimationScale`:
    // how far the hand climbs back, and how much of that the model asks for.
    float item_swap_scale = 1.0f;
    float main_swap_animation_scale = 1.0f;
    float off_swap_animation_scale = 1.0f;
    // `ItemModelResolver.shouldPlaySwapAnimation`: false swaps with no dip.
    bool main_swaps = true;
    bool off_swaps = true;

    float attack_anim = 0.0f;
    HandSide swinging_hand = HandSide::Main;
    float x_rot = 0.0f;

    bool using_item = false;
    HandSide used_hand = HandSide::Main;
    int32_t use_remaining = 0;
    int32_t use_duration = 0;
    UseAnimation use_animation = UseAnimation::None;
    SwingKind main_swing = SwingKind::Whack;
    SwingKind off_swing = SwingKind::Whack;

    // The two bow-like items decide which hands are drawn at all.
    bool main_is_bow = false, off_is_bow = false;
    bool main_is_crossbow = false, off_is_crossbow = false;
    bool main_crossbow_charged = false, off_crossbow_charged = false;
    int32_t crossbow_charge_duration = 25;

    // The head's own drift behind where the camera points.
    float view_x_rot = 0.0f, view_y_rot = 0.0f;
    float x_bob_o = 0.0f, x_bob = 0.0f;
    float y_bob_o = 0.0f, y_bob = 0.0f;

    // The walk bob and the hurt reel, which sit under everything else.
    bool view_bobbing = true;
    float walk_dist_o = 0.0f, walk_dist = 0.0f;
    float bob_o = 0.0f, bob = 0.0f;
    int32_t hurt_time = 0, hurt_duration = 10, death_time = 0;
    float hurt_dir = 0.0f;
    double damage_tilt = 1.0;
    bool dying = false;
};

// One thing to draw in front of the camera.
struct HandDraw {
    Matrix pose = mcpp::entity::identity();
    Arm arm = Arm::Right;
    // The bare arm, with the player's own skin; otherwise `item` is held.
    bool is_arm = false;
    int32_t item = 0;
    // The crossbow's and the bow's own pull, for the model that shows it.
    float pull = 0.0f;
};

// `ItemInHandRenderer.HandRenderSelection`.
struct HandSelection {
    bool main = true;
    bool off = true;
};
// `ItemInHandRenderer.evaluateWhichHandsToRender`.
HandSelection which_hands(const HandPlayer& player);

// The two heights `ItemInHandRenderer.tick` keeps, so a hand that changes what
// it holds dips out of view and back.
class HandBob {
public:
    // `ItemInHandRenderer.tick`.
    void tick(const HandPlayer& player);
    // `ItemInHandRenderer.itemUsed`.
    void item_used(HandSide hand);
    int32_t shown_main() const { return main_item_; }
    int32_t shown_off() const { return off_item_; }
    // `Mth.lerp(partialTick, oHeight, height)` for either hand.
    float height(HandSide hand, float partial_tick) const;

private:
    int32_t main_item_ = 0;
    int32_t off_item_ = 0;
    float main_height_ = 0.0f, main_height_o_ = 0.0f;
    float off_height_ = 0.0f, off_height_o_ = 0.0f;
};

// `GameRenderer.renderItemInHand`, `ItemInHandRenderer.renderHandsWithItems`,
// `ItemInHandRenderer.renderArmWithItem`: everything in front of the camera,
// in camera space, with the hurt reel and the walk bob already under it.
void hands_with_items(const HandPlayer& player, const HandBob& bob, HandDraw* out,
                      int& count);

// `GameRenderer.getFov` for the hand, which never follows the fov option.
constexpr float kHandFovDegrees = 70.0f;
constexpr float kHandNear = 0.05f;
constexpr float kHandFar = 100.0f;

} // namespace mcpp::client
