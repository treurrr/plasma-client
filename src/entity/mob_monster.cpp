#include "mcpp/entity/humanoid.hpp"
#include "mcpp/entity/mobs.hpp"
#include "mcpp/entity/motion.hpp"
#include "mcpp/jmath.hpp"

#include "mob_factory.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace mcpp::entity {
namespace {

constexpr float kPi = static_cast<float>(M_PI);
constexpr float kDegrees = static_cast<float>(M_PI / 180.0);

// `Mth.triangleWave`: the saw the golem's limbs swing on.
float triangle_wave(float value, float period) {
    return (std::fabs(std::fmod(value, period) - period * 0.5f) - period * 0.25f) /
           (period * 0.25f);
}

// `CreeperModel`: a head and four legs, with the field names the game swaps
// between the left and the right side when it looks them up.
class Creeper : public MobModel {
public:
    explicit Creeper(const Model& model) : MobModel(model) {
        head_ = part("head");
        left_hind_leg_ = part("right_hind_leg");
        right_hind_leg_ = part("left_hind_leg");
        left_front_leg_ = part("right_front_leg");
        right_front_leg_ = part("left_front_leg");
    }

    // `CreeperModel.setupAnim`: the head follows the look, the legs the walk.
    void animate(const MobState& s, Pose& pose) const override {
        reset(pose);
        pose[head_].y_rot = s.y_rot * kDegrees;
        pose[head_].x_rot = s.x_rot * kDegrees;
        const float speed = s.walk_speed;
        const float walked = s.walk_position;
        pose[right_hind_leg_].x_rot = jmath::cos(walked * 0.6662f) * 1.4f * speed;
        pose[left_hind_leg_].x_rot = jmath::cos(walked * 0.6662f + kPi) * 1.4f * speed;
        pose[right_front_leg_].x_rot = jmath::cos(walked * 0.6662f + kPi) * 1.4f * speed;
        pose[left_front_leg_].x_rot = jmath::cos(walked * 0.6662f) * 1.4f * speed;
    }

    // `CreeperRenderer.scale`: the swell, with the shiver on top of it.
    void scale(const MobState& s, Placement& place) const override {
        float swell = s.swelling;
        const float shiver = 1.0f + jmath::sin(swell * 100.0f) * swell * 0.01f;
        swell = std::clamp(swell, 0.0f, 1.0f);
        swell *= swell;
        swell *= swell;
        const float wide = (1.0f + swell * 0.4f) * shiver;
        const float tall = (1.0f + swell * 0.1f) / shiver;
        place.scale(wide, tall, wide);
    }

    // `CreeperRenderer.getWhiteOverlayProgress`: the flash it blinks while it
    // swells.
    float white_overlay(const MobState& s) const override {
        const float swell = s.swelling;
        return static_cast<int>(swell * 10.0f) % 2 == 0 ? 0.0f : std::clamp(swell, 0.5f, 1.0f);
    }

private:
    int32_t head_ = -1;
    int32_t right_hind_leg_ = -1, left_hind_leg_ = -1;
    int32_t right_front_leg_ = -1, left_front_leg_ = -1;
};

// `SpiderModel`: the head, two body segments and eight legs of a spider.
class Spider : public MobModel {
public:
    explicit Spider(const Model& model) : MobModel(model) {
        head_ = part("head");
        right_hind_leg_ = part("right_hind_leg");
        left_hind_leg_ = part("left_hind_leg");
        right_middle_hind_leg_ = part("right_middle_hind_leg");
        left_middle_hind_leg_ = part("left_middle_hind_leg");
        right_middle_front_leg_ = part("right_middle_front_leg");
        left_middle_front_leg_ = part("left_middle_front_leg");
        right_front_leg_ = part("right_front_leg");
        left_front_leg_ = part("left_front_leg");
    }

