#include "client/hand.hpp"

#include "mcpp/jmath.hpp"

#include <algorithm>
#include <cmath>

namespace mcpp::client {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegToRad = static_cast<float>(3.14159265358979323846 / 180.0);

float lerp(float t, float from, float to) { return from + t * (to - from); }

// `Math.cosFromSin`: the cosine beside a sine, by Pythagoras and the quadrant.
float cos_from_sin(float sine, float angle) {
    const float cosine = static_cast<float>(std::sqrt(1.0f - sine * sine));
    const float shifted = angle + static_cast<float>(3.14159265358979323846 * 0.5);
    const float two_pi = static_cast<float>(3.14159265358979323846 * 2.0);
    float wrapped = shifted - static_cast<float>(static_cast<int>(shifted / two_pi)) * two_pi;
    if (wrapped < 0.0f) wrapped = two_pi + wrapped;
    if (wrapped >= static_cast<float>(3.14159265358979323846)) return -cosine;
    return cosine;
}

// `Matrix4f.rotate(Quaternionfc)`: the nine products a unit quaternion makes.
Matrix from_quaternion(float x, float y, float z, float w) {
    const float w2 = w * w;
    const float x2 = x * x;
    const float y2 = y * y;
    const float z2 = z * z;
    const float zw = z * w, dzw = zw + zw;
    const float xy = x * y, dxy = xy + xy;
    const float xz = x * z, dxz = xz + xz;
    const float yw = y * w, dyw = yw + yw;
    const float yz = y * z, dyz = yz + yz;
    const float xw = x * w, dxw = xw + xw;
    Matrix m = entity::identity();
    m[0] = w2 + x2 - z2 - y2;
    m[1] = dxy + dzw;
    m[2] = dxz - dyw;
    m[4] = -dzw + dxy;
    m[5] = y2 - z2 + w2 - x2;
    m[6] = dyz + dxw;
    m[8] = dyw + dxz;
    m[9] = dyz - dxw;
    m[10] = z2 - y2 - x2 + w2;
    return m;
}

} // namespace

bool has_custom_arm_transform(UseAnimation animation) {
    return animation == UseAnimation::Eat || animation == UseAnimation::Drink
           || animation == UseAnimation::Spear;
}

// `Quaternionf.rotationX`: a turn about x as a quaternion, then as a matrix.
Matrix rotation_degrees_x(float degrees) {
    const float half = degrees * kDegToRad * 0.5f;
    const float sine = static_cast<float>(std::sin(static_cast<double>(half)));
    return from_quaternion(sine, 0.0f, 0.0f, cos_from_sin(sine, half));
}

// `Quaternionf.rotationY`.
Matrix rotation_degrees_y(float degrees) {
    const float half = degrees * kDegToRad * 0.5f;
    const float sine = static_cast<float>(std::sin(static_cast<double>(half)));
    return from_quaternion(0.0f, sine, 0.0f, cos_from_sin(sine, half));
}

// `Quaternionf.rotationZ`.
Matrix rotation_degrees_z(float degrees) {
    const float half = degrees * kDegToRad * 0.5f;
    const float sine = static_cast<float>(std::sin(static_cast<double>(half)));
    return from_quaternion(0.0f, 0.0f, sine, cos_from_sin(sine, half));
}

// `Quaternionf.rotationXYZ`: one quaternion for a turn about all three axes.
Matrix rotation_xyz_degrees(float x, float y, float z) {
    const float hx = x * kDegToRad * 0.5f;
    const float hy = y * kDegToRad * 0.5f;
    const float hz = z * kDegToRad * 0.5f;
    const float sx = static_cast<float>(std::sin(static_cast<double>(hx)));
    const float cx = cos_from_sin(sx, hx);
    const float sy = static_cast<float>(std::sin(static_cast<double>(hy)));
    const float cy = cos_from_sin(sy, hy);
    const float sz = static_cast<float>(std::sin(static_cast<double>(hz)));
    const float cz = cos_from_sin(sz, hz);
    const float cycz = cy * cz;
    const float sysz = sy * sz;
    const float sycz = sy * cz;
    const float cysz = cy * sz;
    return from_quaternion(sx * cycz + cx * sysz, cx * sycz - sx * cysz,
                           cx * cysz + sx * sycz, cx * cycz - sx * sysz);
}

// `Math.fma`: JOML's, which multiplies and adds separately unless the
// property that turns on the fused instruction is set, and it is off.
float mul_add(float a, float b, float c) { return a * b + c; }

