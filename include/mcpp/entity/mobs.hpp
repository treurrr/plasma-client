#pragma once
#include "mcpp/entity/model.hpp"
#include "mcpp/gpu/entity_renderer.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mcpp::entity {

// `HumanoidModel.ArmPose`: what an arm is doing with what it holds, and the
// two answers the pose gives about itself, which decide which arm is posed
// first and whether the other one is posed at all.
enum class ArmPose : uint8_t {
    Empty, Item, Block, BowAndArrow, ThrowTrident, CrossbowCharge, CrossbowHold, Spyglass,
    TootHorn, Brush, Spear,
};
bool two_handed(ArmPose pose);
bool affects_offhand_pose(ArmPose pose);

// `SwingAnimationType`: which swing the item in hand asks for.
enum class SwingAnimation : uint8_t { None, Whack, Stab };

// `Pose`: the entity poses the server sends in metadata index six.
enum class EntityPose : uint8_t {
    Standing, FallFlying, Sleeping, Swimming, SpinAttack, Crouching, LongJumping, Dying,
    Croaking, UsingTongue, Sitting, Roaring, Sniffing, Emerging, Digging, Sliding, Shooting,
    Inhaling,
};

// `AnimationState`: a keyframe animation that is either stopped or has been
// running since some tick.
struct AnimationState {
    bool started = false;
    int64_t start_tick = 0;
    float accumulated = 0.0f;

    void start(int64_t tick);
    void start_if_stopped(int64_t tick);
    void stop();
    bool running() const { return started; }
    // `AnimationState.getAccumulatedTime`: milliseconds since it started.
    float elapsed(int64_t tick, float partial) const;
};

// Every render state field any mob's `setupAnim` reads, in one struct: the
// hierarchy the game splits over `EntityRenderState`, `LivingEntityRenderState`
// and a class per mob, flattened, since one client keeps one of these per
// entity and nothing here is virtual.
struct MobState {
    // `EntityRenderState`.
    int32_t entity_id = 0;
    float age_in_ticks = 0.0f;
    float bounding_box_width = 0.6f;
    float bounding_box_height = 1.8f;
    float eye_height = 1.62f;
    bool invisible = false;

    // `LivingEntityRenderState`.
    float body_rot = 0.0f;
    float y_rot = 0.0f;   // head yaw minus body yaw, degrees
    float x_rot = 0.0f;   // pitch, degrees
    float death_time = 0.0f;
    float walk_position = 0.0f;
    float walk_speed = 0.0f;
    float scale = 1.0f;
    float age_scale = 1.0f;
    bool upside_down = false;
    bool fully_frozen = false;
    bool baby = false;
    bool in_water = false;
    bool auto_spin_attack = false;
    bool red_overlay = false;
    EntityPose pose = EntityPose::Standing;
    // `Direction` of the bed, 2..5 (north, south, west, east), -1 when awake.
    int8_t bed_orientation = -1;

    // `ArmedEntityRenderState`.
    bool main_arm_right = true;
    ArmPose right_arm_pose = ArmPose::Empty;
    ArmPose left_arm_pose = ArmPose::Empty;
    bool right_hand_item_empty = true;
    bool left_hand_item_empty = true;
    SwingAnimation swing_animation = SwingAnimation::Whack;
    float attack_time = 0.0f;

    // `HumanoidRenderState`.
    float swim_amount = 0.0f;
    float speed_value = 1.0f;
    // `HumanoidRenderState.maxCrossbowChargeDuration`: zero until the renderer
    // reads `CrossbowItem.getChargeDuration` off the item in hand, which is
    // what the crossbow arm pose divides by.
    float max_crossbow_charge_duration = 0.0f;
    float ticks_using_item = 0.0f;
    bool attack_arm_left = false;
    bool use_item_off_hand = false;
    bool crouching = false;
    bool fall_flying = false;
    bool visually_swimming = false;
    bool passenger = false;
    bool using_item = false;
    float elytra_rot_x = 0.0f, elytra_rot_y = 0.0f, elytra_rot_z = 0.0f;

    // `AvatarRenderState`: the flight the elytra pose and the body's tilt read.
    float fall_flying_ticks = 0.0f;
    bool apply_flying_y_rot = false;
    float flying_y_rot = 0.0f;