    // `SpiderModel.setupAnim`: the eight legs on four phases of the walk, each
    // pair mirrored.
    void animate(const MobState& s, Pose& pose) const override {
        reset(pose);
        pose[head_].y_rot = s.y_rot * kDegrees;
        pose[head_].x_rot = s.x_rot * kDegrees;
        const float walked = s.walk_position * 0.6662f;
        const float speed = s.walk_speed;
        const float yaw0 = -(jmath::cos(walked * 2.0f + 0.0f) * 0.4f) * speed;
        const float yaw1 = -(jmath::cos(walked * 2.0f + kPi) * 0.4f) * speed;
        const float yaw2 =
            -(jmath::cos(walked * 2.0f + static_cast<float>(M_PI / 2)) * 0.4f) * speed;
        const float yaw3 =
            -(jmath::cos(walked * 2.0f + static_cast<float>(M_PI * 3.0 / 2.0)) * 0.4f) * speed;
        const float lift0 = std::fabs(jmath::sin(walked + 0.0f) * 0.4f) * speed;
        const float lift1 = std::fabs(jmath::sin(walked + kPi) * 0.4f) * speed;
        const float lift2 =
            std::fabs(jmath::sin(walked + static_cast<float>(M_PI / 2)) * 0.4f) * speed;
        const float lift3 =
            std::fabs(jmath::sin(walked + static_cast<float>(M_PI * 3.0 / 2.0)) * 0.4f) * speed;
        pose[right_hind_leg_].y_rot += yaw0;
        pose[left_hind_leg_].y_rot -= yaw0;
        pose[right_middle_hind_leg_].y_rot += yaw1;
        pose[left_middle_hind_leg_].y_rot -= yaw1;
        pose[right_middle_front_leg_].y_rot += yaw2;
        pose[left_middle_front_leg_].y_rot -= yaw2;
        pose[right_front_leg_].y_rot += yaw3;
        pose[left_front_leg_].y_rot -= yaw3;
        pose[right_hind_leg_].z_rot += lift0;
        pose[left_hind_leg_].z_rot -= lift0;
        pose[right_middle_hind_leg_].z_rot += lift1;
        pose[left_middle_hind_leg_].z_rot -= lift1;
        pose[right_middle_front_leg_].z_rot += lift2;
        pose[left_middle_front_leg_].z_rot -= lift2;
        pose[right_front_leg_].z_rot += lift3;
        pose[left_front_leg_].z_rot -= lift3;
    }

    // `SpiderRenderer.getFlipDegrees`: a dying spider rolls right over.
    float flip_degrees() const override { return 180.0f; }

private:
    int32_t head_ = -1;
    int32_t right_hind_leg_ = -1, left_hind_leg_ = -1;
    int32_t right_middle_hind_leg_ = -1, left_middle_hind_leg_ = -1;
    int32_t right_middle_front_leg_ = -1, left_middle_front_leg_ = -1;
    int32_t right_front_leg_ = -1, left_front_leg_ = -1;
};

// `EndermanModel`: a humanoid stretched thin, with the stare and the block it
// carries on top of it.
class Enderman : public Humanoid {
public:
    explicit Enderman(const Model& model) : Humanoid(model) {}

    // `EndermanModel.setupAnim`: the humanoid walk halved and clamped, the
    // arms raised around a carried block, the head dropped when it is angry.
    void animate(const MobState& s, Pose& pose) const override {
        humanoid_anim(s, pose);
        PartPose& head = pose[head_];
        PartPose& right_arm = pose[right_arm_];
        PartPose& left_arm = pose[left_arm_];
        PartPose& right_leg = pose[right_leg_];
        PartPose& left_leg = pose[left_leg_];
        head.visible = true;
        right_arm.x_rot *= 0.5f;
        left_arm.x_rot *= 0.5f;
        right_leg.x_rot *= 0.5f;
        left_leg.x_rot *= 0.5f;
        right_arm.x_rot = std::clamp(right_arm.x_rot, -0.4f, 0.4f);
        left_arm.x_rot = std::clamp(left_arm.x_rot, -0.4f, 0.4f);
        right_leg.x_rot = std::clamp(right_leg.x_rot, -0.4f, 0.4f);
        left_leg.x_rot = std::clamp(left_leg.x_rot, -0.4f, 0.4f);
        if (s.carries_block) {
            right_arm.x_rot = -0.5f;
            left_arm.x_rot = -0.5f;
            right_arm.z_rot = 0.05f;
            left_arm.z_rot = -0.05f;
        }
        if (s.creepy) {
            head.y -= 5.0f;
            pose[hat_].y += 5.0f;
        }
    }
};

// `SlimeModel`: the cube the slime is, inner and outer, which the game never
// animates and the renderer squashes whole.
class Slime : public MobModel {
public:
    explicit Slime(const Model& model) : MobModel(model) {}

    // `SlimeModel.setupAnim`: nothing of its own.
    void animate(const MobState&, Pose& pose) const override { reset(pose); }