// `Matrix4f.translate`, `Matrix4f.translation`: the first move on a fresh
// matrix is written in; a later one folds three products in from the right.
void translate(Pose& pose, float x, float y, float z) {
    Matrix& m = pose.matrix;
    if (pose.identity) {
        m = entity::identity();
        m[12] = x;
        m[13] = y;
        m[14] = z;
        pose.identity = false;
        return;
    }
    for (int row = 0; row < 4; ++row) {
        m[12 + row] = mul_add(m[row], x,
                              mul_add(m[4 + row], y, mul_add(m[8 + row], z, m[12 + row])));
    }
}

// `Matrix4f.rotate`, `Matrix4f.rotation`: the same, with the nine products of
// the quaternion already in `rotation`.
void rotate(Pose& pose, const Matrix& rotation) {
    Matrix& m = pose.matrix;
    if (pose.identity) {
        m = rotation;
        pose.identity = false;
        pose.moved_only = false;
        return;
    }
    if (pose.moved_only) {
        for (int i = 0; i < 12; ++i) m[i] = rotation[i];
        pose.moved_only = false;
        return;
    }
    Matrix out = m;
    for (int column = 0; column < 3; ++column) {
        const float r0 = rotation[column * 4];
        const float r1 = rotation[column * 4 + 1];
        const float r2 = rotation[column * 4 + 2];
        for (int row = 0; row < 3; ++row) {
            out[column * 4 + row] =
                mul_add(m[row], r0, mul_add(m[4 + row], r1, m[8 + row] * r2));
        }
        // `Matrix4f.rotateAffine` writes the bottom row rather than working
        // it out, so a signed zero never reaches it.
        out[column * 4 + 3] = 0.0f;
    }
    m = out;
}

// `Matrix4f.scale`, `Matrix4f.scaling`.
void scale(Pose& pose, float x, float y, float z) {
    Matrix& m = pose.matrix;
    if (pose.identity) {
        m = entity::scaling(x, y, z);
        pose.identity = false;
        pose.moved_only = false;
        return;
    }
    pose.moved_only = false;
    for (int row = 0; row < 4; ++row) {
        m[row] *= x;
        m[4 + row] *= y;
        m[8 + row] *= z;
    }
}

// `ItemTransform.apply`: a display context, and the half block every model
// shifts by so its own middle is where the context put it.
void apply_display_transform(Pose& pose, const ItemDisplay& display, bool left_handed) {
    if (!display.present) {
        translate(pose, -0.5f, -0.5f, -0.5f);
        return;
    }
    const float x = left_handed ? -display.translation[0] : display.translation[0];
    const float y_rot = left_handed ? -display.rotation[1] : display.rotation[1];
    const float z_rot = left_handed ? -display.rotation[2] : display.rotation[2];
    translate(pose, x, display.translation[1], display.translation[2]);
    rotate(pose, rotation_xyz_degrees(display.rotation[0], y_rot, z_rot));
    scale(pose, display.scale[0], display.scale[1], display.scale[2]);
    translate(pose, -0.5f, -0.5f, -0.5f);
}

// `ItemInHandRenderer.applyItemArmTransform`: where a held item rests.
void apply_item_arm_transform(Pose& pose, Arm arm, float equipped) {
    const int sign = arm == Arm::Right ? 1 : -1;
    translate(pose, static_cast<float>(sign) * 0.56f, -0.52f + equipped * -0.6f, -0.72f);
}

// `ItemInHandRenderer.applyItemArmAttackTransform`: the swing's four turns.
void apply_item_arm_attack_transform(Pose& pose, Arm arm, float swing) {
    const int sign = arm == Arm::Right ? 1 : -1;
    const float eased = jmath::sin(static_cast<double>(swing * swing * kPi));
    rotate(pose, rotation_degrees_y(static_cast<float>(sign) * (45.0f + eased * -20.0f)));
    const float rooted =
        jmath::sin(static_cast<double>(static_cast<float>(std::sqrt(swing)) * kPi));
    rotate(pose, rotation_degrees_z(static_cast<float>(sign) * rooted * -20.0f));
    rotate(pose, rotation_degrees_x(rooted * -80.0f));
    rotate(pose, rotation_degrees_y(static_cast<float>(sign) * -45.0f));
}

