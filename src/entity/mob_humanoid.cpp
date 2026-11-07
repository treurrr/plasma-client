#include "mcpp/entity/humanoid.hpp"

#include "mcpp/entity/motion.hpp"
#include "mcpp/jmath.hpp"
#include "mob_factory.hpp"

#include <array>
#include <cmath>
#include <string_view>

namespace mcpp::entity {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegrees = kPi / 180.0f;

// `AnimationUtils.bobModelPart`: the idle sway of one arm.
void bob_model_part(PartPose& part, float age, float side) {
    part.z_rot = part.z_rot + side * (jmath::cos(age * 0.09f) * 0.05f + 0.05f);
    part.x_rot = part.x_rot + side * (jmath::sin(age * 0.067f) * 0.05f);
}

// `AnimationUtils.bobArms`.
void bob_arms(PartPose& first, PartPose& second, float age) {
    bob_model_part(first, age, 1.0f);
    bob_model_part(second, age, -1.0f);
}

// `AnimationUtils.animateZombieArms`: both arms out in front, further out
// when it is chasing, swinging with the attack, and always swaying.
void animate_zombie_arms(PartPose& left_arm, PartPose& right_arm, bool aggressive,
                         const MobState& s) {
    if (s.swing_animation != SwingAnimation::Stab) {
        const float time = s.attack_time;
        const float reach = -kPi / (aggressive ? 1.5f : 2.25f);
        const float swing = jmath::sin(time * kPi);
        const float recover = jmath::sin((1.0f - (1.0f - time) * (1.0f - time)) * kPi);
        right_arm.z_rot = 0.0f;
        right_arm.y_rot = -(0.1f - swing * 0.6f);
        right_arm.x_rot = reach;
        right_arm.x_rot += swing * 1.2f - recover * 0.4f;
        left_arm.z_rot = 0.0f;
        left_arm.y_rot = 0.1f - swing * 0.6f;
        left_arm.x_rot = reach;
        left_arm.x_rot += swing * 1.2f - recover * 0.4f;
    }
    bob_arms(right_arm, left_arm, s.age_in_ticks);
}

// `VillagerModel.setupAnim`, `WitchModel.setupAnim`: the leg swing both
// share, which is the humanoid's with the arms' half-speed factor.
void villager_legs(const MobState& s, PartPose& right_leg, PartPose& left_leg) {
    right_leg.x_rot = jmath::cos(s.walk_position * 0.6662f) * 1.4f * s.walk_speed * 0.5f;
    left_leg.x_rot = jmath::cos(s.walk_position * 0.6662f + kPi) * 1.4f * s.walk_speed * 0.5f;
}

// `AbstractZombieModel`: a humanoid whose arms are then thrown out in front.
class AbstractZombie : public Humanoid {
public:
    explicit AbstractZombie(const Model& model) : Humanoid(model) {}

    // `AbstractZombieModel.setupAnim`.
    void animate(const MobState& s, Pose& pose) const override {
        humanoid_anim(s, pose);
        animate_zombie_arms(pose[left_arm_], pose[right_arm_], s.aggressive, s);
    }

    // `AbstractZombieRenderer.isShaking`: it also shakes while it drowns.
    bool is_shaking(const MobState& s) const override {
        return MobModel::is_shaking(s) || s.converting;
    }
};

// `ZombieModel`: the zombie, the husk, and the body the drowned builds on.
class Zombie : public AbstractZombie {
public:
    explicit Zombie(const Model& model) : AbstractZombie(model) {}
};

// `ZombieVillagerModel`: the villager's head and smock on a humanoid body,
// animated like a zombie without going through `AbstractZombieModel`.
class ZombieVillager : public Humanoid {
public:
    explicit ZombieVillager(const Model& model) : Humanoid(model) {}

    // `ZombieVillagerModel.setupAnim`.
    void animate(const MobState& s, Pose& pose) const override {
        humanoid_anim(s, pose);
        animate_zombie_arms(pose[left_arm_], pose[right_arm_], s.aggressive, s);
    }

    // `ZombieVillagerRenderer.isShaking`.
    bool is_shaking(const MobState& s) const override {
        return MobModel::is_shaking(s) || s.converting;
    }
};

// `DrownedModel`: the zombie, then the trident throw and the swim stroke.
class Drowned : public Zombie {
public:
    explicit Drowned(const Model& model) : Zombie(model) {}