    // `SlimeRenderer.scale`: a hair under one block, squashed by the bounce
    // and grown by the size.
    void scale(const MobState& s, Placement& place) const override {
        place.scale(0.999f, 0.999f, 0.999f);
        place.translate(0.0f, 0.001f, 0.0f);
        const float size = static_cast<float>(s.slime_size);
        const float squish = s.squish / (size * 0.5f + 1.0f);
        const float factor = 1.0f / (squish + 1.0f);
        place.scale(factor * size, 1.0f / factor * size, factor * size);
    }
};

// `MagmaCubeModel`: eight slices that spread apart as the cube squashes.
class MagmaCube : public MobModel {
public:
    explicit MagmaCube(const Model& model) : MobModel(model) {
        for (int i = 0; i < kSegments; ++i) {
            cubes_[static_cast<size_t>(i)] = part("cube" + std::to_string(i));
        }
    }

    // `MagmaCubeModel.setupAnim`: each slice lifted by its distance from the
    // fourth one.
    void animate(const MobState& s, Pose& pose) const override {
        reset(pose);
        const float squish = std::max(0.0f, s.squish);
        for (int i = 0; i < kSegments; ++i) {
            pose[cubes_[static_cast<size_t>(i)]].y = static_cast<float>(-(4 - i)) * squish * 1.7f;
        }
    }

    // `MagmaCubeRenderer.scale`.
    void scale(const MobState& s, Placement& place) const override {
        const float size = static_cast<float>(s.slime_size);
        const float squish = s.squish / (size * 0.5f + 1.0f);
        const float factor = 1.0f / (squish + 1.0f);
        place.scale(factor * size, 1.0f / factor * size, factor * size);
    }

private:
    static constexpr int kSegments = 8;
    std::array<int32_t, kSegments> cubes_{};
};

// `IronGolemModel`: the head, the two long arms and the two legs of an iron
// golem.
class IronGolem : public MobModel {
public:
    explicit IronGolem(const Model& model) : MobModel(model) {
        head_ = part("head");
        right_arm_ = part("right_arm");
        left_arm_ = part("left_arm");
        right_leg_ = part("right_leg");
        left_leg_ = part("left_leg");
    }

    // `IronGolemModel.setupAnim`: the swing, the offered flower or the walk,
    // whichever is going on, and the legs on the same saw.
    void animate(const MobState& s, Pose& pose) const override {
        reset(pose);
        const float attack = s.attack_ticks_remaining;
        const float speed = s.walk_speed;
        const float walked = s.walk_position;
        if (attack > 0.0f) {
            pose[right_arm_].x_rot = -2.0f + 1.5f * triangle_wave(attack, 10.0f);
            pose[left_arm_].x_rot = -2.0f + 1.5f * triangle_wave(attack, 10.0f);
        } else {
            const int32_t flower = s.offer_flower_tick;
            if (flower > 0) {
                pose[right_arm_].x_rot =
                    -0.8f + 0.025f * triangle_wave(static_cast<float>(flower), 70.0f);
                pose[left_arm_].x_rot = 0.0f;
            } else {
                pose[right_arm_].x_rot = (-0.2f + 1.5f * triangle_wave(walked, 13.0f)) * speed;
                pose[left_arm_].x_rot = (-0.2f - 1.5f * triangle_wave(walked, 13.0f)) * speed;
            }
        }
        pose[head_].y_rot = s.y_rot * kDegrees;
        pose[head_].x_rot = s.x_rot * kDegrees;
        pose[right_leg_].x_rot = -1.5f * triangle_wave(walked, 13.0f) * speed;
        pose[left_leg_].x_rot = 1.5f * triangle_wave(walked, 13.0f) * speed;
        pose[right_leg_].y_rot = 0.0f;
        pose[left_leg_].y_rot = 0.0f;
    }

    // `IronGolemRenderer.setupRotations`: the whole body rocks side to side as
    // it walks.
    void setup_rotations(const MobState& s, Placement& place, float body_rot,
                         float scale) const override {
        MobModel::setup_rotations(s, place, body_rot, scale);
        if (!(s.walk_speed < 0.01)) {
            const float shifted = s.walk_position + 6.0f;
            const float wave = (std::fabs(std::fmod(shifted, 13.0f) - 6.5f) - 3.25f) / 3.25f;
            place.rotate(quaternion_z(to_radians(6.5f * wave)));
        }
    }

private:
    int32_t head_ = -1, right_arm_ = -1, left_arm_ = -1, right_leg_ = -1, left_leg_ = -1;
};

// `SnowGolemModel`: the head and the upper body, whose turn carries the two
// stick arms around with it.
class SnowGolem : public MobModel {
public:
    explicit SnowGolem(const Model& model) : MobModel(model) {
        head_ = part("head");
        left_arm_ = part("left_arm");
        right_arm_ = part("right_arm");
        upper_body_ = part("upper_body");
    }

