#pragma once
#include "mcpp/entity/mobs.hpp"
#include "mcpp/jmath.hpp"
#include "mcpp/proto/metadata.hpp"

#include <cstdint>
#include <string_view>

namespace mcpp::entity {

// `BinaryAnimator`: a counter that walks up to its length while a condition
// holds and back down to zero once it stops, read through an easing curve.
struct BinaryAnimator {
    int32_t length = 10;
    int32_t ticks = 0;
    int32_t ticks_old = 0;

    void tick(bool on);
    // The curve is `BinaryAnimator.getFactor`, the only one the mobs here ask for.
    float factor(float partial) const;
};

// The counters a mob's own tick keeps, which no packet carries: each with the
// previous tick's value beside it, so a frame between two ticks can ease.
struct MobTick {
    // `Entity.tick`: what the keyframe animations date themselves from.
    int32_t tick_count = 0;

    // Inputs vanilla's own tick reads off the entity that this client does not
    // keep yet. Each rests at the value that holds its counter still; a caller
    // that learns one may write it before the tick.
    bool in_water = false;           // `Axolotl.isInWater`
    bool in_water_or_rain = false;   // `Wolf.isInWaterOrRain`
    bool look_changed = false;       // the pitch or yaw moved this tick, for `Axolotl`
    bool has_item_in_hand = false;   // `Allay.hasItemInHand`
    bool nearly_motionless = true;   // `Bee`'s landed pose, which wants the velocity
    // `Level.getGameTime`, which `NeutralMob.isAngry` measures the anger end
    // time the wolf's metadata carries against.
    int64_t game_time = 0;

    // `Mob.aiStep`'s own random, which vanilla seeds per entity. A caller that
    // wants two horses not to flick their tails in lockstep seeds it once.
    jmath::LegacyRandom random{0};

    // `Creeper.tick`.
    int32_t swell = 0;
    int32_t old_swell = 0;

    // `Sheep.aiStep`.
    int32_t eat_animation_tick = 0;

    // `Chicken.aiStep`.
    float flap = 0.0f;
    float o_flap = 0.0f;
    float flap_speed = 0.0f;
    float o_flap_speed = 0.0f;
    float flapping = 1.0f;

    // `Wolf.getHeadRollAngle`, `Wolf.getShakeAnim`, which the fox shares the first of.
    float interested_angle = 0.0f;
    float interested_angle_o = 0.0f;
    float shake_anim = 0.0f;
    float shake_anim_o = 0.0f;
    bool is_wet = false;
    bool is_shaking = false;

    // `Cat.getLieDownAmount`, `Cat.getRelaxStateOneAmount`.
    float lie_down_amount = 0.0f;
    float lie_down_amount_o = 0.0f;
    float lie_down_amount_tail = 0.0f;
    float lie_down_amount_o_tail = 0.0f;
    float relax_state_one_amount = 0.0f;
    float relax_state_one_amount_o = 0.0f;

    // `Fox.getCrouchAmount`.
    float crouch_amount = 0.0f;
    float crouch_amount_o = 0.0f;

    // `Bee.getRollAmount`.
    float roll_amount = 0.0f;
    float roll_amount_o = 0.0f;

    // `AbstractHorse.getEatAnim`, `.standAnim`, `.mouthAnim`, `.tailCounter`.
    float eat_anim = 0.0f;
    float eat_anim_o = 0.0f;
    float stand_anim = 0.0f;
    float stand_anim_o = 0.0f;
    float mouth_anim = 0.0f;
    float mouth_anim_o = 0.0f;
    int32_t tail_counter = 0;

    // `IronGolem.tick`, `.offerFlowerTick`.
    int32_t attack_animation_tick = 0;
    int32_t offer_flower_tick = 0;

    // `Slime.tick`.
    float squish = 0.0f;
    float o_squish = 0.0f;
    float target_squish = 0.0f;
    bool was_on_ground = false;

    // `Allay.getHoldingItemAnimationProgress`, `.dancingAnimationTicks`,
    // `.spinningAnimationTicks`.
    float holding_item_animation_ticks = 0.0f;
    float holding_item_animation_ticks_0 = 0.0f;
    float dancing_animation_ticks = 0.0f;
    float spinning_animation_ticks = 0.0f;
    float spinning_animation_ticks_0 = 0.0f;

    // `Axolotl`'s four ten-tick animators.
    BinaryAnimator playing_dead_animator;
    BinaryAnimator in_water_animator;
    BinaryAnimator on_ground_animator;
    BinaryAnimator moving_animator;

    // `Armadillo.tick` and its three animations; the state rests at -1,
    // "the server has not said yet", so its first word counts as the change
    // `onSyncedDataUpdated` sees.
    AnimationState roll_out, roll_up, peek;
    int64_t in_state_ticks = 0;
    int32_t armadillo_state = -1;
    bool peek_received_client = false;

    // `Breeze`'s six animations, keyed off the pose it was last seen in.
    AnimationState breeze_idle, breeze_shoot, breeze_slide, breeze_slide_back, breeze_inhale,
        breeze_long_jump;
    int32_t breeze_pose = -1;

    // `CopperGolem`'s five animations and the tick its idle head spin is due on.
    AnimationState golem_idle, golem_get_item, golem_get_no_item, golem_drop_item,
        golem_drop_no_item;
    int32_t idle_animation_start_tick = 0;

    // `Villager.getUnhappyCounter`, which the client counts down off the last
    // value the server sent.
    int32_t unhappy_counter = 0;
    int32_t sent_unhappy_counter = 0;

    // What a mob's `extractRenderState` reads straight out of its metadata,
    // resolved once a tick so a frame needs nothing but these counters. Named
    // after the `MobState` fields they end up in.
    struct Flags {
        bool aggressive = false;
        bool powered = false;
        bool angry = false;
        bool sitting = false;
        bool crouching = false;
        bool sprinting = false;
        bool sleeping = false;
        bool faceplanted = false;
        bool pouncing = false;
        bool has_stinger = true;
        bool has_nectar = false;
        bool has_chest = false;
        bool has_pumpkin = true;
        bool sheared = false;
        bool dancing = false;
        bool holding_potion = false;
        bool converting = false;
        bool shaking = false;
        bool hiding_in_shell = false;
        int32_t slime_size = 1;
        float tail_angle = 0.62831855f;
        // `Sheep.getColor`, a `DyeColor` id: `MobState` has nowhere for it, so
        // it waits here until the wool overlay can be tinted per entity.
        uint8_t wool_color = 0;
    } flags;

    // What the tick saw and the frame between two ticks needs again.
    bool on_ground = false;
};

// One client tick of one mob: what `<Mob>.tick`/`.aiStep` does to its own
// animation counters. `type` is the registry name ("minecraft:creeper"),
// `data` is everything set_entity_data has said about it, `on_ground` and
// `walk_speed` are what the shared `Living` tick already worked out.
void tick_mob_counters(std::string_view type, const proto::Metadata& data, bool on_ground,
                       float walk_speed, MobTick& own);

// What the renderer's `extractRenderState` reads off those counters for this
// frame, `partial` of the way into the next tick. It also sets `state.tick`
// and `state.partial`, which the keyframe animations here date themselves
// from, and it reads `state.x_rot` for a grazing sheep, so the shared living
// fields belong in `state` before the call.
void read_mob_counters(std::string_view type, const MobTick& own, float partial, MobState& state);

// `<Mob>.handleEntityEvent`: a status byte that starts one of them.
void mob_entity_event(std::string_view type, uint8_t status, MobTick& own);

} // namespace mcpp::entity
