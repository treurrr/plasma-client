#include "mcpp/entity/mob_tick.hpp"

#include "mcpp/entity/ease.hpp"
#include "mcpp/entity/motion.hpp"

#include <algorithm>
#include <cmath>

namespace mcpp::entity {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kFifthPi = static_cast<float>(M_PI / 5);

// Every type whose own tick keeps an animation counter, or whose render flags
// all arrive in metadata; one entry per family that shares the class they
// come from.
enum class Kind : uint8_t {
    Other, Creeper, Sheep, Chicken, Wolf, Cat, Fox, Bee, Equine, IronGolem, SnowGolem, Slime,
    Allay, Axolotl, Armadillo, Breeze, CopperGolem, Villager, Witch, Zombie, ZombieVillager,
    Skeleton, Bogged,
};

Kind kind_of(std::string_view type) {
    if (type == "minecraft:creeper") return Kind::Creeper;
    if (type == "minecraft:sheep") return Kind::Sheep;
    if (type == "minecraft:chicken") return Kind::Chicken;
    if (type == "minecraft:wolf") return Kind::Wolf;
    if (type == "minecraft:cat" || type == "minecraft:ocelot") return Kind::Cat;
    if (type == "minecraft:fox") return Kind::Fox;
    if (type == "minecraft:bee") return Kind::Bee;
    if (type == "minecraft:horse" || type == "minecraft:donkey" || type == "minecraft:mule" ||
        type == "minecraft:skeleton_horse" || type == "minecraft:zombie_horse") {
        return Kind::Equine;
    }
    if (type == "minecraft:iron_golem") return Kind::IronGolem;
    if (type == "minecraft:snow_golem") return Kind::SnowGolem;
    if (type == "minecraft:slime" || type == "minecraft:magma_cube") return Kind::Slime;
    if (type == "minecraft:allay") return Kind::Allay;
    if (type == "minecraft:axolotl") return Kind::Axolotl;
    if (type == "minecraft:armadillo") return Kind::Armadillo;
    if (type == "minecraft:breeze") return Kind::Breeze;
    if (type == "minecraft:copper_golem") return Kind::CopperGolem;
    if (type == "minecraft:villager") return Kind::Villager;
    if (type == "minecraft:witch") return Kind::Witch;
    if (type == "minecraft:zombie" || type == "minecraft:husk" || type == "minecraft:drowned" ||
        type == "minecraft:zombified_piglin") {
        return Kind::Zombie;
    }
    if (type == "minecraft:zombie_villager") return Kind::ZombieVillager;
    if (type == "minecraft:bogged") return Kind::Bogged;
    if (type == "minecraft:skeleton" || type == "minecraft:stray" ||
        type == "minecraft:wither_skeleton") {
        return Kind::Skeleton;
    }
    return Kind::Other;
}

int64_t int_field(const proto::Metadata& data, std::string_view type, std::string_view name,
                  int64_t fallback) {
    const int index = proto::metadata_index(type, name);
    if (index < 0) return fallback;
    return data.int_at(static_cast<uint8_t>(index), fallback);
}

bool bool_field(const proto::Metadata& data, std::string_view type, std::string_view name) {
    const int index = proto::metadata_index(type, name);
    if (index < 0) return false;
    return data.bool_at(static_cast<uint8_t>(index), false);
}

bool bit_field(const proto::Metadata& data, std::string_view type, std::string_view name,
               int64_t bit) {
    return (int_field(data, type, name, 0) & bit) != 0;
}

// `Mob.isAggressive`, which the table names `flags_2`.
constexpr int64_t kMobNoAi = 1;
constexpr int64_t kMobAggressive = 4;

// `TamableAnimal.isInSittingPose`, which the table names `flags_3`.
constexpr int64_t kTameSitting = 1;
constexpr int64_t kTameTame = 4;

// `Fox.getFlag`.
constexpr int64_t kFoxSitting = 1;
constexpr int64_t kFoxCrouching = 4;
constexpr int64_t kFoxInterested = 8;
constexpr int64_t kFoxPouncing = 16;
constexpr int64_t kFoxSleeping = 32;
constexpr int64_t kFoxFaceplanted = 64;

// `Bee.getFlag`.
constexpr int64_t kBeeRoll = 2;
constexpr int64_t kBeeHasStung = 4;
constexpr int64_t kBeeHasNectar = 8;

// `AbstractHorse.getFlag`.
constexpr int64_t kHorseEating = 16;
constexpr int64_t kHorseStanding = 32;
constexpr int64_t kHorseOpenMouth = 64;

// `Sheep.getColor`, `SnowGolem.hasPumpkin`.
constexpr int64_t kSheepSheared = 16;
constexpr int64_t kSheepColor = 15;
constexpr int64_t kSnowGolemPumpkin = 16;

// `Entity.getSharedFlag`.
bool shared_flag(const proto::Metadata& data, proto::Metadata::Flag bit) {
    return (data.flags() & bit) != 0;
}

// `Entity.isCrouching`, which is the pose and not a shared flag.
bool crouching_pose(const proto::Metadata& data) {
    return data.pose() == proto::Pose::Crouching;
}

// `Entity.isFullyFrozen`, against the 140 ticks of `getTicksRequiredToFreeze`.
bool fully_frozen(const proto::Metadata& data) {
    return data.int_at(proto::Metadata::kFrozenTicks, 0) >= 140;
}

// `Creeper.getSwelling`: never synced, so a creeper carrying a `Fuse` of its own
// still swells at this pace.
constexpr int32_t kCreeperMaxSwell = 30;

// `Wolf.applyTamingSideEffects`: what `applyTamingSideEffects` puts on a tamed wolf's
// max health attribute, which no packet this client reads carries.
constexpr float kWolfTameHealth = 40.0f;

// `Armadillo.getState`, in the order the serializer numbers them.
constexpr int32_t kArmadilloRolling = 1;
constexpr int32_t kArmadilloScared = 2;
constexpr int32_t kArmadilloUnrolling = 3;

// `Armadillo.ArmadilloState.animationDuration`.
int32_t armadillo_duration(int32_t state) {
    switch (state) {
        case kArmadilloRolling: return 10;
        case kArmadilloScared: return 50;
        case kArmadilloUnrolling: return 30;
        default: return 0;
    }
}

// `Armadillo.ArmadilloState.shouldHideInShell`.
bool armadillo_hides(int32_t state, int64_t in_state_ticks) {
    switch (state) {
        case kArmadilloRolling: return in_state_ticks > 5;
        case kArmadilloScared: return true;
        case kArmadilloUnrolling: return in_state_ticks < 26;
        default: return false;
    }
}

// `CopperGolemState`, in the order the serializer numbers them.
constexpr int32_t kGolemGettingItem = 1;
constexpr int32_t kGolemGettingNoItem = 2;
constexpr int32_t kGolemDroppingItem = 3;
constexpr int32_t kGolemDroppingNoItem = 4;

// `Creeper.tick`: the fuse winds on by the swell dir the server sends, which
// an ignited creeper pins at one. `Creeper.isPowered`.
void tick_creeper(std::string_view type, const proto::Metadata& data, MobTick& own) {
    own.old_swell = own.swell;
    const int32_t dir = bool_field(data, type, "is_ignited")
                            ? 1
                            : static_cast<int32_t>(int_field(data, type, "state", -1));
    own.swell += dir;
    if (own.swell < 0) own.swell = 0;
    if (own.swell >= kCreeperMaxSwell) own.swell = kCreeperMaxSwell;

    own.flags.powered = bool_field(data, type, "is_charged");
}

// `Sheep.aiStep`: the forty ticks of grazing an entity event starts.
// `Sheep.isSheared`, `Sheep.getColor`.
void tick_sheep(std::string_view type, const proto::Metadata& data, MobTick& own) {
    own.eat_animation_tick = std::max(0, own.eat_animation_tick - 1);
    const int64_t wool = int_field(data, type, "flags_3", 0);
    own.flags.sheared = (wool & kSheepSheared) != 0;
    own.flags.wool_color = static_cast<uint8_t>(wool & kSheepColor);
}

// `Chicken.aiStep`: the wings beat harder the longer it has been off the
// ground, and the beat decays back out once it lands.
void tick_chicken(bool on_ground, MobTick& own) {
    own.o_flap = own.flap;
    own.o_flap_speed = own.flap_speed;
    own.flap_speed = own.flap_speed + (on_ground ? -1.0f : 4.0f) * 0.3f;
    own.flap_speed = std::clamp(own.flap_speed, 0.0f, 1.0f);
    if (!on_ground && own.flapping < 1.0f) own.flapping = 1.0f;
    own.flapping *= 0.9f;
    own.flap = own.flap + own.flapping * 2.0f;
}

// `Wolf.tick`: the head it cocks at a bone and the shake it dries off with.
// `NeutralMob.isAngry`, `Wolf.isInSittingPose`, `Wolf.getTailAngle`.
void tick_wolf(std::string_view type, const proto::Metadata& data, MobTick& own) {
    own.interested_angle_o = own.interested_angle;
    if (bool_field(data, type, "is_begging")) {
        own.interested_angle = own.interested_angle + (1.0f - own.interested_angle) * 0.4f;
    } else {
        own.interested_angle = own.interested_angle + (0.0f - own.interested_angle) * 0.4f;
    }

    if (own.in_water_or_rain) {
        own.is_wet = true;
    } else if ((own.is_wet || own.is_shaking) && own.is_shaking) {
        own.shake_anim_o = own.shake_anim;
        own.shake_anim += 0.05f;
        if (own.shake_anim_o >= 2.0f) {
            own.is_wet = false;
            own.is_shaking = false;
            own.shake_anim_o = 0.0f;
            own.shake_anim = 0.0f;
        }
    }

    const int64_t anger_end = int_field(data, type, "anger_time", -1);
    own.flags.angry = anger_end > 0 && anger_end - own.game_time > 0;
    own.flags.sitting = bit_field(data, type, "flags_3", kTameSitting);
    if (own.flags.angry) {
        own.flags.tail_angle = 1.5393804f;
    } else if (bit_field(data, type, "flags_3", kTameTame)) {
        const float health =
            static_cast<float>(data.float_at(proto::Metadata::kHealth, kWolfTameHealth));
        const float hurt = (kWolfTameHealth - health) / kWolfTameHealth;
        own.flags.tail_angle = (0.55f - hurt * 0.4f) * kPi;
    } else {
        own.flags.tail_angle = kFifthPi;
    }
}

// `Cat.updateLieDownAmount`, `Cat.updateRelaxStateOneAmount`.
void tick_cat(std::string_view type, const proto::Metadata& data, MobTick& own) {
    own.lie_down_amount_o = own.lie_down_amount;
    own.lie_down_amount_o_tail = own.lie_down_amount_tail;
    if (bool_field(data, type, "is_lying")) {
        own.lie_down_amount = std::min(1.0f, own.lie_down_amount + 0.15f);
        own.lie_down_amount_tail = std::min(1.0f, own.lie_down_amount_tail + 0.08f);
    } else {
        own.lie_down_amount = std::max(0.0f, own.lie_down_amount - 0.22f);
        own.lie_down_amount_tail = std::max(0.0f, own.lie_down_amount_tail - 0.13f);
    }

    own.relax_state_one_amount_o = own.relax_state_one_amount;
    if (bool_field(data, type, "is_relaxed")) {
        own.relax_state_one_amount = std::min(1.0f, own.relax_state_one_amount + 0.1f);
    } else {
        own.relax_state_one_amount = std::max(0.0f, own.relax_state_one_amount - 0.13f);
    }

    own.flags.sitting = bit_field(data, type, "flags_3", kTameSitting);
    own.flags.crouching = crouching_pose(data);
    own.flags.sprinting = shared_flag(data, proto::Metadata::Sprinting);
}

// `Fox.tick`: the head it cocks at prey, and the three ticks it takes to drop
// into a stalk. `Fox.isSitting`, `.isCrouching`, `.isPouncing`, `.isSleeping`,
// `.isFaceplanted`.
void tick_fox(std::string_view type, const proto::Metadata& data, MobTick& own) {
    own.interested_angle_o = own.interested_angle;
    if (bit_field(data, type, "flags_3", kFoxInterested)) {
        own.interested_angle = own.interested_angle + (1.0f - own.interested_angle) * 0.4f;
    } else {
        own.interested_angle = own.interested_angle + (0.0f - own.interested_angle) * 0.4f;
    }

    own.crouch_amount_o = own.crouch_amount;
    const bool crouching = bit_field(data, type, "flags_3", kFoxCrouching);
    if (crouching) {
        own.crouch_amount += 0.2f;
        if (own.crouch_amount > 3.0f) own.crouch_amount = 3.0f;
    } else {
        own.crouch_amount = 0.0f;
    }

    own.flags.crouching = crouching;
    own.flags.sitting = bit_field(data, type, "flags_3", kFoxSitting);
    own.flags.pouncing = bit_field(data, type, "flags_3", kFoxPouncing);
    own.flags.sleeping = bit_field(data, type, "flags_3", kFoxSleeping);
    own.flags.faceplanted = bit_field(data, type, "flags_3", kFoxFaceplanted);
}

// `Bee.updateRollAmount`: the barrel roll it goes into to sting.
// `Bee.hasStung`, `Bee.hasNectar`.
void tick_bee(std::string_view type, const proto::Metadata& data, MobTick& own) {
    own.roll_amount_o = own.roll_amount;
    if (bit_field(data, type, "flags_3", kBeeRoll)) {
        own.roll_amount = std::min(1.0f, own.roll_amount + 0.2f);
    } else {
        own.roll_amount = std::max(0.0f, own.roll_amount - 0.24f);
    }

    own.flags.has_stinger = !bit_field(data, type, "flags_3", kBeeHasStung);
    own.flags.has_nectar = bit_field(data, type, "flags_3", kBeeHasNectar);
}

// `AbstractHorse.aiStep`, `AbstractHorse.tick`: the tail flick, and the three
// eases the eating, rearing and open-mouth flags drive. `AbstractHorse.isEating`,
// `.isStanding`.
void tick_equine(std::string_view type, const proto::Metadata& data, MobTick& own) {
    if (own.random.next_int(200) == 0) own.tail_counter = 1;
    if (own.tail_counter > 0 && ++own.tail_counter > 8) own.tail_counter = 0;

    own.eat_anim_o = own.eat_anim;
    if (bit_field(data, type, "flags_3", kHorseEating)) {
        own.eat_anim = own.eat_anim + ((1.0f - own.eat_anim) * 0.4f + 0.05f);
        if (own.eat_anim > 1.0f) own.eat_anim = 1.0f;
    } else {
        own.eat_anim = own.eat_anim + ((0.0f - own.eat_anim) * 0.4f - 0.05f);
        if (own.eat_anim < 0.0f) own.eat_anim = 0.0f;
    }

    own.stand_anim_o = own.stand_anim;
    if (bit_field(data, type, "flags_3", kHorseStanding)) {
        own.eat_anim = 0.0f;
        own.eat_anim_o = own.eat_anim;
        own.stand_anim = own.stand_anim + ((1.0f - own.stand_anim) * 0.4f + 0.05f);
        if (own.stand_anim > 1.0f) own.stand_anim = 1.0f;
    } else {
        own.stand_anim =
            own.stand_anim +
            ((0.8f * own.stand_anim * own.stand_anim * own.stand_anim - own.stand_anim) * 0.6f -
             0.05f);
        if (own.stand_anim < 0.0f) own.stand_anim = 0.0f;
    }

    own.mouth_anim_o = own.mouth_anim;
    if (bit_field(data, type, "flags_3", kHorseOpenMouth)) {
        own.mouth_anim = own.mouth_anim + ((1.0f - own.mouth_anim) * 0.7f + 0.05f);
        if (own.mouth_anim > 1.0f) own.mouth_anim = 1.0f;
    } else {
        own.mouth_anim = own.mouth_anim + ((0.0f - own.mouth_anim) * 0.7f - 0.05f);
        if (own.mouth_anim < 0.0f) own.mouth_anim = 0.0f;
    }

    own.flags.has_chest = bool_field(data, type, "has_chest");
}

// `IronGolem.aiStep`: the swing and the poppy both just run down.
void tick_iron_golem(MobTick& own) {
    if (own.attack_animation_tick > 0) own.attack_animation_tick--;
    if (own.offer_flower_tick > 0) own.offer_flower_tick--;
}

// `Slime.tick`, `Slime.decreaseSquish`: the squash on landing and the stretch
// on leaving the ground. `Slime.getSize`.
void tick_slime(std::string_view type, const proto::Metadata& data, bool on_ground,
                MobTick& own) {
    own.o_squish = own.squish;
    own.squish = own.squish + (own.target_squish - own.squish) * 0.5f;
    if (on_ground && !own.was_on_ground) {
        own.target_squish = -0.5f;
    } else if (!on_ground && own.was_on_ground) {
        own.target_squish = 1.0f;
    }

    own.was_on_ground = on_ground;
    own.target_squish *= 0.6f;

    own.flags.slime_size = static_cast<int32_t>(int_field(data, type, "size", 1));
}

// `Allay.tick`: the five ticks of taking an item up, and the fifty-five tick
// bar of the dance, the first fifteen of which are a spin. `Allay.isDancing`.
void tick_allay(std::string_view type, const proto::Metadata& data, MobTick& own) {
    own.holding_item_animation_ticks_0 = own.holding_item_animation_ticks;
    if (own.has_item_in_hand) {
        own.holding_item_animation_ticks =
            std::clamp(own.holding_item_animation_ticks + 1.0f, 0.0f, 5.0f);
    } else {
        own.holding_item_animation_ticks =
            std::clamp(own.holding_item_animation_ticks - 1.0f, 0.0f, 5.0f);
    }

    const bool dancing = bool_field(data, type, "is_dancing");
    if (dancing) {
        own.dancing_animation_ticks++;
        own.spinning_animation_ticks_0 = own.spinning_animation_ticks;
        if (std::fmod(own.dancing_animation_ticks, 55.0f) < 15.0f) {
            own.spinning_animation_ticks++;
        } else {
            own.spinning_animation_ticks--;
        }

        own.spinning_animation_ticks = std::clamp(own.spinning_animation_ticks, 0.0f, 15.0f);
    } else {
        own.dancing_animation_ticks = 0.0f;
        own.spinning_animation_ticks = 0.0f;
        own.spinning_animation_ticks_0 = 0.0f;
    }

    own.flags.dancing = dancing;
}

// `Axolotl.tickAnimations`: one of four states wins the tick, and each of the
// four animators walks toward whether it is the one.
void tick_axolotl(std::string_view type, const proto::Metadata& data, bool on_ground,
                  float walk_speed, MobTick& own) {
    const bool playing_dead = bool_field(data, type, "it_currently_playing_dead");
    const bool in_water = !playing_dead && own.in_water;
    const bool grounded = !playing_dead && !own.in_water && on_ground;

    own.playing_dead_animator.tick(playing_dead);
    own.in_water_animator.tick(in_water);
    own.on_ground_animator.tick(grounded);
    own.moving_animator.tick(walk_speed > 1.0e-5f || own.look_changed);
}

// `Armadillo.setupAnimationStates`, `Armadillo.onSyncedDataUpdated`,
// `Armadillo.shouldHideInShell`: the state the server names, and how long it
// has held it.
void tick_armadillo(std::string_view type, const proto::Metadata& data, MobTick& own) {
    const int32_t state = static_cast<int32_t>(int_field(data, type, "armadillo_state", 0));
    if (state != own.armadillo_state) {
        own.armadillo_state = state;
        own.in_state_ticks = 0;
    }

    switch (state) {
        case kArmadilloRolling:
            own.roll_out.stop();
            own.roll_up.start_if_stopped(own.tick_count);
            own.peek.stop();
            break;
        case kArmadilloScared:
            own.roll_out.stop();
            own.roll_up.stop();
            if (own.peek_received_client) {
                own.peek.stop();
                own.peek_received_client = false;
            }

            if (own.in_state_ticks == 0) {
                own.peek.start(own.tick_count);
                // `AnimationState.fastForward`: the peek opens already finished.
                own.peek.start_tick -= armadillo_duration(kArmadilloScared);
            } else {
                own.peek.start_if_stopped(own.tick_count);
            }
            break;
        case kArmadilloUnrolling:
            own.roll_out.start_if_stopped(own.tick_count);
            own.roll_up.stop();
            own.peek.stop();
            break;
        default:
            own.roll_out.stop();
            own.roll_up.stop();
            own.peek.stop();
            break;
    }

    own.in_state_ticks++;
    own.flags.hiding_in_shell = armadillo_hides(state, own.in_state_ticks);
}

// `Breeze.onSyncedDataUpdated`, `Breeze.tick`: the pose picks the animation,
// and a pose that is no longer a slide plays the slide backwards. Both run
// before `super.tick` raises the tick count, so they date from the one before.
void tick_breeze(const proto::Metadata& data, MobTick& own) {
    const int32_t previous = own.tick_count - 1;
    const int32_t pose = static_cast<int32_t>(data.int_at(proto::Metadata::kPose, 0));
    if (pose != own.breeze_pose) {
        own.breeze_pose = pose;
        own.breeze_shoot.stop();
        own.breeze_idle.stop();
        own.breeze_inhale.stop();
        own.breeze_long_jump.stop();
        switch (static_cast<EntityPose>(pose)) {
            case EntityPose::Shooting: own.breeze_shoot.start_if_stopped(previous); break;
            case EntityPose::Inhaling: own.breeze_inhale.start_if_stopped(previous); break;
            case EntityPose::Sliding: own.breeze_slide.start_if_stopped(previous); break;
            default: break;
        }
    }

    if (static_cast<EntityPose>(pose) == EntityPose::LongJumping) {
        own.breeze_long_jump.start_if_stopped(previous);
    }

    own.breeze_idle.start_if_stopped(previous);
    if (static_cast<EntityPose>(pose) != EntityPose::Sliding && own.breeze_slide.running()) {
        own.breeze_slide_back.start(previous);
        own.breeze_slide.stop();
    }
}

// `CopperGolem.setupAnimationStates`: one animation per state, and an idle
// head spin due every two hundred-odd ticks.
void tick_copper_golem(std::string_view type, const proto::Metadata& data, MobTick& own) {
    if (bit_field(data, type, "flags_2", kMobNoAi)) return;

    switch (static_cast<int32_t>(int_field(data, type, "copper_golem_state", 0))) {
        case kGolemGettingItem:
            own.golem_idle.stop();
            own.idle_animation_start_tick = 0;
            own.golem_get_no_item.stop();
            own.golem_drop_item.stop();
            own.golem_drop_no_item.stop();
            own.golem_get_item.start_if_stopped(own.tick_count);
            break;
        case kGolemGettingNoItem:
            own.golem_idle.stop();
            own.idle_animation_start_tick = 0;
            own.golem_get_item.stop();
            own.golem_drop_no_item.stop();
            own.golem_drop_item.stop();
            own.golem_get_no_item.start_if_stopped(own.tick_count);
            break;
        case kGolemDroppingItem:
            own.golem_idle.stop();
            own.idle_animation_start_tick = 0;
            own.golem_get_item.stop();
            own.golem_get_no_item.stop();
            own.golem_drop_no_item.stop();
            own.golem_drop_item.start_if_stopped(own.tick_count);
            break;
        case kGolemDroppingNoItem:
            own.golem_idle.stop();
            own.idle_animation_start_tick = 0;
            own.golem_get_item.stop();
            own.golem_get_no_item.stop();
            own.golem_drop_item.stop();
            own.golem_drop_no_item.start_if_stopped(own.tick_count);
            break;
        default:
            own.golem_get_no_item.stop();
            own.golem_get_item.stop();
            own.golem_drop_item.stop();
            own.golem_drop_no_item.stop();
            if (own.idle_animation_start_tick == own.tick_count) {
                own.golem_idle.start(own.tick_count);
            } else if (own.idle_animation_start_tick == 0) {
                own.idle_animation_start_tick = own.tick_count + 200 + own.random.next_int(40);
            }

            if (own.tick_count == own.idle_animation_start_tick + 10) {
                own.idle_animation_start_tick = 0;
            }
            break;
    }
}

// `Villager.tick`, `Villager.getUnhappyCounter`: the head shake counts down
// from whatever the server last put on it.
void tick_villager(std::string_view type, const proto::Metadata& data, MobTick& own) {
    const int32_t sent = static_cast<int32_t>(int_field(data, type, "head_shake_timer", 0));
    if (sent != own.sent_unhappy_counter) {
        own.sent_unhappy_counter = sent;
        own.unhappy_counter = sent;
    }

    if (own.unhappy_counter > 0) own.unhappy_counter--;
}

// `WitchRenderer.extractRenderState` reads the stack in hand; `Witch.isDrinkingPotion`
// is what the wire carries, so both the item and the potion come off the flag.
void tick_witch(std::string_view type, const proto::Metadata& data, MobTick& own) {
    own.flags.holding_potion = bool_field(data, type, "is_drinking_potion");
}

// `Zombie.isUnderWaterConverting`, `ZombieVillager.isConverting`,
// `Mob.isAggressive`.
void tick_zombie(std::string_view type, const proto::Metadata& data, bool villager,
                 MobTick& own) {
    own.flags.aggressive = bit_field(data, type, "flags_2", kMobAggressive);
    own.flags.converting = bool_field(data, type,
                                      villager ? "is_converting" : "is_becoming_a_drowned");
}

// `AbstractSkeleton.isShaking` is the freeze; `Skeleton.isShaking` is the
// conversion into a stray. `Bogged.isSheared`, `Mob.isAggressive`.
void tick_skeleton(std::string_view type, const proto::Metadata& data, MobTick& own) {
    own.flags.aggressive = bit_field(data, type, "flags_2", kMobAggressive);
    own.flags.shaking = type == "minecraft:skeleton"
                            ? bool_field(data, type, "being_converted_into_stray")
                            : fully_frozen(data);
    own.flags.sheared = bool_field(data, type, "is_sheared");
}

} // namespace