    void animate(const MobState& s, Pose& pose) const override;
    void setup_rotations(const MobState& s, Placement& place, float body_rot,
                         float scale) const override;
};

// `DrownedModel.setupAnim`.
void Drowned::animate(const MobState& s, Pose& pose) const {
    Zombie::animate(s, pose);
    PartPose& right_arm = pose[right_arm_];
    PartPose& left_arm = pose[left_arm_];
    if (s.left_arm_pose == ArmPose::ThrowTrident) {
        left_arm.x_rot = left_arm.x_rot * 0.5f - kPi;
        left_arm.y_rot = 0.0f;
    }
    if (s.right_arm_pose == ArmPose::ThrowTrident) {
        right_arm.x_rot = right_arm.x_rot * 0.5f - kPi;
        right_arm.y_rot = 0.0f;
    }

    const float swim = s.swim_amount;
    if (swim > 0.0f) {
        constexpr float kReach = static_cast<float>(-M_PI * 4.0 / 5.0);
        const float stroke = jmath::sin(0.1f * s.age_in_ticks);
        right_arm.x_rot = rot_lerp_rad(swim, right_arm.x_rot, kReach) + swim * 0.35f * stroke;
        left_arm.x_rot = rot_lerp_rad(swim, left_arm.x_rot, kReach) - swim * 0.35f * stroke;
        right_arm.z_rot = rot_lerp_rad(swim, right_arm.z_rot, -0.15f);
        left_arm.z_rot = rot_lerp_rad(swim, left_arm.z_rot, 0.15f);
        pose[left_leg_].x_rot = pose[left_leg_].x_rot - swim * 0.55f * stroke;
        pose[right_leg_].x_rot = pose[right_leg_].x_rot + swim * 0.55f * stroke;
        pose[head_].x_rot = 0.0f;
    }
}

// `DrownedRenderer.setupRotations`: swimming tips the whole body forward
// about its own middle.
void Drowned::setup_rotations(const MobState& s, Placement& place, float body_rot,
                              float scale) const {
    Zombie::setup_rotations(s, place, body_rot, scale);
    const float swim = s.swim_amount;
    if (swim > 0.0f) {
        const float target = -10.0f - s.x_rot;
        const float angle = lerp(swim, 0.0f, target);
        const float pivot = s.bounding_box_height / 2.0f / scale;
        place.translate(0.0f, pivot, 0.0f);
        place.rotate(quaternion_x(to_radians(angle)));
        place.translate(0.0f, -pivot, 0.0f);
    }
}

// `SkeletonModel`: the humanoid, plus the two-armed melee swing it makes
// when it is chasing with anything but a bow.
class Skeleton : public Humanoid {
public:
    explicit Skeleton(const Model& model) : Humanoid(model) {}

    // `SkeletonModel.setupAnim`.
    void animate(const MobState& s, Pose& pose) const override {
        humanoid_anim(s, pose);
        if (s.aggressive && !s.holding_bow) {
            const float time = s.attack_time;
            const float swing = jmath::sin(time * kPi);
            const float recover = jmath::sin((1.0f - (1.0f - time) * (1.0f - time)) * kPi);
            PartPose& right_arm = pose[right_arm_];
            PartPose& left_arm = pose[left_arm_];
            right_arm.z_rot = 0.0f;
            left_arm.z_rot = 0.0f;
            right_arm.y_rot = -(0.1f - swing * 0.6f);
            left_arm.y_rot = 0.1f - swing * 0.6f;
            right_arm.x_rot = -kPi / 2.0f;
            left_arm.x_rot = -kPi / 2.0f;
            right_arm.x_rot -= swing * 1.2f - recover * 0.4f;
            left_arm.x_rot -= swing * 1.2f - recover * 0.4f;
            bob_arms(right_arm, left_arm, s.age_in_ticks);
        }
    }

    // `AbstractSkeletonRenderer.isShaking`: the stray's freeze is the only
    // thing that shakes a skeleton.
    bool is_shaking(const MobState& s) const override { return s.shaking; }
};

// `BoggedModel`: a skeleton with mushrooms on its skull until it is sheared.
class Bogged : public Skeleton {
public:
    explicit Bogged(const Model& model) : Skeleton(model), mushrooms_(part("head/mushrooms")) {}