    // `SnowGolemModel.setupAnim`: the arms swing round the upper body on the
    // sine and cosine of its own turn.
    void animate(const MobState& s, Pose& pose) const override {
        reset(pose);
        pose[head_].y_rot = s.y_rot * kDegrees;
        pose[head_].x_rot = s.x_rot * kDegrees;
        pose[upper_body_].y_rot = s.y_rot * kDegrees * 0.25f;
        const float turn = pose[upper_body_].y_rot;
        const float sin = jmath::sin(turn);
        const float cos = jmath::cos(turn);
        pose[left_arm_].y_rot = turn;
        pose[right_arm_].y_rot = turn + kPi;
        pose[left_arm_].x = cos * 5.0f;
        pose[left_arm_].z = -sin * 5.0f;
        pose[right_arm_].x = -cos * 5.0f;
        pose[right_arm_].z = sin * 5.0f;
    }

private:
    int32_t head_ = -1, left_arm_ = -1, right_arm_ = -1, upper_body_ = -1;
};

// `BeeModel`: the body hung off a bone, with wings, antennae, three pairs of
// legs and the stinger it loses when it stings.
class Bee : public MobModel {
public:
    explicit Bee(const Model& model) : MobModel(model) {
        bone_ = part("bone");
        stinger_ = part("bone/body/stinger");
        left_antenna_ = part("bone/body/left_antenna");
        right_antenna_ = part("bone/body/right_antenna");
        right_wing_ = part("bone/right_wing");
        left_wing_ = part("bone/left_wing");
        front_leg_ = part("bone/front_legs");
        mid_leg_ = part("bone/middle_legs");
        back_leg_ = part("bone/back_legs");
    }

    // `BeeModel.setupAnim`: the wing beat and the tucked legs while it flies,
    // the idle hover when it is calm, and the roll onto its back.
    void animate(const MobState& s, Pose& pose) const override {
        reset(pose);
        const float roll = s.roll_amount;
        pose[stinger_].visible = s.has_stinger;
        if (!s.on_ground) {
            const float beat = s.age_in_ticks * 120.32113f * kDegrees;
            pose[right_wing_].y_rot = 0.0f;
            pose[right_wing_].z_rot = jmath::cos(beat) * kPi * 0.15f;
            pose[left_wing_].x_rot = pose[right_wing_].x_rot;
            pose[left_wing_].y_rot = pose[right_wing_].y_rot;
            pose[left_wing_].z_rot = -pose[right_wing_].z_rot;
            pose[front_leg_].x_rot = static_cast<float>(M_PI / 4);
            pose[mid_leg_].x_rot = static_cast<float>(M_PI / 4);
            pose[back_leg_].x_rot = static_cast<float>(M_PI / 4);
        }

        if (!s.angry && !s.on_ground) {
            const float hover = jmath::cos(s.age_in_ticks * 0.18f);
            pose[bone_].x_rot = 0.1f + hover * kPi * 0.025f;
            pose[left_antenna_].x_rot = hover * kPi * 0.03f;
            pose[right_antenna_].x_rot = hover * kPi * 0.03f;
            pose[front_leg_].x_rot = -hover * kPi * 0.1f + static_cast<float>(M_PI / 8);
            pose[back_leg_].x_rot = -hover * kPi * 0.05f + static_cast<float>(M_PI / 4);
            pose[bone_].y = pose[bone_].y - jmath::cos(s.age_in_ticks * 0.18f) * 0.9f;
        }

        if (roll > 0.0f) {
            pose[bone_].x_rot = rot_lerp_rad(roll, pose[bone_].x_rot, 3.0915928f);
        }
    }

private:
    int32_t bone_ = -1, stinger_ = -1, left_antenna_ = -1, right_antenna_ = -1;
    int32_t right_wing_ = -1, left_wing_ = -1;
    int32_t front_leg_ = -1, mid_leg_ = -1, back_leg_ = -1;
};

// `BeeStingerModel`: the two crossed quads of a stinger stuck in something.
class BeeStinger : public MobModel {
public:
    explicit BeeStinger(const Model& model) : MobModel(model) {}