// `BinaryAnimator.tick`.
void BinaryAnimator::tick(bool on) {
    ticks_old = ticks;
    if (on) {
        if (ticks < length) ticks++;
    } else if (ticks > 0) {
        ticks--;
    }
}

// `BinaryAnimator.getFactor`, through `BinaryAnimator.getFactor`.
float BinaryAnimator::factor(float partial) const {
    const float t = lerp(partial, static_cast<float>(ticks_old), static_cast<float>(ticks)) /
                    static_cast<float>(length);
    return ease::in_out_sine(t);
}

void tick_mob_counters(std::string_view type, const proto::Metadata& data, bool on_ground,
                       float walk_speed, MobTick& own) {
    // `Entity.baseTick`, which every mob reaches through `super.tick()` before
    // its own animation work, the breeze excepted.
    own.tick_count++;
    own.on_ground = on_ground;

    switch (kind_of(type)) {
        case Kind::Creeper: tick_creeper(type, data, own); break;
        case Kind::Sheep: tick_sheep(type, data, own); break;
        case Kind::Chicken: tick_chicken(on_ground, own); break;
        case Kind::Wolf: tick_wolf(type, data, own); break;
        case Kind::Cat: tick_cat(type, data, own); break;
        case Kind::Fox: tick_fox(type, data, own); break;
        case Kind::Bee: tick_bee(type, data, own); break;
        case Kind::Equine: tick_equine(type, data, own); break;
        case Kind::IronGolem: tick_iron_golem(own); break;
        case Kind::SnowGolem:
            own.flags.has_pumpkin = bit_field(data, type, "flags_3", kSnowGolemPumpkin);
            break;
        case Kind::Slime: tick_slime(type, data, on_ground, own); break;
        case Kind::Allay: tick_allay(type, data, own); break;
        case Kind::Axolotl: tick_axolotl(type, data, on_ground, walk_speed, own); break;
        case Kind::Armadillo: tick_armadillo(type, data, own); break;
        case Kind::Breeze: tick_breeze(data, own); break;
        case Kind::CopperGolem: tick_copper_golem(type, data, own); break;
        case Kind::Villager: tick_villager(type, data, own); break;
        case Kind::Witch: tick_witch(type, data, own); break;
        case Kind::Zombie: tick_zombie(type, data, false, own); break;
        case Kind::ZombieVillager: tick_zombie(type, data, true, own); break;
        case Kind::Skeleton:
        case Kind::Bogged: tick_skeleton(type, data, own); break;
        case Kind::Other: break;
    }
}