// `ItemInHandRenderer.swingArm`: the item pulls back and across as it swings.
void swing_arm(Pose& pose, float swing, int sign, Arm arm) {
    const float root = static_cast<float>(std::sqrt(swing));
    const float x = -0.4f * jmath::sin(static_cast<double>(root * kPi));
    const float y = 0.2f * jmath::sin(static_cast<double>(root * (kPi * 2.0f)));
    const float z = -0.2f * jmath::sin(static_cast<double>(swing * kPi));
    translate(pose, static_cast<float>(sign) * x, y, z);
    apply_item_arm_attack_transform(pose, arm, swing);
}

// `ItemInHandRenderer.renderPlayerArm`: the bare arm, up to the model itself.
void player_arm_transform(Pose& pose, float equipped, float swing, Arm arm) {
    const bool right = arm != Arm::Left;
    const float sign = right ? 1.0f : -1.0f;
    const float root = static_cast<float>(std::sqrt(swing));
    const float x = -0.3f * jmath::sin(static_cast<double>(root * kPi));
    const float y = 0.4f * jmath::sin(static_cast<double>(root * (kPi * 2.0f)));
    const float z = -0.4f * jmath::sin(static_cast<double>(swing * kPi));
    translate(pose, sign * (x + 0.64000005f), y + -0.6f + equipped * -0.6f, z + -0.71999997f);
    rotate(pose, rotation_degrees_y(sign * 45.0f));
    const float squared = jmath::sin(static_cast<double>(swing * swing * kPi));
    const float rooted = jmath::sin(static_cast<double>(root * kPi));
    rotate(pose, rotation_degrees_y(sign * rooted * 70.0f));
    rotate(pose, rotation_degrees_z(sign * squared * -20.0f));
    translate(pose, sign * -1.0f, 3.6f, 3.5f);
    rotate(pose, rotation_degrees_z(sign * 120.0f));
    rotate(pose, rotation_degrees_x(200.0f));
    rotate(pose, rotation_degrees_y(sign * -135.0f));
    translate(pose, sign * 5.6f, 0.0f, 0.0f);
}

// `ItemInHandRenderer.applyEatTransform`: the bite and the tilt away.
void apply_eat_transform(Pose& pose, float partial_tick, Arm arm, int32_t use_remaining,
                         int32_t use_duration) {
    const float left = static_cast<float>(use_remaining) - partial_tick + 1.0f;
    const float through = left / static_cast<float>(use_duration);
    if (through < 0.8f) {
        const float jiggle =
            std::abs(jmath::cos(static_cast<double>(left / 4.0f * kPi)) * 0.1f);
        translate(pose, 0.0f, jiggle, 0.0f);
    }
    const float away = 1.0f - static_cast<float>(std::pow(static_cast<double>(through), 27.0));
    const int sign = arm == Arm::Right ? 1 : -1;
    translate(pose, away * 0.6f * static_cast<float>(sign), away * -0.5f, away * 0.0f);
    rotate(pose, rotation_degrees_y(static_cast<float>(sign) * away * 90.0f));
    rotate(pose, rotation_degrees_x(away * 10.0f));
    rotate(pose, rotation_degrees_z(static_cast<float>(sign) * away * 30.0f));
}

// `ItemInHandRenderer.applyBrushTransform`: the brush sweeps once every ten ticks.
void apply_brush_transform(Pose& pose, float partial_tick, Arm arm, int32_t use_remaining) {
    const float cycle = static_cast<float>(use_remaining % 10);
    const float left = cycle - partial_tick + 1.0f;
    const float through = 1.0f - left / 10.0f;
    const float sweep =
        -15.0f + 75.0f * jmath::cos(static_cast<double>(through * 2.0f * kPi));
    if (arm != Arm::Right) {
        translate(pose, 0.1f, 0.83f, 0.35f);
        rotate(pose, rotation_degrees_x(-80.0f));
        rotate(pose, rotation_degrees_y(-90.0f));
        rotate(pose, rotation_degrees_x(sweep));
        translate(pose, -0.3f, 0.22f, 0.35f);
    } else {
        translate(pose, -0.25f, 0.22f, 0.35f);
        rotate(pose, rotation_degrees_x(-80.0f));
        rotate(pose, rotation_degrees_y(90.0f));
        rotate(pose, rotation_degrees_z(0.0f));
        rotate(pose, rotation_degrees_x(sweep));
    }
}