    // `BoggedModel.setupAnim`.
    void animate(const MobState& s, Pose& pose) const override {
        Skeleton::animate(s, pose);
        pose[mushrooms_].visible = !s.sheared;
    }

private:
    int32_t mushrooms_ = -1;
};

// `VillagerModel`: a head that looks and sulks, two legs that walk, and arms
// crossed in one fixed part.
class Villager : public MobModel {
public:
    explicit Villager(const Model& model)
        : MobModel(model), head_(part("head")), right_leg_(part("right_leg")),
          left_leg_(part("left_leg")) {}

    // `VillagerModel.setupAnim`.
    void animate(const MobState& s, Pose& pose) const override {
        reset(pose);
        PartPose& head = pose[head_];
        head.y_rot = s.y_rot * kDegrees;
        head.x_rot = s.x_rot * kDegrees;
        if (s.unhappy) {
            head.z_rot = 0.3f * jmath::sin(0.45f * s.age_in_ticks);
            head.x_rot = 0.4f;
        } else {
            head.z_rot = 0.0f;
        }
        villager_legs(s, pose[right_leg_], pose[left_leg_]);
        pose[right_leg_].y_rot = 0.0f;
        pose[left_leg_].y_rot = 0.0f;
    }

protected:
    int32_t head_ = -1, right_leg_ = -1, left_leg_ = -1;
};

// `WitchModel`: the villager's body under a hat, with a nose that twitches
// at a rate the entity's own id picks and swings down over a potion.
class Witch : public MobModel {
public:
    explicit Witch(const Model& model)
        : MobModel(model), head_(part("head")), nose_(part("head/nose")),
          right_leg_(part("right_leg")), left_leg_(part("left_leg")) {}

