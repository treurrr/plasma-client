// The hand's own state, which is not matrix arithmetic and so is not in
// HandOracle: how far the hand dips when it swaps what it holds, how long a
// swing lasts, and which hands a bow leaves on screen.
#include "client/hand.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL %s\n", what);
    ++failures;
}

void close(float got, float want, const char* what) {
    check(std::fabs(got - want) < 1e-5f, what);
    if (std::fabs(got - want) >= 1e-5f) std::printf("  got %f, wanted %f\n", got, want);
}

using namespace mcpp::client;

// `ItemInHandRenderer.tick`: the first tick only notices what the hand now
// holds; from there it climbs four tenths a tick until it is all the way up.
void equip_climbs() {
    HandBob bob;
    HandPlayer player;
    player.main_item = 7;
    bob.tick(player);
    check(bob.shown_main() == 7, "the first tick picks the item up");
    close(bob.height(HandSide::Main, 1.0f), 0.0f, "with the hand still down");
    bob.tick(player);
    close(bob.height(HandSide::Main, 1.0f), 0.4f, "then four tenths");
    bob.tick(player);
    close(bob.height(HandSide::Main, 1.0f), 0.8f, "then eight");
    bob.tick(player);
    close(bob.height(HandSide::Main, 1.0f), 1.0f, "and all the way up");
    bob.tick(player);
    close(bob.height(HandSide::Main, 1.0f), 1.0f, "where it stays");
    close(bob.height(HandSide::Main, 0.5f), 1.0f, "halfway between two full ticks");
}

// A different item dips the hand out of sight first, swaps at the bottom, and
// climbs back: the item on screen only changes below a tenth.
void swap_dips() {
    HandBob bob;
    HandPlayer holding;
    holding.main_item = 7;
    for (int tick = 0; tick < 4; ++tick) bob.tick(holding);
    HandPlayer swapping = holding;
    swapping.main_item = 9;
    bob.tick(swapping);
    close(bob.height(HandSide::Main, 1.0f), 0.6f, "the hand falls four tenths");
    check(bob.shown_main() == 7, "and still holds the old item");
    bob.tick(swapping);
    close(bob.height(HandSide::Main, 1.0f), 0.2f, "and falls again");
    check(bob.shown_main() == 7, "still the old item above a tenth");
    bob.tick(swapping);
    check(bob.shown_main() == 9, "below a tenth it is the new one");
    bob.tick(swapping);
    close(bob.height(HandSide::Main, 1.0f), 0.4f, "and it climbs back");
}

// `LivingEntity.updateSwingTime`: six ticks, and a swing restarts past half.
void swing_runs_six_ticks() {
    SwingClock clock;
    clock.swing();
    check(clock.swinging(), "a swing starts");
    float seen[7]{};
    for (int tick = 0; tick < 7; ++tick) {
        clock.tick();
        seen[tick] = clock.amount(1.0f);
    }
    close(seen[0], 0.0f, "the first tick is the start of the swing");
    close(seen[1], 1.0f / 6.0f, "a sixth");
    close(seen[5], 5.0f / 6.0f, "five sixths");
    // getAttackAnim wraps rather than jumping back, so the tick the swing
    // ends on reads as the end of it and not the start of another.
    close(seen[6], 1.0f, "and the last tick reads as the end of the swing");
    check(!clock.swinging(), "the swing has stopped");
}

// `ItemInHandRenderer.evaluateWhichHandsToRender`.
void bows_hide_the_other_hand() {
    HandPlayer player;
    check(which_hands(player).main && which_hands(player).off, "two empty hands both draw");
    player.main_is_crossbow = true;
    player.main_crossbow_charged = true;
    check(which_hands(player).main && !which_hands(player).off,
          "a charged crossbow hides the off hand");
    player.main_crossbow_charged = false;
    check(which_hands(player).off, "an empty crossbow does not");
    player.main_is_bow = true;
    player.using_item = true;
    player.used_hand = HandSide::Main;
    check(which_hands(player).main && !which_hands(player).off,
          "drawing a bow hides the off hand");
    player.used_hand = HandSide::Off;
    check(which_hands(player).main && which_hands(player).off,
          "but using something else in the off hand draws both");
    player.off_is_bow = true;
    check(!which_hands(player).main && which_hands(player).off,
          "and drawing a bow in the off hand hides the main one");
}

// `ItemInHandRenderer.applyItemArmTransform`: the hand's rest, lowered by the
// equip height, which is the one place a wrong sign is invisible in a matrix.
void the_arm_rests_where_it_should() {
    Pose pose;
    apply_item_arm_transform(pose, Arm::Right, 0.0f);
    close(pose.matrix[12], 0.56f, "the right hand sits to the right");
    close(pose.matrix[13], -0.52f, "and below the middle");
    close(pose.matrix[14], -0.72f, "and in front");
    Pose left;
    apply_item_arm_transform(left, Arm::Left, 1.0f);
    close(left.matrix[12], -0.56f, "the left hand mirrors it");
    close(left.matrix[13], -1.12f, "a hand mid swap is lowered six tenths more");
}

} // namespace

int main() {
    equip_climbs();
    swap_dips();
    swing_runs_six_ticks();
    bows_hide_the_other_hand();
    the_arm_rests_where_it_should();
    if (failures == 0) std::printf("hand: every check passed\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