    // `Model.setupAnim`: the stinger has no animation of its own.
    void animate(const MobState&, Pose& pose) const override { reset(pose); }
};

// `AxolotlModel`: four states blended on top of each other - swimming,
// hovering in water, crawling on the ground, lying still - and the play-dead
// pose, with the right legs mirrored from the left.
class Axolotl : public MobModel {
public:
    explicit Axolotl(const Model& model) : MobModel(model) {
        body_ = part("body");
        head_ = part("body/head");
        right_hind_leg_ = part("body/right_hind_leg");
        left_hind_leg_ = part("body/left_hind_leg");
        right_front_leg_ = part("body/right_front_leg");
        left_front_leg_ = part("body/left_front_leg");
        tail_ = part("body/tail");
        top_gills_ = part("body/head/top_gills");
        left_gills_ = part("body/head/left_gills");
        right_gills_ = part("body/head/right_gills");
    }

    // `AxolotlModel.setupAnim`: the four blends, then play dead, then the
    // mirror.
    void animate(const MobState& s, Pose& pose) const override {
        reset(pose);
        const float dead = s.playing_dead_factor;
        const float in_water = s.in_water_factor;
        const float on_ground = s.on_ground_factor;
        const float moving = s.moving_factor;
        const float still = 1.0f - moving;
        const float mirror = 1.0f - std::min(on_ground, moving);
        pose[body_].y_rot = pose[body_].y_rot + s.y_rot * kDegrees;
        swimming(pose, s.age_in_ticks, s.x_rot, std::min(moving, in_water));
        water_hovering(pose, s.age_in_ticks, std::min(still, in_water));
        ground_crawling(pose, s.age_in_ticks, std::min(moving, on_ground));
        lay_still_on_ground(pose, s.age_in_ticks, std::min(still, on_ground));
        play_dead(pose, dead);
        mirror_legs(pose, mirror);
    }

private:
    // `AxolotlModel.setupLayStillOnGroundAnimation`.
    void lay_still_on_ground(Pose& pose, float age, float weight) const {
        if (!(weight <= 1.0e-5f)) {
            const float t = age * 0.09f;
            const float sin = jmath::sin(t);
            const float cos = jmath::cos(t);
            const float wobble = sin * sin - 2.0f * sin;
            const float gill_wobble = cos * cos - 3.0f * sin;
            pose[head_].x_rot += -0.09f * wobble * weight;
            pose[head_].z_rot += -0.2f * weight;
            pose[tail_].y_rot += (-0.1f + 0.1f * wobble) * weight;
            const float gills = (0.6f + 0.05f * gill_wobble) * weight;
            pose[top_gills_].x_rot += gills;
            pose[left_gills_].y_rot -= gills;
            pose[right_gills_].y_rot += gills;
            pose[left_hind_leg_].x_rot += 1.1f * weight;
            pose[left_hind_leg_].y_rot += 1.0f * weight;
            pose[left_front_leg_].x_rot += 0.8f * weight;
            pose[left_front_leg_].y_rot += 2.3f * weight;
            pose[left_front_leg_].z_rot -= 0.5f * weight;
        }
    }

    // `AxolotlModel.setupGroundCrawlingAnimation`.
    void ground_crawling(Pose& pose, float age, float weight) const {
        if (!(weight <= 1.0e-5f)) {
            const float t = age * 0.11f;
            const float cos = jmath::cos(t);
            const float splay = (cos * cos - 2.0f * cos) / 5.0f;
            const float reach = 0.7f * cos;
            const float sway = 0.09f * cos * weight;
            pose[head_].y_rot += sway;
            pose[tail_].y_rot += sway;
            const float gills = (0.6f - 0.08f * (cos * cos + 2.0f * jmath::sin(t))) * weight;
            pose[top_gills_].x_rot += gills;
            pose[left_gills_].y_rot -= gills;
            pose[right_gills_].y_rot += gills;
            const float hind = 0.9424779f * weight;
            const float front = 1.0995574f * weight;
            pose[left_hind_leg_].x_rot += hind;
            pose[left_hind_leg_].y_rot += (1.5f - splay) * weight;
            pose[left_hind_leg_].z_rot += -0.1f * weight;
            pose[left_front_leg_].x_rot += front;
            pose[left_front_leg_].y_rot += (static_cast<float>(M_PI / 2) - reach) * weight;
            pose[right_hind_leg_].x_rot += hind;
            pose[right_hind_leg_].y_rot += (-1.0f - splay) * weight;
            pose[right_front_leg_].x_rot += front;
            pose[right_front_leg_].y_rot += (static_cast<float>(-M_PI / 2) - reach) * weight;
        }
    }