    // `AvatarRenderState`: the skin's own layers.
    bool spectator = false;
    bool show_hat = true, show_jacket = true;
    bool show_left_sleeve = true, show_right_sleeve = true;
    bool show_left_pants = true, show_right_pants = true;

    // `ZombieRenderState`, `SkeletonRenderState`.
    bool aggressive = false;
    bool converting = false;
    bool shaking = false;
    bool holding_bow = false;
    bool sheared = false;   // bogged, and sheep

    // `CreeperRenderState`.
    float swelling = 0.0f;
    bool powered = false;

    // `SlimeRenderState`.
    float squish = 0.0f;
    int32_t slime_size = 1;

    // `EndermanRenderState`.
    bool creepy = false;
    bool carries_block = false;

    // `SheepRenderState`.
    float head_eat_position_scale = 0.0f;
    float head_eat_angle_scale = 0.0f;
    // `DyeColor`'s own id; the rainbow a sheep called "jeb_" wears instead.
    int8_t wool_color = 0;
    bool jeb_sheep = false;

    // `CopperGolemRenderState`: `CopperGolemState` and `WeatheringCopper.WeatherState`.
    uint8_t copper_golem_state = 0;
    uint8_t weathering = 0;

    // `ChickenRenderState`.
    float flap = 0.0f;
    float flap_speed = 0.0f;

    // `EquineRenderState`.
    bool saddled = false;
    bool ridden = false;
    bool animate_tail = false;
    float eat_animation = 0.0f;
    float stand_animation = 0.0f;
    float feeding_animation = 0.0f;

    // `WolfRenderState`.
    bool angry = false;
    bool sitting = false;
    float tail_angle = 0.62831855f;
    float head_roll_angle = 0.0f;
    float shake_anim = 0.0f;
    float wet_shade = 1.0f;

    // `CatRenderState`, `DonkeyRenderState`.
    bool lying_on_top_of_sleeping_player = false;
    bool has_chest = false;
    // `DyeColor`'s own id, or -1 when the animal wears no collar.
    int8_t collar_color = -1;

    // `FelineRenderState`.
    bool sprinting = false;
    float lie_down_amount = 0.0f;
    float lie_down_amount_tail = 0.0f;
    float relax_state_one_amount = 0.0f;

    // `VillagerRenderState`, `WitchRenderState`.
    bool unhappy = false;
    bool holding_item = false;
    bool holding_potion = false;

    // `IronGolemRenderState`, `SnowGolemRenderState`.
    float attack_ticks_remaining = 0.0f;
    int32_t offer_flower_tick = 0;
    bool has_pumpkin = true;

    // `BeeRenderState`.
    float roll_amount = 0.0f;
    bool has_stinger = true;
    bool on_ground = false;
    bool has_nectar = false;

    // `FoxRenderState`.
    float crouch_amount = 0.0f;
    bool sleeping = false;
    bool faceplanted = false;
    bool pouncing = false;

    // `AxolotlRenderState`.
    float playing_dead_factor = 0.0f;
    float moving_factor = 0.0f;
    float in_water_factor = 1.0f;
    float on_ground_factor = 0.0f;

    // `AllayRenderState`.
    bool dancing = false;
    bool spinning = false;
    float spinning_progress = 0.0f;
    float holding_animation_progress = 0.0f;

    // `ArmadilloRenderState`.
    bool hiding_in_shell = false;
    AnimationState roll_out, roll_up, peek;

    // `BreezeRenderState`.
    AnimationState breeze_idle, breeze_shoot, breeze_slide, breeze_slide_back, breeze_inhale,
        breeze_long_jump;

    // `CopperGolemRenderState`.
    AnimationState golem_idle, golem_get_item, golem_get_no_item, golem_drop_item,
        golem_drop_no_item;

    // `CopperGolemStatueBlockRenderer`: the `Direction` a statue faces, 2..5,
    // the same numbering as `bed_orientation`.
    int8_t facing = 3;

    // What the keyframe animations measure elapsed time against.
    int64_t tick = 0;
    float partial = 0.0f;

    bool has_pose(EntityPose p) const { return pose == p; }
    // `AvatarRenderState.fallFlyingScale`.
    float fall_flying_scale() const;
};

// One model layer with every part it animates already found, the way the
// game's `EntityModel` subclasses keep their `ModelPart` fields: built once
// per layer, then asked for a pose per entity per frame.
class MobModel {
public:
    virtual ~MobModel() = default;