void read_mob_counters(std::string_view type, const MobTick& own, float partial, MobState& state) {
    state.tick = own.tick_count;
    state.partial = partial;

    switch (kind_of(type)) {
        // `CreeperRenderer.extractRenderState`, `Creeper.getSwelling`.
        case Kind::Creeper:
            state.swelling =
                lerp(partial, static_cast<float>(own.old_swell), static_cast<float>(own.swell)) /
                static_cast<float>(kCreeperMaxSwell - 2);
            state.powered = own.flags.powered;
            break;

        // `SheepRenderer.extractRenderState`, `Sheep.getHeadEatPositionScale`,
        // `Sheep.getHeadEatAngleScale`.
        case Kind::Sheep: {
            const int32_t eat = own.eat_animation_tick;
            const float eatf = static_cast<float>(eat);
            if (eat <= 0) {
                state.head_eat_position_scale = 0.0f;
            } else if (eat >= 4 && eat <= 36) {
                state.head_eat_position_scale = 1.0f;
            } else if (eat < 4) {
                state.head_eat_position_scale = (eatf - partial) / 4.0f;
            } else {
                state.head_eat_position_scale = -(eatf - 40.0f - partial) / 4.0f;
            }

            if (eat > 4 && eat <= 36) {
                const float t = (eatf - 4.0f - partial) / 32.0f;
                state.head_eat_angle_scale = kFifthPi + 0.21991149f * jmath::sin(t * 28.7f);
            } else if (eat > 0) {
                state.head_eat_angle_scale = kFifthPi;
            } else {
                state.head_eat_angle_scale = state.x_rot * (kPi / 180.0f);
            }

            state.sheared = own.flags.sheared;
            break;
        }

        // `ChickenRenderer.extractRenderState`.
        case Kind::Chicken:
            state.flap = lerp(partial, own.o_flap, own.flap);
            state.flap_speed = lerp(partial, own.o_flap_speed, own.flap_speed);
            break;

        // `WolfRenderer.extractRenderState`, `Wolf.getShakeAnim`,
        // `Wolf.getHeadRollAngle`, `Wolf.getWetShade`.
        case Kind::Wolf:
            state.shake_anim = lerp(partial, own.shake_anim_o, own.shake_anim);
            state.head_roll_angle =
                lerp(partial, own.interested_angle_o, own.interested_angle) * 0.15f * kPi;
            state.wet_shade =
                !own.is_wet ? 1.0f : std::min(0.75f + state.shake_anim / 2.0f * 0.25f, 1.0f);
            state.angry = own.flags.angry;
            state.sitting = own.flags.sitting;
            state.tail_angle = own.flags.tail_angle;
            break;

        // `CatRenderer.extractRenderState`, `Cat.getLieDownAmount`,
        // `.getLieDownAmountTail`, `.getRelaxStateOneAmount`.
        case Kind::Cat:
            state.lie_down_amount = lerp(partial, own.lie_down_amount_o, own.lie_down_amount);
            state.lie_down_amount_tail =
                lerp(partial, own.lie_down_amount_o_tail, own.lie_down_amount_tail);
            state.relax_state_one_amount =
                lerp(partial, own.relax_state_one_amount_o, own.relax_state_one_amount);
            state.sitting = own.flags.sitting;
            state.crouching = own.flags.crouching;
            state.sprinting = own.flags.sprinting;
            break;

        // `FoxRenderer.extractRenderState`, `Fox.getHeadRollAngle`,
        // `Fox.getCrouchAmount`.
        case Kind::Fox:
            state.head_roll_angle =
                lerp(partial, own.interested_angle_o, own.interested_angle) * 0.11f * kPi;
            state.crouch_amount = lerp(partial, own.crouch_amount_o, own.crouch_amount);
            state.crouching = own.flags.crouching;
            state.sitting = own.flags.sitting;
            state.pouncing = own.flags.pouncing;
            state.sleeping = own.flags.sleeping;
            state.faceplanted = own.flags.faceplanted;
            break;

        // `BeeRenderer.extractRenderState`, `Bee.getRollAmount`.
        case Kind::Bee:
            state.roll_amount = lerp(partial, own.roll_amount_o, own.roll_amount);
            state.on_ground = own.on_ground && own.nearly_motionless;
            state.has_stinger = own.flags.has_stinger;
            state.has_nectar = own.flags.has_nectar;
            break;

        // `AbstractHorseRenderer.extractRenderState`, `AbstractHorse.getEatAnim`,
        // `.getStandAnim`, `.getMouthAnim`.
        case Kind::Equine:
            state.eat_animation = lerp(partial, own.eat_anim_o, own.eat_anim);
            state.stand_animation = lerp(partial, own.stand_anim_o, own.stand_anim);
            state.feeding_animation = lerp(partial, own.mouth_anim_o, own.mouth_anim);
            state.animate_tail = own.tail_counter > 0;
            state.has_chest = own.flags.has_chest;
            break;

        // `IronGolemRenderer.extractRenderState`.
        case Kind::IronGolem:
            state.attack_ticks_remaining =
                own.attack_animation_tick > 0
                    ? static_cast<float>(own.attack_animation_tick) - partial
                    : 0.0f;
            state.offer_flower_tick = own.offer_flower_tick;
            break;

        // `SnowGolemRenderer.extractRenderState`.
        case Kind::SnowGolem: state.has_pumpkin = own.flags.has_pumpkin; break;

        // `SlimeRenderer.extractRenderState`.
        case Kind::Slime:
            state.squish = lerp(partial, own.o_squish, own.squish);
            state.slime_size = own.flags.slime_size;
            break;

        // `AllayRenderer.extractRenderState`, `Allay.isSpinning`,
        // `Allay.getSpinningProgress`, `Allay.getHoldingItemAnimationProgress`.
        case Kind::Allay:
            state.dancing = own.flags.dancing;
            state.spinning = std::fmod(own.dancing_animation_ticks, 55.0f) < 15.0f;
            state.spinning_progress =
                lerp(partial, own.spinning_animation_ticks_0, own.spinning_animation_ticks) /
                15.0f;
            state.holding_animation_progress =
                lerp(partial, own.holding_item_animation_ticks_0,
                     own.holding_item_animation_ticks) /
                5.0f;
            break;

        // `AxolotlRenderer.extractRenderState`.
        case Kind::Axolotl:
            state.playing_dead_factor = own.playing_dead_animator.factor(partial);
            state.in_water_factor = own.in_water_animator.factor(partial);
            state.on_ground_factor = own.on_ground_animator.factor(partial);
            state.moving_factor = own.moving_animator.factor(partial);
            break;

        // `ArmadilloRenderer.extractRenderState`.
        case Kind::Armadillo:
            state.hiding_in_shell = own.flags.hiding_in_shell;
            state.roll_out = own.roll_out;
            state.roll_up = own.roll_up;
            state.peek = own.peek;
            break;

        // `BreezeRenderer.extractRenderState`.
        case Kind::Breeze:
            state.breeze_idle = own.breeze_idle;
            state.breeze_shoot = own.breeze_shoot;
            state.breeze_slide = own.breeze_slide;
            state.breeze_slide_back = own.breeze_slide_back;
            state.breeze_inhale = own.breeze_inhale;
            state.breeze_long_jump = own.breeze_long_jump;
            break;

        // `CopperGolemRenderer.extractRenderState`.
        case Kind::CopperGolem:
            state.golem_idle = own.golem_idle;
            state.golem_get_item = own.golem_get_item;
            state.golem_get_no_item = own.golem_get_no_item;
            state.golem_drop_item = own.golem_drop_item;
            state.golem_drop_no_item = own.golem_drop_no_item;
            break;

        // `VillagerRenderer.extractRenderState`.
        case Kind::Villager: state.unhappy = own.unhappy_counter > 0; break;

        // `WitchRenderer.extractRenderState`.
        case Kind::Witch:
            state.holding_potion = own.flags.holding_potion;
            state.holding_item = own.flags.holding_potion;
            break;

        // `AbstractZombieRenderer.extractRenderState`,
        // `ZombieVillagerRenderer.extractRenderState`.
        case Kind::Zombie:
        case Kind::ZombieVillager:
            state.aggressive = own.flags.aggressive;
            state.converting = own.flags.converting;
            break;

        // `AbstractSkeletonRenderer.extractRenderState`,
        // `BoggedRenderer.extractRenderState`.
        case Kind::Skeleton:
        case Kind::Bogged:
            state.aggressive = own.flags.aggressive;
            state.shaking = own.flags.shaking;
            state.sheared = own.flags.sheared;
            break;

        case Kind::Other: break;
    }
}