    // `AxolotlModel.setupWaterHoveringAnimation`.
    void water_hovering(Pose& pose, float age, float weight) const {
        if (!(weight <= 1.0e-5f)) {
            const float t = age * 0.075f;
            const float cos = jmath::cos(t);
            const float rise = jmath::sin(t) * 0.15f;
            const float tilt = (-0.15f + 0.075f * cos) * weight;
            pose[body_].x_rot += tilt;
            pose[body_].y -= rise * weight;
            pose[head_].x_rot -= tilt;
            pose[top_gills_].x_rot += 0.2f * cos * weight;
            const float gills = (-0.3f * cos - 0.19f) * weight;
            pose[left_gills_].y_rot += gills;
            pose[right_gills_].y_rot -= gills;
            pose[left_hind_leg_].x_rot +=
                (static_cast<float>(M_PI * 3.0 / 4.0) - cos * 0.11f) * weight;
            pose[left_hind_leg_].y_rot += 0.47123894f * weight;
            pose[left_hind_leg_].z_rot += 1.7278761f * weight;
            pose[left_front_leg_].x_rot +=
                (static_cast<float>(M_PI / 4) - cos * 0.2f) * weight;
            pose[left_front_leg_].y_rot += 2.042035f * weight;
            pose[tail_].y_rot += 0.5f * cos * weight;
        }
    }

    // `AxolotlModel.setupSwimmingAnimation`.
    void swimming(Pose& pose, float age, float pitch, float weight) const {
        if (!(weight <= 1.0e-5f)) {
            const float t = age * 0.33f;
            const float sin = jmath::sin(t);
            const float cos = jmath::cos(t);
            const float undulate = 0.13f * sin;
            pose[body_].x_rot += (pitch * kDegrees + undulate) * weight;
            pose[head_].x_rot -= undulate * 1.8f * weight;
            pose[body_].y -= 0.45f * cos * weight;
            pose[top_gills_].x_rot += (-0.5f * sin - 0.8f) * weight;
            const float gills = (0.3f * sin + 0.9f) * weight;
            pose[left_gills_].y_rot += gills;
            pose[right_gills_].y_rot -= gills;
            pose[tail_].y_rot = pose[tail_].y_rot + 0.3f * jmath::cos(t * 0.9f) * weight;
            pose[left_hind_leg_].x_rot += 1.8849558f * weight;
            pose[left_hind_leg_].y_rot += -0.4f * sin * weight;
            pose[left_hind_leg_].z_rot += static_cast<float>(M_PI / 2) * weight;
            pose[left_front_leg_].x_rot += 1.8849558f * weight;
            pose[left_front_leg_].y_rot += (-0.2f * cos - 0.1f) * weight;
            pose[left_front_leg_].z_rot += static_cast<float>(M_PI / 2) * weight;
        }
    }

    // `AxolotlModel.setupPlayDeadAnimation`.
    void play_dead(Pose& pose, float weight) const {
        if (!(weight <= 1.0e-5f)) {
            pose[left_hind_leg_].x_rot += 1.4137167f * weight;
            pose[left_hind_leg_].y_rot += 1.0995574f * weight;
            pose[left_hind_leg_].z_rot += static_cast<float>(M_PI / 4) * weight;
            pose[left_front_leg_].x_rot += static_cast<float>(M_PI / 4) * weight;
            pose[left_front_leg_].y_rot += 2.042035f * weight;
            pose[body_].x_rot += -0.15f * weight;
            pose[body_].z_rot += 0.35f * weight;
        }
    }