// `GameRenderer.bobHurt`: a turn out to the blow, a roll, and a turn back.
void bob_hurt(Pose& pose, float partial_tick, int32_t hurt_time, int32_t hurt_duration,
              float hurt_dir, double tilt_strength, bool dying, int32_t death_time) {
    float left = static_cast<float>(hurt_time) - partial_tick;
    if (dying) {
        const float fallen = std::min(static_cast<float>(death_time) + partial_tick, 20.0f);
        rotate(pose, rotation_degrees_z(40.0f - 8000.0f / (fallen + 200.0f)));
    }
    if (left < 0.0f) return;
    left /= static_cast<float>(hurt_duration);
    left = jmath::sin(static_cast<double>(left * left * left * left * kPi));
    rotate(pose, rotation_degrees_y(-hurt_dir));
    const float roll = static_cast<float>(-left * 14.0 * tilt_strength);
    rotate(pose, rotation_degrees_z(roll));
    rotate(pose, rotation_degrees_y(hurt_dir));
}

// `GameRenderer.bobView`: the step the whole view takes with every stride.
void bob_view(Pose& pose, float walk_dist, float bob) {
    translate(pose, jmath::sin(static_cast<double>(walk_dist * kPi)) * bob * 0.5f,
              -std::abs(jmath::cos(static_cast<double>(walk_dist * kPi)) * bob), 0.0f);
    rotate(pose, rotation_degrees_z(jmath::sin(static_cast<double>(walk_dist * kPi)) * bob
                                    * 3.0f));
    rotate(pose, rotation_degrees_x(
                     std::abs(jmath::cos(static_cast<double>(walk_dist * kPi - 0.2f)) * bob)
                     * 5.0f));
}

// `ItemInHandRenderer.evaluateWhichHandsToRender`,
// `ItemInHandRenderer.selectionUsingItemWhileHoldingBowLike`,
// `ItemInHandRenderer.isChargedCrossbow`.
HandSelection which_hands(const HandPlayer& player) {
    const bool bow = player.main_is_bow || player.off_is_bow;
    const bool crossbow = player.main_is_crossbow || player.off_is_crossbow;
    if (!bow && !crossbow) return {true, true};
    if (player.using_item) {
        const bool using_bow_like =
            player.used_hand == HandSide::Main
                ? player.main_is_bow || player.main_is_crossbow
                : player.off_is_bow || player.off_is_crossbow;
        if (!using_bow_like) {
            const bool charged_off = player.off_is_crossbow && player.off_crossbow_charged;
            return player.used_hand == HandSide::Main && charged_off ? HandSelection{true, false}
                                                                     : HandSelection{true, true};
        }
        return player.used_hand == HandSide::Main ? HandSelection{true, false}
                                                  : HandSelection{false, true};
    }
    const bool charged_main = player.main_is_crossbow && player.main_crossbow_charged;
    return charged_main ? HandSelection{true, false} : HandSelection{true, true};
}

// `ItemInHandRenderer.tick`: each hand's height falls to nothing when what it
// holds changes and climbs back over three ticks.
void HandBob::tick(const HandPlayer& player) {
    main_height_o_ = main_height_;
    off_height_o_ = off_height_;
    // `ItemInHandRenderer.shouldInstantlyReplaceVisibleItem`: the same item
    // swaps without an animation; a different one waits for the dip below.
    if (main_item_ == player.main_item || !player.main_swaps) main_item_ = player.main_item;
    if (off_item_ == player.off_item || !player.off_swaps) off_item_ = player.off_item;
    if (player.hands_busy) {
        main_height_ = std::clamp(main_height_ - 0.4f, 0.0f, 1.0f);
        off_height_ = std::clamp(off_height_ - 0.4f, 0.0f, 1.0f);
    } else {
        const float swap = player.item_swap_scale;
        const float main_target = main_item_ != player.main_item ? 0.0f : swap * swap * swap;
        const float off_target = off_item_ != player.off_item ? 0.0f : 1.0f;
        main_height_ += std::clamp(main_target - main_height_, -0.4f, 0.4f);
        off_height_ += std::clamp(off_target - off_height_, -0.4f, 0.4f);
    }
    if (main_height_ < 0.1f) main_item_ = player.main_item;
    if (off_height_ < 0.1f) off_item_ = player.off_item;
}

// `ItemInHandRenderer.itemUsed`.
void HandBob::item_used(HandSide hand) {
    if (hand == HandSide::Main) main_height_ = 0.0f;
    else off_height_ = 0.0f;
}