    // `EntityModel.setupAnim`: resets the pose to the layer's own values and
    // writes this frame's.
    virtual void animate(const MobState& state, Pose& pose) const = 0;
    // `LivingEntityRenderer.scale` and `AgeableMobRenderer`: what the renderer
    // puts on the pose stack after the flip, before the -1.501 lift.
    virtual void scale(const MobState& state, Placement& place) const;
    // `LivingEntityRenderer.setupRotations`: the body's yaw, the death tilt,
    // the spin attack, sleeping and upside down, before the flip.
    virtual void setup_rotations(const MobState& state, Placement& place, float body_rot,
                                 float scale) const;
    // `LivingEntityRenderer.getWhiteOverlayProgress`: the white flash, 0 to 1.
    virtual float white_overlay(const MobState& state) const;
    // `LivingEntityRenderer.getModelTint`: ARGB multiplied into the model.
    virtual uint32_t tint(const MobState& state) const;
    // `LivingEntityRenderer.getFlipDegrees`: how far it tips over when it dies.
    virtual float flip_degrees() const { return 90.0f; }
    // `LivingEntityRenderer.isShaking`.
    virtual bool is_shaking(const MobState& state) const { return state.fully_frozen; }

    const Model& model() const { return *model_; }
    // `ArmedModel.translateToHand`: the part chain an item in that hand hangs
    // from, outermost first, or empty when the model holds nothing.
    virtual std::span<const int32_t> hand_chain(bool left) const;

protected:
    explicit MobModel(const Model& model) : model_(&model) {}
    // The index of a part by path; throws when the layer has no such part, so
    // a model and a layer that disagree are found at load instead of by a
    // wrong pose.
    int32_t part(std::string_view path) const;
    // -1 when the layer has no such part.
    int32_t optional(std::string_view path) const;
    // `EntityModel.setupAnim`: every part back to the layer's own pose.
    void reset(Pose& pose) const;

private:
    const Model* model_;
};

// What one mob layer is drawn on top of its base layer for: a second model
// over the same body, with its own texture.
enum class Overlay : uint8_t {
    // Its own render type and texture, posed by the base model.
    Plain,
    // `EyesLayer`: drawn bright, through the dark.
    Eyes,
    // `CreeperPowerLayer`, `SpinAttackEffectLayer`: the energy swirl.
    EnergySwirl,
};

struct MobOverlay {
    std::string_view layer;     // "sheep#wool", empty to reuse the base layer
    std::string_view texture;   // under textures/entity/, no extension
    Overlay kind = Overlay::Plain;
    gpu::EntityRenderer::Type render = gpu::EntityRenderer::Type::Cutout;
    // What the layer multiplies into the model, the way each of the game's own
    // layers works its colour out: a sheep's wool, a collar's dye. White when
    // the layer names none.
    uint32_t (*colour)(const MobState&) = nullptr;
    // The layer a baby wears instead, empty when it wears the same one.
    std::string_view baby_layer;
    // Whether the layer is drawn at all: a creeper's charge, a saddle, a
    // sheep's wool. Always, when the layer names no condition.
    bool (*when)(const MobState&) = nullptr;
};

// One entity type as its renderer draws it.
struct MobType {
    std::string_view type;         // "minecraft:zombie"
    std::string_view layer;        // the main model layer
    std::string_view texture;      // under textures/entity/, no extension
    std::string_view baby_layer;   // empty when the type has no baby
    float shadow_radius = 0.5f;
    gpu::EntityRenderer::Type render = gpu::EntityRenderer::Type::CutoutNoCull;
    // `HumanoidArmorLayer`: the four armour slots over a humanoid body.
    bool humanoid_armour = false;
    // `ItemInHandLayer`: what it holds, in world space.
    bool items_in_hand = false;
    std::span<const MobOverlay> overlays;
};

// `Direction.getStepX`/`.getStepZ` of a bed's facing, for the shuffle a
// sleeping body makes toward the pillow.
std::array<int8_t, 2> bed_step(int8_t orientation);

// Every type this client draws, by registry name; nullptr for one it does not.
const MobType* mob_type(std::string_view type);
std::span<const MobType> mob_types();

// The model for a layer, or nullptr when no class animates it. The model
// outlives the call; `models` must outlive it too.
std::unique_ptr<MobModel> make_mob_model(std::string_view layer, const Model& model);

} // namespace mcpp::entity