    // `AxolotlModel.applyMirrorLegRotations`.
    void mirror_legs(Pose& pose, float weight) const {
        if (!(weight <= 1.0e-5f)) {
            pose[right_hind_leg_].x_rot =
                pose[right_hind_leg_].x_rot + pose[left_hind_leg_].x_rot * weight;
            pose[right_hind_leg_].y_rot =
                pose[right_hind_leg_].y_rot + -pose[left_hind_leg_].y_rot * weight;
            pose[right_hind_leg_].z_rot =
                pose[right_hind_leg_].z_rot + -pose[left_hind_leg_].z_rot * weight;
            pose[right_front_leg_].x_rot =
                pose[right_front_leg_].x_rot + pose[left_front_leg_].x_rot * weight;
            pose[right_front_leg_].y_rot =
                pose[right_front_leg_].y_rot + -pose[left_front_leg_].y_rot * weight;
            pose[right_front_leg_].z_rot =
                pose[right_front_leg_].z_rot + -pose[left_front_leg_].z_rot * weight;
        }
    }

    int32_t body_ = -1, head_ = -1, tail_ = -1;
    int32_t right_hind_leg_ = -1, left_hind_leg_ = -1;
    int32_t right_front_leg_ = -1, left_front_leg_ = -1;
    int32_t top_gills_ = -1, left_gills_ = -1, right_gills_ = -1;
};

// `AllayModel`: the head, the body with its two arms and two wings, all hung
// off the layer's own `root` part, which the dance turns.
class Allay : public MobModel {
public:
    explicit Allay(const Model& model) : MobModel(model) {
        root_ = part("root");
        body_ = part("root/body");
        head_ = part("root/head");
        right_arm_ = part("root/body/right_arm");
        left_arm_ = part("root/body/left_arm");
        right_wing_ = part("root/body/right_wing");
        left_wing_ = part("root/body/left_wing");
        hand_chain_ = {root_, body_};
    }

    // `AllayModel.setupAnim`: the dance or the look, the wing beat, the body's
    // forward lean and the arms around whatever it carries.
    void animate(const MobState& s, Pose& pose) const override {
        reset(pose);
        const float speed = s.walk_speed;
        const float walked = s.walk_position;
        const float beat_phase = s.age_in_ticks * 20.0f * kDegrees + walked;
        const float beat = jmath::cos(beat_phase) * kPi * 0.15f + speed;
        const float hover = s.age_in_ticks * 9.0f * kDegrees;
        const float flying = std::min(speed / 0.3f, 1.0f);
        const float idle = 1.0f - flying;
        const float holding = s.holding_animation_progress;
        if (s.dancing) {
            const float dance = s.age_in_ticks * 8.0f * kDegrees + speed;
            const float lean = jmath::cos(dance) * 16.0f * kDegrees;
            const float spin = s.spinning_progress;
            const float head_tilt = jmath::cos(dance) * 14.0f * kDegrees;
            const float head_turn = jmath::cos(dance) * 30.0f * kDegrees;
            pose[root_].y_rot =
                s.spinning ? static_cast<float>(M_PI * 4) * spin : pose[root_].y_rot;
            pose[root_].z_rot = lean * (1.0f - spin);
            pose[head_].y_rot = head_turn * (1.0f - spin);
            pose[head_].z_rot = head_tilt * (1.0f - spin);
        } else {
            pose[head_].x_rot = s.x_rot * kDegrees;
            pose[head_].y_rot = s.y_rot * kDegrees;
        }

        pose[right_wing_].x_rot = 0.43633232f * (1.0f - flying);
        pose[right_wing_].y_rot = static_cast<float>(-M_PI / 4) + beat;
        pose[left_wing_].x_rot = 0.43633232f * (1.0f - flying);
        pose[left_wing_].y_rot = static_cast<float>(M_PI / 4) - beat;
        pose[body_].x_rot = flying * static_cast<float>(M_PI / 4);
        const float arm_lift =
            holding * lerp(flying, static_cast<float>(-M_PI / 3), -1.134464f);
        pose[root_].y = pose[root_].y + static_cast<float>(std::cos(hover)) * 0.25f * idle;
        pose[right_arm_].x_rot = arm_lift;
        pose[left_arm_].x_rot = arm_lift;
        const float sway = idle * (1.0f - holding);
        const float spread =
            0.43633232f -
            jmath::cos(hover + static_cast<float>(M_PI * 3.0 / 2.0)) * kPi * 0.075f * sway;
        pose[left_arm_].z_rot = -spread;
        pose[right_arm_].z_rot = spread;
        pose[right_arm_].y_rot = 0.27925268f * holding;
        pose[left_arm_].y_rot = -0.27925268f * holding;
    }