float HandBob::height(HandSide hand, float partial_tick) const {
    return hand == HandSide::Main ? lerp(partial_tick, main_height_o_, main_height_)
                                  : lerp(partial_tick, off_height_o_, off_height_);
}

namespace {

// `ItemInHandRenderer.renderArmWithItem`: one hand, with whatever is in it.
void arm_with_item(const HandPlayer& player, HandSide hand, float swing, int32_t item,
                   float equipped, Pose pose, HandDraw* out, int& count) {
    if (player.scoping) return;
    const bool main = hand == HandSide::Main;
    const Arm arm = main ? player.main_arm
                         : (player.main_arm == Arm::Right ? Arm::Left : Arm::Right);
    const int sign = arm == Arm::Right ? 1 : -1;
    const float sign_f = static_cast<float>(sign);
    const bool crossbow = main ? player.main_is_crossbow : player.off_is_crossbow;
    const bool charged = main ? player.main_crossbow_charged : player.off_crossbow_charged;
    const float partial = player.partial_tick;
    float pull = 0.0f;

    if (item == 0) {
        if (main && !player.invisible) {
            player_arm_transform(pose, equipped, swing, arm);
            out[count++] = {pose.matrix, arm, true, 0, 0.0f};
        }
        return;
    }

    if (crossbow) {
        apply_item_arm_transform(pose, arm, equipped);
        if (player.using_item && player.use_remaining > 0 && player.used_hand == hand
            && !charged) {
            translate(pose, sign_f * -0.4785682f, -0.094387f, 0.05731531f);
            rotate(pose, rotation_degrees_x(-11.935f));
            rotate(pose, rotation_degrees_y(sign_f * 65.3f));
            rotate(pose, rotation_degrees_z(sign_f * -9.785f));
            const float used = static_cast<float>(player.use_duration)
                               - (static_cast<float>(player.use_remaining) - partial + 1.0f);
            float charge = used / static_cast<float>(player.crossbow_charge_duration);
            if (charge > 1.0f) charge = 1.0f;
            if (charge > 0.1f) {
                const float shake =
                    jmath::sin(static_cast<double>((used - 0.1f) * 1.3f)) * (charge - 0.1f);
                translate(pose, shake * 0.0f, shake * 0.004f, shake * 0.0f);
            }
            translate(pose, charge * 0.0f, charge * 0.0f, charge * 0.04f);
            scale(pose, 1.0f, 1.0f, 1.0f + charge * 0.2f);
            rotate(pose, rotation_degrees_y(-(sign_f * 45.0f)));
            pull = charge;
        } else {
            swing_arm(pose, swing, sign, arm);
            if (charged && swing < 0.001f && main) {
                translate(pose, sign_f * -0.641864f, 0.0f, 0.0f);
                rotate(pose, rotation_degrees_y(sign_f * 10.0f));
            }
        }
        out[count++] = {pose.matrix, arm, false, item, pull};
        return;
    }

    if (player.using_item && player.use_remaining > 0 && player.used_hand == hand) {
        const UseAnimation animation = player.use_animation;
        if (!has_custom_arm_transform(animation)) {
            apply_item_arm_transform(pose, arm, equipped);
        }
        switch (animation) {
        case UseAnimation::Eat:
        case UseAnimation::Drink:
            apply_eat_transform(pose, partial, arm, player.use_remaining, player.use_duration);
            apply_item_arm_transform(pose, arm, equipped);
            break;
        case UseAnimation::Block:
            translate(pose, sign_f * -0.14142136f, 0.08f, 0.14142136f);
            rotate(pose, rotation_degrees_x(-102.25f));
            rotate(pose, rotation_degrees_y(sign_f * 13.365f));
            rotate(pose, rotation_degrees_z(sign_f * 78.05f));
            break;
        case UseAnimation::Bow: {
            translate(pose, sign_f * -0.2785682f, 0.18344387f, 0.15731531f);
            rotate(pose, rotation_degrees_x(-13.935f));
            rotate(pose, rotation_degrees_y(sign_f * 35.3f));
            rotate(pose, rotation_degrees_z(sign_f * -9.785f));
            const float used = static_cast<float>(player.use_duration)
                               - (static_cast<float>(player.use_remaining) - partial + 1.0f);
            float charge = used / 20.0f;
            charge = (charge * charge + charge * 2.0f) / 3.0f;
            if (charge > 1.0f) charge = 1.0f;
            if (charge > 0.1f) {
                const float shake =
                    jmath::sin(static_cast<double>((used - 0.1f) * 1.3f)) * (charge - 0.1f);
                translate(pose, shake * 0.0f, shake * 0.004f, shake * 0.0f);
            }
            translate(pose, charge * 0.0f, charge * 0.0f, charge * 0.04f);
            scale(pose, 1.0f, 1.0f, 1.0f + charge * 0.2f);
            rotate(pose, rotation_degrees_y(-(sign_f * 45.0f)));
            // `minecraft:use_duration` scaled by a twentieth, which is what
            // picks the drawn model; the eased charge only bends the arm.
            pull = std::min(used / 20.0f, 1.0f);
            break;
        }
        case UseAnimation::Trident: {
            translate(pose, sign_f * -0.5f, 0.7f, 0.1f);
            rotate(pose, rotation_degrees_x(-55.0f));
            rotate(pose, rotation_degrees_y(sign_f * 35.3f));
            rotate(pose, rotation_degrees_z(sign_f * -9.785f));
            const float used = static_cast<float>(player.use_duration)
                               - (static_cast<float>(player.use_remaining) - partial + 1.0f);
            float charge = used / 10.0f;
            if (charge > 1.0f) charge = 1.0f;
            if (charge > 0.1f) {
                const float shake =
                    jmath::sin(static_cast<double>((used - 0.1f) * 1.3f)) * (charge - 0.1f);
                translate(pose, shake * 0.0f, shake * 0.004f, shake * 0.0f);
            }
            translate(pose, 0.0f, 0.0f, charge * 0.2f);
            scale(pose, 1.0f, 1.0f, 1.0f + charge * 0.2f);
            rotate(pose, rotation_degrees_y(-(sign_f * 45.0f)));
            break;
        }
        case UseAnimation::Brush:
            apply_brush_transform(pose, partial, arm, player.use_remaining);
            break;
        case UseAnimation::Bundle:
            swing_arm(pose, swing, sign, arm);
            break;
        default:
            break;
        }
    } else if (player.auto_spin_attack) {
        apply_item_arm_transform(pose, arm, equipped);
        translate(pose, sign_f * -0.4f, 0.8f, 0.3f);
        rotate(pose, rotation_degrees_y(sign_f * 65.0f));
        rotate(pose, rotation_degrees_z(sign_f * -85.0f));
    } else {
        apply_item_arm_transform(pose, arm, equipped);
        if ((main ? player.main_swing : player.off_swing) == SwingKind::Whack) {
            swing_arm(pose, swing, sign, arm);
        }
    }
    out[count++] = {pose.matrix, arm, false, item, pull};
}

} // namespace