    // `WitchModel.setupAnim`.
    void animate(const MobState& s, Pose& pose) const override {
        reset(pose);
        PartPose& head = pose[head_];
        PartPose& nose = pose[nose_];
        head.y_rot = s.y_rot * kDegrees;
        head.x_rot = s.x_rot * kDegrees;
        villager_legs(s, pose[right_leg_], pose[left_leg_]);
        const float rate = 0.01f * static_cast<float>(s.entity_id % 10);
        nose.x_rot = jmath::sin(s.age_in_ticks * rate) * 4.5f * kDegrees;
        nose.z_rot = jmath::cos(s.age_in_ticks * rate) * 2.5f * kDegrees;
        if (s.holding_item) {
            nose.x = 0.0f;
            nose.y = 1.0f;
            nose.z = -1.5f;
            nose.x_rot = -0.9f;
        }
    }

private:
    int32_t head_ = -1, nose_ = -1, right_leg_ = -1, left_leg_ = -1;
};

// `HumanoidArmorLayer`: the four slots are baked as sub-layers of the body
// they cover, and `ArmorModelSet.bake` builds each of them with the mob's own
// model class, so a zombie's armour swings its arms out like the zombie does.
constexpr std::array<std::string_view, 4> kArmourSlots = {"#helmet", "#chestplate", "#leggings",
                                                          "#boots"};
constexpr std::array<std::string_view, 12> kArmouredBodies = {
    "zombie",  "zombie_baby", "zombie_villager", "zombie_villager_baby",
    "drowned", "drowned_baby", "husk",           "husk_baby",
    "skeleton", "stray",      "wither_skeleton", "bogged",
};

// The body an armour sub-layer covers, empty when the layer is not one.
std::string_view armour_body(std::string_view layer) {
    const size_t hash = layer.find('#');
    if (hash == std::string_view::npos) return {};
    const std::string_view body = layer.substr(0, hash);
    const std::string_view slot = layer.substr(hash);
    bool slot_known = false;
    for (std::string_view known : kArmourSlots) slot_known = slot_known || slot == known;
    if (!slot_known) return {};
    for (std::string_view known : kArmouredBodies) {
        if (body == known) return body;
    }
    return {};
}

constexpr std::array<MobOverlay, 1> kDrownedOverlays = {{
    {"drowned#outer", "zombie/drowned_outer_layer", Overlay::Plain,
     gpu::EntityRenderer::Type::CutoutNoCull},
}};
constexpr std::array<MobOverlay, 1> kStrayOverlays = {{
    {"stray#outer", "skeleton/stray_overlay", Overlay::Plain,
     gpu::EntityRenderer::Type::CutoutNoCull},
}};
constexpr std::array<MobOverlay, 1> kBoggedOverlays = {{
    {"bogged#outer", "skeleton/bogged_overlay", Overlay::Plain,
     gpu::EntityRenderer::Type::CutoutNoCull},
}};

constexpr std::array<MobType, 10> kTypes = {{
    {"minecraft:zombie", "zombie", "zombie/zombie", "zombie_baby", 0.5f,
     gpu::EntityRenderer::Type::CutoutNoCull, true, true, {}},
    {"minecraft:zombie_villager", "zombie_villager", "zombie_villager/zombie_villager",
     "zombie_villager_baby", 0.5f, gpu::EntityRenderer::Type::CutoutNoCull, true, true, {}},
    {"minecraft:drowned", "drowned", "zombie/drowned", "drowned_baby", 0.5f,
     gpu::EntityRenderer::Type::CutoutNoCull, true, true, kDrownedOverlays},
    {"minecraft:husk", "husk", "zombie/husk", "husk_baby", 0.5f,
     gpu::EntityRenderer::Type::CutoutNoCull, true, true, {}},
    {"minecraft:skeleton", "skeleton", "skeleton/skeleton", "", 0.5f,
     gpu::EntityRenderer::Type::CutoutNoCull, true, true, {}},
    {"minecraft:stray", "stray", "skeleton/stray", "", 0.5f,
     gpu::EntityRenderer::Type::CutoutNoCull, true, true, kStrayOverlays},
    {"minecraft:wither_skeleton", "wither_skeleton", "skeleton/wither_skeleton", "", 0.5f,
     gpu::EntityRenderer::Type::CutoutNoCull, true, true, {}},
    {"minecraft:bogged", "bogged", "skeleton/bogged", "", 0.5f,
     gpu::EntityRenderer::Type::CutoutNoCull, true, true, kBoggedOverlays},
    {"minecraft:witch", "witch", "witch", "", 0.5f, gpu::EntityRenderer::Type::CutoutNoCull,
     false, false, {}},
    {"minecraft:villager", "villager", "villager/villager", "villager_baby", 0.5f,
     gpu::EntityRenderer::Type::CutoutNoCull, false, false, {}},
}};

} // namespace

std::unique_ptr<MobModel> make_humanoid_mob_model(std::string_view layer, const Model& model) {
    if (const std::string_view body = armour_body(layer); !body.empty()) {
        // `AbstractSkeletonRenderer`: a bogged's armour is a plain
        // `SkeletonModel`, without the mushrooms its own skull wears.
        return make_humanoid_mob_model(body == "bogged" ? std::string_view("skeleton") : body,
                                       model);
    }
    if (layer == "zombie" || layer == "zombie_baby" || layer == "husk" || layer == "husk_baby") {
        return std::make_unique<Zombie>(model);
    }
    if (layer == "drowned" || layer == "drowned_baby" || layer == "drowned#outer" ||
        layer == "drowned_baby#outer") {
        return std::make_unique<Drowned>(model);
    }
    if (layer == "zombie_villager" || layer == "zombie_villager_baby" ||
        layer == "zombie_villager_no_hat" || layer == "zombie_villager_baby_no_hat") {
        return std::make_unique<ZombieVillager>(model);
    }
    // The stray's and the bogged's clothing is a `SkeletonModel` of its own,
    // posed from the body underneath.
    if (layer == "skeleton" || layer == "stray" || layer == "wither_skeleton" ||
        layer == "stray#outer" || layer == "bogged#outer") {
        return std::make_unique<Skeleton>(model);
    }
    if (layer == "bogged") return std::make_unique<Bogged>(model);
    if (layer == "witch") return std::make_unique<Witch>(model);
    if (layer == "villager" || layer == "villager_baby" || layer == "villager_no_hat" ||
        layer == "villager_baby_no_hat") {
        return std::make_unique<Villager>(model);
    }
    return nullptr;
}

std::span<const MobType> humanoid_mob_types() { return kTypes; }

} // namespace mcpp::entity