    // `AllayModel.translateToHand`: the chain only; the fixed translate, the
    // turn by the right arm's pitch and the 0.7 shrink the game puts on top of
    // it have nowhere to go in a part chain.
    std::span<const int32_t> hand_chain(bool) const override { return hand_chain_; }

private:
    int32_t root_ = -1, head_ = -1, body_ = -1;
    int32_t right_arm_ = -1, left_arm_ = -1, right_wing_ = -1, left_wing_ = -1;
    std::array<int32_t, 2> hand_chain_{};
};

constexpr std::array<MobOverlay, 1> kCreeperOverlays = {{
    {"creeper#armor", "creeper/creeper_armor", Overlay::EnergySwirl,
     gpu::EntityRenderer::Type::Translucent},
}};

constexpr std::array<MobOverlay, 1> kSpiderOverlays = {{
    {"", "spider_eyes", Overlay::Eyes, gpu::EntityRenderer::Type::Translucent},
}};

constexpr std::array<MobOverlay, 1> kEndermanOverlays = {{
    {"", "enderman/enderman_eyes", Overlay::Eyes, gpu::EntityRenderer::Type::Translucent},
}};

constexpr std::array<MobOverlay, 1> kSlimeOverlays = {{
    {"slime#outer", "slime/slime", Overlay::Plain, gpu::EntityRenderer::Type::Translucent},
}};

} // namespace

std::unique_ptr<MobModel> make_monster_model(std::string_view layer, const Model& model) {
    if (layer == "creeper" || layer == "creeper#armor") return std::make_unique<Creeper>(model);
    if (layer == "spider" || layer == "cave_spider") return std::make_unique<Spider>(model);
    if (layer == "enderman") return std::make_unique<Enderman>(model);
    if (layer == "slime" || layer == "slime#outer") return std::make_unique<Slime>(model);
    if (layer == "magma_cube") return std::make_unique<MagmaCube>(model);
    if (layer == "iron_golem") return std::make_unique<IronGolem>(model);
    if (layer == "snow_golem") return std::make_unique<SnowGolem>(model);
    if (layer == "bee" || layer == "bee_baby") return std::make_unique<Bee>(model);
    if (layer == "bee_stinger") return std::make_unique<BeeStinger>(model);
    if (layer == "axolotl" || layer == "axolotl_baby") return std::make_unique<Axolotl>(model);
    if (layer == "allay") return std::make_unique<Allay>(model);
    return nullptr;
}

std::span<const MobType> monster_mob_types() {
    static constexpr std::array<MobType, 11> kTypes = {{
        {"minecraft:creeper", "creeper", "creeper/creeper", "", 0.5f,
         gpu::EntityRenderer::Type::CutoutNoCull, false, false, kCreeperOverlays},
        {"minecraft:spider", "spider", "spider/spider", "", 0.8f,
         gpu::EntityRenderer::Type::CutoutNoCull, false, false, kSpiderOverlays},
        {"minecraft:cave_spider", "cave_spider", "spider/cave_spider", "", 0.56f,
         gpu::EntityRenderer::Type::CutoutNoCull, false, false, kSpiderOverlays},
        {"minecraft:enderman", "enderman", "enderman/enderman", "", 0.5f,
         gpu::EntityRenderer::Type::CutoutNoCull, false, false, kEndermanOverlays},
        {"minecraft:slime", "slime", "slime/slime", "", 0.25f,
         gpu::EntityRenderer::Type::CutoutNoCull, false, false, kSlimeOverlays},
        {"minecraft:magma_cube", "magma_cube", "slime/magmacube", "", 0.25f,
         gpu::EntityRenderer::Type::CutoutNoCull, false, false, {}},
        {"minecraft:iron_golem", "iron_golem", "iron_golem/iron_golem", "", 0.7f,
         gpu::EntityRenderer::Type::CutoutNoCull, false, false, {}},
        {"minecraft:snow_golem", "snow_golem", "snow_golem", "", 0.5f,
         gpu::EntityRenderer::Type::CutoutNoCull, false, false, {}},
        {"minecraft:bee", "bee", "bee/bee", "bee_baby", 0.4f,
         gpu::EntityRenderer::Type::CutoutNoCull, false, false, {}},
        {"minecraft:axolotl", "axolotl", "axolotl/axolotl_lucy", "axolotl_baby", 0.5f,
         gpu::EntityRenderer::Type::CutoutNoCull, false, false, {}},
        {"minecraft:allay", "allay", "allay/allay", "", 0.4f,
         gpu::EntityRenderer::Type::Translucent, false, true, {}},
    }};
    return kTypes;
}

} // namespace mcpp::entity
