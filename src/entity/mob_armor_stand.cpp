#include "mcpp/entity/humanoid.hpp"

#include "mcpp/jmath.hpp"
#include "mob_factory.hpp"

#include <array>
#include <string_view>

namespace mcpp::entity {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegrees = kPi / 180.0f;

// The six `Rotations` an armour stand carries, in the order `ArmorStand`
// defines them, which is the order metadata indices 16 to 21 arrive in.
enum StandPose : size_t { Head, Body, LeftArm, RightArm, LeftLeg, RightLeg };

// `ArmorStandArmorModel`: a humanoid whose every part is turned by the pose
// the server sends rather than by a walk. The four armour layers over a stand
// are this model too, which is why the poses live here and not in the stand's
// own class.
class ArmorStandArmor : public Humanoid {
public:
    explicit ArmorStandArmor(const Model& model) : Humanoid(model) {}

    // `ArmorStandArmorModel.setupAnim`.
    void animate(const MobState& s, Pose& pose) const override {
        humanoid_anim(s, pose);
        turn(pose[head_], s.stand_poses[Head]);
        turn(pose[body_], s.stand_poses[Body]);
        turn(pose[left_arm_], s.stand_poses[LeftArm]);
        turn(pose[right_arm_], s.stand_poses[RightArm]);
        turn(pose[left_leg_], s.stand_poses[LeftLeg]);
        turn(pose[right_leg_], s.stand_poses[RightLeg]);
    }

    // `ArmorStandRenderer.setupRotations`: the body's yaw and, for five ticks
    // after a hit, the wobble it was knocked into. Nothing of
    // `LivingEntityRenderer.setupRotations` is kept, so a stand neither tips
    // over when it dies nor hangs upside down.
    void setup_rotations(const MobState& s, Placement& place, float body_rot,
                         float) const override {
        place.rotate(quaternion_y(to_radians(180.0f - body_rot)));
        if (s.wiggle < 5.0f) {
            place.rotate(quaternion_y(to_radians(jmath::sin(s.wiggle / 1.5f * kPi) * 3.0f)));
        }
    }

protected:
    static void turn(PartPose& part, const std::array<float, 3>& degrees) {
        part.x_rot = kDegrees * degrees[0];
        part.y_rot = kDegrees * degrees[1];
        part.z_rot = kDegrees * degrees[2];
    }
};

// `ArmorStandModel`: the sticks the wooden stand is made of, with the arms and
// the base plate the server can take away, and no hat ever.
class ArmorStand : public ArmorStandArmor {
public:
    explicit ArmorStand(const Model& model)
        : ArmorStandArmor(model), right_body_stick_(part("right_body_stick")),
          left_body_stick_(part("left_body_stick")), shoulder_stick_(part("shoulder_stick")),
          base_plate_(part("base_plate")) {}

    // `ArmorStandModel.setupAnim`, and the hat its constructor hides for good.
    void animate(const MobState& s, Pose& pose) const override {
        ArmorStandArmor::animate(s, pose);
        pose[hat()].visible = false;
        // The plate turns against the body's own yaw, which the renderer put
        // in `yRot` before the model ran.
        pose[base_plate_].y_rot = kDegrees * -s.body_rot;
        pose[arm(true)].visible = s.show_arms;
        pose[arm(false)].visible = s.show_arms;
        pose[base_plate_].visible = s.show_base_plate;
        turn(pose[right_body_stick_], s.stand_poses[Body]);
        turn(pose[left_body_stick_], s.stand_poses[Body]);
        turn(pose[shoulder_stick_], s.stand_poses[Body]);
    }

private:
    int32_t right_body_stick_ = -1, left_body_stick_ = -1, shoulder_stick_ = -1, base_plate_ = -1;
};

// `ArmorStandRenderer`: the small stand is a layer of its own, with the half
// scale baked into every part, so it rides the baby slot of the type table.
constexpr std::array<MobType, 1> kTypes = {{
    {"minecraft:armor_stand", "armor_stand", "armorstand/wood", "armor_stand_small", 0.0f,
     gpu::EntityRenderer::Type::CutoutNoCull, true, true, {}},
}};

constexpr std::array<std::string_view, 4> kArmourSlots = {"#helmet", "#chestplate", "#leggings",
                                                          "#boots"};

} // namespace

std::unique_ptr<MobModel> make_armor_stand_model(std::string_view layer, const Model& model) {
    const size_t hash = layer.find('#');
    const std::string_view body = layer.substr(0, hash);
    if (body != "armor_stand" && body != "armor_stand_small") return nullptr;
    if (hash == std::string_view::npos) return std::make_unique<ArmorStand>(model);
    for (const std::string_view slot : kArmourSlots) {
        if (layer.substr(hash) == slot) return std::make_unique<ArmorStandArmor>(model);
    }
    return nullptr;
}

std::span<const MobType> armor_stand_mob_types() { return kTypes; }

} // namespace mcpp::entity