void mob_entity_event(std::string_view type, uint8_t status, MobTick& own) {
    switch (kind_of(type)) {
        // `Sheep.handleEntityEvent`: forty ticks with its head in the grass.
        case Kind::Sheep:
            if (status == 10) own.eat_animation_tick = 40;
            break;

        // `Wolf.handleEntityEvent`, `Wolf.cancelShake`: start shaking dry, or
        // give it up because it walked back into the water.
        case Kind::Wolf:
            if (status == 8) {
                own.is_shaking = true;
                own.shake_anim = 0.0f;
                own.shake_anim_o = 0.0f;
            } else if (status == 56) {
                own.is_shaking = false;
                own.shake_anim = 0.0f;
                own.shake_anim_o = 0.0f;
            }
            break;

        // `IronGolem.handleEntityEvent`: the swing, and the poppy it holds out
        // for twenty seconds or puts away.
        case Kind::IronGolem:
            if (status == 4) {
                own.attack_animation_tick = 10;
            } else if (status == 11) {
                own.offer_flower_tick = 400;
            } else if (status == 34) {
                own.offer_flower_tick = 0;
            }
            break;

        // `Armadillo.handleEntityEvent`: `Armadillo.handleEntityEvent`, which
        // the next tick restarts the peek from.
        case Kind::Armadillo:
            if (status == 64) own.peek_received_client = true;
            break;

        default: break;
    }
}

} // namespace mcpp::entity