// `GameRenderer.renderItemInHand`, `ItemInHandRenderer.renderHandsWithItems`.
void hands_with_items(const HandPlayer& player, const HandBob& bob, HandDraw* out, int& count) {
    count = 0;
    Pose pose;
    bob_hurt(pose, player.partial_tick, player.hurt_time, player.hurt_duration, player.hurt_dir,
             player.damage_tilt, player.dying, player.death_time);
    if (player.view_bobbing) {
        bob_view(pose, lerp(player.partial_tick, player.walk_dist_o, player.walk_dist),
                 lerp(player.partial_tick, player.bob_o, player.bob));
    }

    const float swing_amount = player.attack_anim;
    const float x_bob = lerp(player.partial_tick, player.x_bob_o, player.x_bob);
    const float y_bob = lerp(player.partial_tick, player.y_bob_o, player.y_bob);
    rotate(pose, rotation_degrees_x((player.view_x_rot - x_bob) * 0.1f));
    rotate(pose, rotation_degrees_y((player.view_y_rot - y_bob) * 0.1f));

    const HandSelection selection = which_hands(player);
    if (selection.main) {
        const float swing = player.swinging_hand == HandSide::Main ? swing_amount : 0.0f;
        const float equipped = player.main_swap_animation_scale
                               * (1.0f - bob.height(HandSide::Main, player.partial_tick));
        arm_with_item(player, HandSide::Main, swing, bob.shown_main(), equipped, pose, out,
                      count);
    }
    if (selection.off) {
        const float swing = player.swinging_hand == HandSide::Off ? swing_amount : 0.0f;
        const float equipped = player.off_swap_animation_scale
                               * (1.0f - bob.height(HandSide::Off, player.partial_tick));
        arm_with_item(player, HandSide::Off, swing, bob.shown_off(), equipped, pose, out, count);
    }
}

} // namespace mcpp::client
