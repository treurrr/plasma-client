#include "mcpp/entity/mobs.hpp"

#include "mcpp/entity/motion.hpp"
#include "mcpp/jmath.hpp"
#include "mcpp/entity/dye.hpp"
#include "mob_factory.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace mcpp::entity {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegrees = static_cast<float>(M_PI / 180.0);
constexpr float kHalfPi = static_cast<float>(M_PI / 2);
constexpr float kQuarterPi = static_cast<float>(M_PI / 4);

// `QuadrupedModel`: the head, the barrel and the four legs every farm animal
// is built from.
class Quadruped : public MobModel {
public:
    explicit Quadruped(const Model& model)
        : MobModel(model), head_(part("head")), body_(part("body")),
          right_hind_leg_(part("right_hind_leg")), left_hind_leg_(part("left_hind_leg")),
          right_front_leg_(part("right_front_leg")), left_front_leg_(part("left_front_leg")) {}

    void animate(const MobState& state, Pose& pose) const override;

protected:
    int32_t head_, body_;
    int32_t right_hind_leg_, left_hind_leg_, right_front_leg_, left_front_leg_;
};

// `QuadrupedModel.setupAnim`: the head follows the look, the legs the walk.
void Quadruped::animate(const MobState& state, Pose& pose) const {
    reset(pose);
    pose[head_].x_rot = state.x_rot * kDegrees;
    pose[head_].y_rot = state.y_rot * kDegrees;
    const float position = state.walk_position;
    const float speed = state.walk_speed;
    pose[right_hind_leg_].x_rot = jmath::cos(position * 0.6662f) * 1.4f * speed;
    pose[left_hind_leg_].x_rot = jmath::cos(position * 0.6662f + kPi) * 1.4f * speed;
    pose[right_front_leg_].x_rot = jmath::cos(position * 0.6662f + kPi) * 1.4f * speed;
    pose[left_front_leg_].x_rot = jmath::cos(position * 0.6662f) * 1.4f * speed;
}

// `SheepModel`, `SheepFurModel`: the sheep's body and the wool over it, which
// animate alike.
class Sheep : public Quadruped {
public:
    using Quadruped::Quadruped;
    void animate(const MobState& state, Pose& pose) const override;
};

// `SheepModel.setupAnim`, `SheepFurModel.setupAnim`: the head dips to the grass.
void Sheep::animate(const MobState& state, Pose& pose) const {
    Quadruped::animate(state, pose);
    pose[head_].y = pose[head_].y + state.head_eat_position_scale * 9.0f * state.age_scale;
    pose[head_].x_rot = state.head_eat_angle_scale;
}

// `ChickenModel`: two legs and two flapping wings.
class Chicken : public MobModel {
public:
    explicit Chicken(const Model& model)
        : MobModel(model), head_(part("head")), right_leg_(part("right_leg")),
          left_leg_(part("left_leg")), right_wing_(part("right_wing")),
          left_wing_(part("left_wing")) {}

    void animate(const MobState& state, Pose& pose) const override;

private:
    int32_t head_, right_leg_, left_leg_, right_wing_, left_wing_;
};

// `ChickenModel.setupAnim`: the wings beat with the fall, the legs with the walk.
void Chicken::animate(const MobState& state, Pose& pose) const {
    reset(pose);
    const float wing = (jmath::sin(state.flap) + 1.0f) * state.flap_speed;
    pose[head_].x_rot = state.x_rot * kDegrees;
    pose[head_].y_rot = state.y_rot * kDegrees;
    const float speed = state.walk_speed;
    const float position = state.walk_position;
    pose[right_leg_].x_rot = jmath::cos(position * 0.6662f) * 1.4f * speed;
    pose[left_leg_].x_rot = jmath::cos(position * 0.6662f + kPi) * 1.4f * speed;
    pose[right_wing_].z_rot = wing;
    pose[left_wing_].z_rot = -wing;
}

// `WolfRenderState.getBodyRollAngle`: how far the shake has twisted the body
// at this distance along it.
float wolf_body_roll_angle(const MobState& state, float offset) {
    float progress = (state.shake_anim + offset) / 1.8f;
    if (progress < 0.0f) {
        progress = 0.0f;
    } else if (progress > 1.0f) {
        progress = 1.0f;
    }
    return jmath::sin(progress * kPi) * jmath::sin(progress * kPi * 11.0f) * 0.15f * kPi;
}

// `WolfModel`: the wolf, which sits, shakes itself dry and wags.
class Wolf : public MobModel {
public:
    explicit Wolf(const Model& model)
        : MobModel(model), head_(part("head")), real_head_(part("head/real_head")),
          body_(part("body")), upper_body_(part("upper_body")),
          right_hind_leg_(part("right_hind_leg")), left_hind_leg_(part("left_hind_leg")),
          right_front_leg_(part("right_front_leg")), left_front_leg_(part("left_front_leg")),
          tail_(part("tail")), real_tail_(part("tail/real_tail")) {}

    void animate(const MobState& state, Pose& pose) const override;
    uint32_t tint(const MobState& state) const override;

private:
    int32_t head_, real_head_, body_, upper_body_;
    int32_t right_hind_leg_, left_hind_leg_, right_front_leg_, left_front_leg_;
    int32_t tail_, real_tail_;
};

// `WolfModel.setupAnim`: the sit, the walk and the shake down the spine.
void Wolf::animate(const MobState& state, Pose& pose) const {
    reset(pose);
    const float position = state.walk_position;
    const float speed = state.walk_speed;
    if (state.angry) {
        pose[tail_].y_rot = 0.0f;
    } else {
        pose[tail_].y_rot = jmath::cos(position * 0.6662f) * 1.4f * speed;
    }

    if (state.sitting) {
        const float age_scale = state.age_scale;
        pose[upper_body_].y += 2.0f * age_scale;
        pose[upper_body_].x_rot = static_cast<float>(M_PI * 2.0 / 5.0);
        pose[upper_body_].y_rot = 0.0f;
        pose[body_].y += 4.0f * age_scale;
        pose[body_].z -= 2.0f * age_scale;
        pose[body_].x_rot = kQuarterPi;
        pose[tail_].y += 9.0f * age_scale;
        pose[tail_].z -= 2.0f * age_scale;
        pose[right_hind_leg_].y += 6.7f * age_scale;
        pose[right_hind_leg_].z -= 5.0f * age_scale;
        pose[right_hind_leg_].x_rot = static_cast<float>(M_PI * 3.0 / 2.0);
        pose[left_hind_leg_].y += 6.7f * age_scale;
        pose[left_hind_leg_].z -= 5.0f * age_scale;
        pose[left_hind_leg_].x_rot = static_cast<float>(M_PI * 3.0 / 2.0);
        pose[right_front_leg_].x_rot = 5.811947f;
        pose[right_front_leg_].x += 0.01f * age_scale;
        pose[right_front_leg_].y += 1.0f * age_scale;
        pose[left_front_leg_].x_rot = 5.811947f;
        pose[left_front_leg_].x -= 0.01f * age_scale;
        pose[left_front_leg_].y += 1.0f * age_scale;
    } else {
        pose[right_hind_leg_].x_rot = jmath::cos(position * 0.6662f) * 1.4f * speed;
        pose[left_hind_leg_].x_rot = jmath::cos(position * 0.6662f + kPi) * 1.4f * speed;
        pose[right_front_leg_].x_rot = jmath::cos(position * 0.6662f + kPi) * 1.4f * speed;
        pose[left_front_leg_].x_rot = jmath::cos(position * 0.6662f) * 1.4f * speed;
    }

    pose[real_head_].z_rot = state.head_roll_angle + wolf_body_roll_angle(state, 0.0f);
    pose[upper_body_].z_rot = wolf_body_roll_angle(state, -0.08f);
    pose[body_].z_rot = wolf_body_roll_angle(state, -0.16f);
    pose[real_tail_].z_rot = wolf_body_roll_angle(state, -0.2f);
    pose[head_].x_rot = state.x_rot * kDegrees;
    pose[head_].y_rot = state.y_rot * kDegrees;
    pose[tail_].x_rot = state.tail_angle;
}

// `ARGB.as8BitChannel`.
uint32_t as_8bit_channel(float value) {
    return static_cast<uint32_t>(static_cast<int32_t>(std::floor(value * 255.0f))) & 0xFFu;
}

// `WolfRenderer.getModelTint`: a wet wolf is drawn darker.
uint32_t Wolf::tint(const MobState& state) const {
    const float shade = state.wet_shade;
    if (shade == 1.0f) return 0xFFFFFFFFu;
    const uint32_t channel = as_8bit_channel(shade);
    return 0xFF000000u | (channel << 16) | (channel << 8) | channel;
}

// `FelineModel`: the cat and the ocelot, which crouch, sprint, sit and lie down.
class Feline : public MobModel {
public:
    explicit Feline(const Model& model)
        : MobModel(model), head_(part("head")), body_(part("body")), tail1_(part("tail1")),
          tail2_(part("tail2")), left_hind_leg_(part("left_hind_leg")),
          right_hind_leg_(part("right_hind_leg")), left_front_leg_(part("left_front_leg")),
          right_front_leg_(part("right_front_leg")) {}

    void animate(const MobState& state, Pose& pose) const override;

protected:
    int32_t head_, body_, tail1_, tail2_;
    int32_t left_hind_leg_, right_hind_leg_, left_front_leg_, right_front_leg_;
};

// `FelineModel.setupAnim`: the crouch, the sprint, the sit and the nap.
void Feline::animate(const MobState& state, Pose& pose) const {
    reset(pose);
    const float age_scale = state.age_scale;
    if (state.crouching) {
        pose[body_].y += 1.0f * age_scale;
        pose[head_].y += 2.0f * age_scale;
        pose[tail1_].y += 1.0f * age_scale;
        pose[tail2_].y += -4.0f * age_scale;
        pose[tail2_].z += 2.0f * age_scale;
        pose[tail1_].x_rot = kHalfPi;
        pose[tail2_].x_rot = kHalfPi;
    } else if (state.sprinting) {
        pose[tail2_].y = pose[tail1_].y;
        pose[tail2_].z += 2.0f * age_scale;
        pose[tail1_].x_rot = kHalfPi;
        pose[tail2_].x_rot = kHalfPi;
    }

    pose[head_].x_rot = state.x_rot * kDegrees;
    pose[head_].y_rot = state.y_rot * kDegrees;
    if (!state.sitting) {
        pose[body_].x_rot = kHalfPi;
        const float speed = state.walk_speed;
        const float position = state.walk_position;
        if (state.sprinting) {
            pose[left_hind_leg_].x_rot = jmath::cos(position * 0.6662f) * speed;
            pose[right_hind_leg_].x_rot = jmath::cos(position * 0.6662f + 0.3f) * speed;
            pose[left_front_leg_].x_rot = jmath::cos(position * 0.6662f + kPi + 0.3f) * speed;
            pose[right_front_leg_].x_rot = jmath::cos(position * 0.6662f + kPi) * speed;
            pose[tail2_].x_rot = 1.7278761f + static_cast<float>(M_PI / 10) *
                                                  jmath::cos(position) * speed;
        } else {
            pose[left_hind_leg_].x_rot = jmath::cos(position * 0.6662f) * speed;
            pose[right_hind_leg_].x_rot = jmath::cos(position * 0.6662f + kPi) * speed;
            pose[left_front_leg_].x_rot = jmath::cos(position * 0.6662f + kPi) * speed;
            pose[right_front_leg_].x_rot = jmath::cos(position * 0.6662f) * speed;
            if (!state.crouching) {
                pose[tail2_].x_rot = 1.7278761f + kQuarterPi * jmath::cos(position) * speed;
            } else {
                pose[tail2_].x_rot = 1.7278761f + 0.47123894f * jmath::cos(position) * speed;
            }
        }
    }

    if (state.sitting) {
        pose[body_].x_rot = kQuarterPi;
        pose[body_].y += -4.0f * age_scale;
        pose[body_].z += 5.0f * age_scale;
        pose[head_].y += -3.3f * age_scale;
        pose[head_].z += 1.0f * age_scale;
        pose[tail1_].y += 8.0f * age_scale;
        pose[tail1_].z += -2.0f * age_scale;
        pose[tail2_].y += 2.0f * age_scale;
        pose[tail2_].z += -0.8f * age_scale;
        pose[tail1_].x_rot = 1.7278761f;
        pose[tail2_].x_rot = 2.670354f;
        pose[left_front_leg_].x_rot = static_cast<float>(-M_PI / 20);
        pose[left_front_leg_].y += 2.0f * age_scale;
        pose[left_front_leg_].z -= 2.0f * age_scale;
        pose[right_front_leg_].x_rot = static_cast<float>(-M_PI / 20);
        pose[right_front_leg_].y += 2.0f * age_scale;
        pose[right_front_leg_].z -= 2.0f * age_scale;
        pose[left_hind_leg_].x_rot = -kHalfPi;
        pose[left_hind_leg_].y += 3.0f * age_scale;
        pose[left_hind_leg_].z -= 4.0f * age_scale;
        pose[right_hind_leg_].x_rot = -kHalfPi;
        pose[right_hind_leg_].y += 3.0f * age_scale;
        pose[right_hind_leg_].z -= 4.0f * age_scale;
    }

    if (state.lie_down_amount > 0.0f) {
        pose[head_].z_rot = rot_lerp(state.lie_down_amount, pose[head_].z_rot, -1.2707963f);
        pose[head_].y_rot = rot_lerp(state.lie_down_amount, pose[head_].y_rot, 1.2707963f);
        pose[left_front_leg_].x_rot = -1.2707963f;
        pose[right_front_leg_].x_rot = -0.47079635f;
        pose[right_front_leg_].z_rot = -0.2f;
        pose[right_front_leg_].x += age_scale;
        pose[left_hind_leg_].x_rot = -0.4f;
        pose[right_hind_leg_].x_rot = 0.5f;
        pose[right_hind_leg_].z_rot = -0.5f;
        pose[right_hind_leg_].x += 0.8f * age_scale;
        pose[right_hind_leg_].y += 2.0f * age_scale;
        pose[tail1_].x_rot = rot_lerp(state.lie_down_amount_tail, pose[tail1_].x_rot, 0.8f);
        pose[tail2_].x_rot = rot_lerp(state.lie_down_amount_tail, pose[tail2_].x_rot, -0.4f);
    }

    if (state.relax_state_one_amount > 0.0f) {
        pose[head_].x_rot = rot_lerp(state.relax_state_one_amount, pose[head_].x_rot, -0.58177644f);
    }
}

// `CatModel`, which only the cat's renderer adds anything to: the roll onto
// its side as it lies down.
class Cat : public Feline {
public:
    using Feline::Feline;
    void setup_rotations(const MobState& state, Placement& place, float body_rot,
                         float scale) const override;
};

// `CatRenderer.setupRotations`: a lying cat tips over and shifts off the pillow.
void Cat::setup_rotations(const MobState& state, Placement& place, float body_rot,
                          float scale) const {
    MobModel::setup_rotations(state, place, body_rot, scale);
    const float lie = state.lie_down_amount;
    if (lie > 0.0f) {
        place.translate(0.4f * lie, 0.15f * lie, 0.1f * lie);
        place.rotate(quaternion_z(to_radians(rot_lerp(lie, 0.0f, 90.0f))));
        if (state.lying_on_top_of_sleeping_player) {
            place.translate(0.15f * lie, 0.0f, 0.0f);
        }
    }
}

// `FoxModel`: the fox, which stalks, sleeps on its side and faceplants.
class Fox : public MobModel {
public:
    explicit Fox(const Model& model)
        : MobModel(model), head_(part("head")), body_(part("body")),
          right_hind_leg_(part("right_hind_leg")), left_hind_leg_(part("left_hind_leg")),
          right_front_leg_(part("right_front_leg")), left_front_leg_(part("left_front_leg")),
          tail_(part("body/tail")) {}

    void animate(const MobState& state, Pose& pose) const override;
    void setup_rotations(const MobState& state, Placement& place, float body_rot,
                         float scale) const override;

private:
    int32_t head_, body_;
    int32_t right_hind_leg_, left_hind_leg_, right_front_leg_, left_front_leg_;
    int32_t tail_;
    // `FoxModel.legMotionPos`: the scrabble of a faceplanted fox, which vanilla
    // also keeps on the one model every fox is drawn with.
    mutable float leg_motion_pos_ = 0.0f;
};

// `FoxModel.setupAnim`: the stalk, the nap, the sit and the scrabble.
void Fox::animate(const MobState& state, Pose& pose) const {
    reset(pose);
    const float speed = state.walk_speed;
    const float position = state.walk_position;
    pose[right_hind_leg_].x_rot = jmath::cos(position * 0.6662f) * 1.4f * speed;
    pose[left_hind_leg_].x_rot = jmath::cos(position * 0.6662f + kPi) * 1.4f * speed;
    pose[right_front_leg_].x_rot = jmath::cos(position * 0.6662f + kPi) * 1.4f * speed;
    pose[left_front_leg_].x_rot = jmath::cos(position * 0.6662f) * 1.4f * speed;
    pose[head_].z_rot = state.head_roll_angle;
    pose[right_hind_leg_].visible = true;
    pose[left_hind_leg_].visible = true;
    pose[right_front_leg_].visible = true;
    pose[left_front_leg_].visible = true;
    const float age_scale = state.age_scale;
    if (state.crouching) {
        pose[body_].x_rot += 0.10471976f;
        const float crouch = state.crouch_amount;
        pose[body_].y += crouch * age_scale;
        pose[head_].y += crouch * age_scale;
    } else if (state.sleeping) {
        pose[body_].z_rot = -kHalfPi;
        pose[body_].y += 5.0f * age_scale;
        pose[tail_].x_rot = static_cast<float>(-M_PI * 5.0 / 6.0);
        if (state.baby) {
            pose[tail_].x_rot = -2.1816616f;
            pose[body_].z += 2.0f;
        }

        pose[head_].x += 2.0f * age_scale;
        pose[head_].y += 2.99f * age_scale;
        pose[head_].y_rot = static_cast<float>(-M_PI * 2.0 / 3.0);
        pose[head_].z_rot = 0.0f;
        pose[right_hind_leg_].visible = false;
        pose[left_hind_leg_].visible = false;
        pose[right_front_leg_].visible = false;
        pose[left_front_leg_].visible = false;
    } else if (state.sitting) {
        pose[body_].x_rot = static_cast<float>(M_PI / 6);
        pose[body_].y -= 7.0f * age_scale;
        pose[body_].z += 3.0f * age_scale;
        pose[tail_].x_rot = kQuarterPi;
        pose[tail_].z -= 1.0f * age_scale;
        pose[head_].x_rot = 0.0f;
        pose[head_].y_rot = 0.0f;
        if (state.baby) {
            pose[head_].y -= 1.75f;
            pose[head_].z -= 0.375f;
        } else {
            pose[head_].y -= 6.5f;
            pose[head_].z += 2.75f;
        }

        pose[right_hind_leg_].x_rot = static_cast<float>(-M_PI * 5.0 / 12.0);
        pose[right_hind_leg_].y += 4.0f * age_scale;
        pose[right_hind_leg_].z -= 0.25f * age_scale;
        pose[left_hind_leg_].x_rot = static_cast<float>(-M_PI * 5.0 / 12.0);
        pose[left_hind_leg_].y += 4.0f * age_scale;
        pose[left_hind_leg_].z -= 0.25f * age_scale;
        pose[right_front_leg_].x_rot = static_cast<float>(-M_PI / 12);
        pose[left_front_leg_].x_rot = static_cast<float>(-M_PI / 12);
    }

    if (!state.sleeping && !state.faceplanted && !state.crouching) {
        pose[head_].x_rot = state.x_rot * kDegrees;
        pose[head_].y_rot = state.y_rot * kDegrees;
    }

    if (state.sleeping) {
        pose[head_].x_rot = 0.0f;
        pose[head_].y_rot = static_cast<float>(-M_PI * 2.0 / 3.0);
        pose[head_].z_rot = jmath::cos(state.age_in_ticks * 0.027f) / 22.0f;
    }

    if (state.crouching) {
        const float sway = jmath::cos(state.age_in_ticks) * 0.01f;
        pose[body_].y_rot = sway;
        pose[right_hind_leg_].z_rot = sway;
        pose[left_hind_leg_].z_rot = sway;
        pose[right_front_leg_].z_rot = sway / 2.0f;
        pose[left_front_leg_].z_rot = sway / 2.0f;
    }

    if (state.faceplanted) {
        leg_motion_pos_ += 0.67f;
        pose[right_hind_leg_].x_rot = jmath::cos(leg_motion_pos_ * 0.4662f) * 0.1f;
        pose[left_hind_leg_].x_rot = jmath::cos(leg_motion_pos_ * 0.4662f + kPi) * 0.1f;
        pose[right_front_leg_].x_rot = jmath::cos(leg_motion_pos_ * 0.4662f + kPi) * 0.1f;
        pose[left_front_leg_].x_rot = jmath::cos(leg_motion_pos_ * 0.4662f) * 0.1f;
    }
}

// `FoxRenderer.setupRotations`: a pouncing or planted fox turns with its pitch.
void Fox::setup_rotations(const MobState& state, Placement& place, float body_rot,
                          float scale) const {
    MobModel::setup_rotations(state, place, body_rot, scale);
    if (state.pouncing || state.faceplanted) {
        place.rotate(quaternion_x(to_radians(-state.x_rot)));
    }
}

// `AbstractEquineModel`: every horse, donkey and mule, which rear, graze and
// chew.
class Equine : public MobModel {
public:
    explicit Equine(const Model& model)
        : MobModel(model), body_(part("body")), head_parts_(part("head_parts")),
          right_hind_leg_(part("right_hind_leg")), left_hind_leg_(part("left_hind_leg")),
          right_front_leg_(part("right_front_leg")), left_front_leg_(part("left_front_leg")),
          tail_(part("body/tail")) {}

    void animate(const MobState& state, Pose& pose) const override;

protected:
    int32_t body_, head_parts_;
    int32_t right_hind_leg_, left_hind_leg_, right_front_leg_, left_front_leg_;
    int32_t tail_;
};

// `AbstractEquineModel.setupAnim`: the gait, the rear, the graze and the tail.
void Equine::animate(const MobState& state, Pose& pose) const {
    reset(pose);
    const float head_yaw = std::clamp(state.y_rot, -20.0f, 20.0f);
    float pitch = state.x_rot * kDegrees;
    const float speed = state.walk_speed;
    const float position = state.walk_position;
    if (speed > 0.2f) {
        pitch += jmath::cos(position * 0.8f) * 0.15f * speed;
    }

    const float eat = state.eat_animation;
    const float stand = state.stand_animation;
    const float grounded = 1.0f - stand;
    const float feeding = state.feeding_animation;
    const bool animate_tail = state.animate_tail;
    pose[head_parts_].x_rot = static_cast<float>(M_PI / 6) + pitch;
    pose[head_parts_].y_rot = head_yaw * kDegrees;
    const float water = state.in_water ? 0.2f : 1.0f;
    const float gait = jmath::cos(water * position * 0.6662f + kPi);
    const float front_swing = gait * 0.8f * speed;
    const float settled =
        (1.0f - std::max(stand, eat)) *
        (static_cast<float>(M_PI / 6) + pitch + feeding * jmath::sin(state.age_in_ticks) * 0.05f);
    pose[head_parts_].x_rot = stand * (static_cast<float>(M_PI / 12) + pitch) +
                              eat * (2.1816616f + jmath::sin(state.age_in_ticks) * 0.05f) +
                              settled;
    pose[head_parts_].y_rot =
        stand * head_yaw * kDegrees + (1.0f - std::max(stand, eat)) * pose[head_parts_].y_rot;
    const float age_scale = state.age_scale;
    pose[head_parts_].y =
        pose[head_parts_].y + lerp(eat, lerp(stand, 0.0f, -8.0f * age_scale), 7.0f * age_scale);
    pose[head_parts_].z = lerp(stand, pose[head_parts_].z, -4.0f * age_scale);
    pose[body_].x_rot = stand * -kQuarterPi + grounded * pose[body_].x_rot;
    const float reared = static_cast<float>(M_PI / 12) * stand;
    const float paw = jmath::cos(state.age_in_ticks * 0.6f + kPi);
    pose[left_front_leg_].y -= 12.0f * age_scale * stand;
    pose[left_front_leg_].z += 4.0f * age_scale * stand;
    pose[right_front_leg_].y = pose[left_front_leg_].y;
    pose[right_front_leg_].z = pose[left_front_leg_].z;
    const float left_front = (static_cast<float>(-M_PI / 3) + paw) * stand + front_swing * grounded;
    const float right_front = (static_cast<float>(-M_PI / 3) - paw) * stand - front_swing * grounded;
    pose[left_hind_leg_].x_rot = reared - gait * 0.5f * speed * grounded;
    pose[right_hind_leg_].x_rot = reared + gait * 0.5f * speed * grounded;
    pose[left_front_leg_].x_rot = left_front;
    pose[right_front_leg_].x_rot = right_front;
    pose[tail_].x_rot = static_cast<float>(M_PI / 6) + speed * 0.75f;
    pose[tail_].y += speed * age_scale;
    pose[tail_].z += speed * 2.0f * age_scale;
    if (animate_tail) {
        pose[tail_].y_rot = jmath::cos(state.age_in_ticks * 0.7f);
    } else {
        pose[tail_].y_rot = 0.0f;
    }
}

// `DonkeyModel`: a horse body with the pack chests on its flanks.
class Donkey : public Equine {
public:
    explicit Donkey(const Model& model)
        : Equine(model), left_chest_(part("body/left_chest")),
          right_chest_(part("body/right_chest")) {}

    void animate(const MobState& state, Pose& pose) const override;

private:
    int32_t left_chest_, right_chest_;
};

// `DonkeyModel.setupAnim`: the chests are there only when it carries them.
void Donkey::animate(const MobState& state, Pose& pose) const {
    Equine::animate(state, pose);
    pose[left_chest_].visible = state.has_chest;
    pose[right_chest_].visible = state.has_chest;
}

// `EquineSaddleModel`: the saddle and its reins over a horse body.
class EquineSaddle : public Equine {
public:
    explicit EquineSaddle(const Model& model)
        : Equine(model), left_line_(part("head_parts/left_saddle_line")),
          right_line_(part("head_parts/right_saddle_line")) {}

    void animate(const MobState& state, Pose& pose) const override;

private:
    int32_t left_line_, right_line_;
};

// `EquineSaddleModel.setupAnim`: the reins are held only by a rider.
void EquineSaddle::animate(const MobState& state, Pose& pose) const {
    Equine::animate(state, pose);
    pose[left_line_].visible = state.ridden;
    pose[right_line_].visible = state.ridden;
}

enum class Kind : uint8_t { Quadruped, Sheep, Chicken, Wolf, Feline, Cat, Fox, Equine, Donkey,
                            EquineSaddle };

constexpr std::array<std::pair<std::string_view, Kind>, 54> kLayerKinds = {{
    {"pig", Kind::Quadruped},
    {"pig_baby", Kind::Quadruped},
    {"pig#saddle", Kind::Quadruped},
    {"pig_baby#saddle", Kind::Quadruped},
    {"cold_pig", Kind::Quadruped},
    {"cold_pig_baby", Kind::Quadruped},
    {"cow", Kind::Quadruped},
    {"cow_baby", Kind::Quadruped},
    {"cold_cow", Kind::Quadruped},
    {"cold_cow_baby", Kind::Quadruped},
    {"warm_cow", Kind::Quadruped},
    {"warm_cow_baby", Kind::Quadruped},
    {"mooshroom", Kind::Quadruped},
    {"mooshroom_baby", Kind::Quadruped},
    {"sheep", Kind::Sheep},
    {"sheep_baby", Kind::Sheep},
    {"sheep#wool", Kind::Sheep},
    {"sheep_baby#wool", Kind::Sheep},
    {"sheep#wool_undercoat", Kind::Sheep},
    {"sheep_baby#wool_undercoat", Kind::Sheep},
    {"chicken", Kind::Chicken},
    {"chicken_baby", Kind::Chicken},
    {"cold_chicken", Kind::Chicken},
    {"cold_chicken_baby", Kind::Chicken},
    {"wolf", Kind::Wolf},
    {"wolf_baby", Kind::Wolf},
    {"cat", Kind::Cat},
    {"cat_baby", Kind::Cat},
    {"cat#collar", Kind::Cat},
    {"cat_baby#collar", Kind::Cat},
    {"ocelot", Kind::Feline},
    {"ocelot_baby", Kind::Feline},
    {"fox", Kind::Fox},
    {"fox_baby", Kind::Fox},
    {"horse", Kind::Equine},
    {"horse_baby", Kind::Equine},
    {"horse#saddle", Kind::EquineSaddle},
    {"horse_baby#saddle", Kind::EquineSaddle},
    {"donkey", Kind::Donkey},
    {"donkey_baby", Kind::Donkey},
    {"donkey#saddle", Kind::EquineSaddle},
    {"donkey_baby#saddle", Kind::EquineSaddle},
    {"mule", Kind::Donkey},
    {"mule_baby", Kind::Donkey},
    {"mule#saddle", Kind::EquineSaddle},
    {"mule_baby#saddle", Kind::EquineSaddle},
    {"skeleton_horse", Kind::Equine},
    {"skeleton_horse_baby", Kind::Equine},
    {"skeleton_horse#saddle", Kind::EquineSaddle},
    {"skeleton_horse_baby#saddle", Kind::EquineSaddle},
    {"zombie_horse", Kind::Equine},
    {"zombie_horse_baby", Kind::Equine},
    {"zombie_horse#saddle", Kind::EquineSaddle},
    {"zombie_horse_baby#saddle", Kind::EquineSaddle},
}};

// `SheepRenderState.getWoolColor`: a sheep called "jeb_" cycles the dyes,
// every other one wears its own, dimmed.
uint32_t sheep_wool_colour(const MobState& s) {
    return s.jeb_sheep ? lerped_colour(ColourCycle::Sheep, s.age_in_ticks)
                       : cycle_colour(ColourCycle::Sheep, s.wool_color);
}

// `WolfCollarLayer.submit`, `CatCollarLayer.submit`: the collar's own dye, and
// nothing drawn at all when the animal wears none.
// `SimpleEquipmentLayer.submit`: a saddle is drawn only when the animal
// wears one.
bool is_saddled(const MobState& s) { return s.saddled; }

// `SheepWoolLayer.submit`, `SheepWoolUndercoatLayer.submit`.
bool is_woolly(const MobState& s) { return !s.sheared; }

uint32_t collar_colour(const MobState& s) {
    return s.collar_color < 0 ? 0 : dye_texture_colour(s.collar_color);
}

constexpr std::array<MobOverlay, 1> kPigOverlays = {{
    {"pig#saddle", "equipment/pig_saddle/saddle", Overlay::Plain,
     gpu::EntityRenderer::Type::CutoutNoCull, nullptr, "pig_baby#saddle", is_saddled},
}};

constexpr std::array<MobOverlay, 2> kSheepOverlays = {{
    {"sheep#wool_undercoat", "sheep/sheep_wool_undercoat", Overlay::Plain,
     gpu::EntityRenderer::Type::CutoutNoCull, nullptr, "sheep_baby#wool_undercoat",
     is_woolly},
    {"sheep#wool", "sheep/sheep_wool", Overlay::Plain, gpu::EntityRenderer::Type::CutoutNoCull,
     sheep_wool_colour, "sheep_baby#wool", is_woolly},
}};

constexpr std::array<MobOverlay, 1> kWolfOverlays = {{
    {"", "wolf/wolf_collar", Overlay::Plain, gpu::EntityRenderer::Type::CutoutNoCull,
     collar_colour},
}};

constexpr std::array<MobOverlay, 1> kCatOverlays = {{
    {"cat#collar", "cat/cat_collar", Overlay::Plain, gpu::EntityRenderer::Type::CutoutNoCull,
     collar_colour, "cat_baby#collar"},
}};

constexpr std::array<MobOverlay, 1> kHorseOverlays = {{
    {"horse#saddle", "equipment/horse_saddle/saddle", Overlay::Plain,
     gpu::EntityRenderer::Type::CutoutNoCull, nullptr, "horse_baby#saddle", is_saddled},
}};

constexpr std::array<MobOverlay, 1> kDonkeyOverlays = {{
    {"donkey#saddle", "equipment/donkey_saddle/saddle", Overlay::Plain,
     gpu::EntityRenderer::Type::CutoutNoCull, nullptr, "donkey_baby#saddle", is_saddled},
}};

constexpr std::array<MobOverlay, 1> kMuleOverlays = {{
    {"mule#saddle", "equipment/mule_saddle/saddle", Overlay::Plain,
     gpu::EntityRenderer::Type::CutoutNoCull, nullptr, "mule_baby#saddle", is_saddled},
}};

constexpr std::array<MobOverlay, 1> kSkeletonHorseOverlays = {{
    {"skeleton_horse#saddle", "equipment/skeleton_horse_saddle/saddle", Overlay::Plain,
     gpu::EntityRenderer::Type::CutoutNoCull, nullptr, "skeleton_horse_baby#saddle", is_saddled},
}};

constexpr std::array<MobOverlay, 1> kZombieHorseOverlays = {{
    {"zombie_horse#saddle", "equipment/zombie_horse_saddle/saddle", Overlay::Plain,
     gpu::EntityRenderer::Type::CutoutNoCull, nullptr, "zombie_horse_baby#saddle", is_saddled},
}};

constexpr std::array<MobType, 14> kAnimalTypes = {{
    {"minecraft:pig", "pig", "pig/temperate_pig", "pig_baby", 0.7f,
     gpu::EntityRenderer::Type::CutoutNoCull, false, false, kPigOverlays},
    {"minecraft:cow", "cow", "cow/temperate_cow", "cow_baby", 0.7f,
     gpu::EntityRenderer::Type::CutoutNoCull, false, false, {}},
    {"minecraft:mooshroom", "mooshroom", "cow/red_mooshroom", "mooshroom_baby", 0.7f,
     gpu::EntityRenderer::Type::CutoutNoCull, false, false, {}},
    {"minecraft:sheep", "sheep", "sheep/sheep", "sheep_baby", 0.7f,
     gpu::EntityRenderer::Type::CutoutNoCull, false, false, kSheepOverlays},
    {"minecraft:chicken", "chicken", "chicken/temperate_chicken", "chicken_baby", 0.3f,
     gpu::EntityRenderer::Type::CutoutNoCull, false, false, {}},
    {"minecraft:wolf", "wolf", "wolf/wolf", "wolf_baby", 0.5f,
     gpu::EntityRenderer::Type::CutoutNoCull, false, false, kWolfOverlays},
    {"minecraft:cat", "cat", "cat/tabby", "cat_baby", 0.4f,
     gpu::EntityRenderer::Type::CutoutNoCull, false, false, kCatOverlays},
    {"minecraft:ocelot", "ocelot", "cat/ocelot", "ocelot_baby", 0.4f,
     gpu::EntityRenderer::Type::CutoutNoCull, false, false, {}},
    {"minecraft:fox", "fox", "fox/fox", "fox_baby", 0.4f,
     gpu::EntityRenderer::Type::CutoutNoCull, false, false, {}},
    {"minecraft:horse", "horse", "horse/horse_white", "horse_baby", 0.75f,
     gpu::EntityRenderer::Type::CutoutNoCull, false, false, kHorseOverlays},
    {"minecraft:donkey", "donkey", "horse/donkey", "donkey_baby", 0.75f,
     gpu::EntityRenderer::Type::CutoutNoCull, false, false, kDonkeyOverlays},
    {"minecraft:mule", "mule", "horse/mule", "mule_baby", 0.75f,
     gpu::EntityRenderer::Type::CutoutNoCull, false, false, kMuleOverlays},
    {"minecraft:skeleton_horse", "skeleton_horse", "horse/horse_skeleton",
     "skeleton_horse_baby", 0.75f, gpu::EntityRenderer::Type::CutoutNoCull, false, false,
     kSkeletonHorseOverlays},
    {"minecraft:zombie_horse", "zombie_horse", "horse/horse_zombie", "zombie_horse_baby", 0.75f,
     gpu::EntityRenderer::Type::CutoutNoCull, false, false, kZombieHorseOverlays},
}};

} // namespace

std::unique_ptr<MobModel> make_animal_model(std::string_view layer, const Model& model) {
    for (const auto& [name, kind] : kLayerKinds) {
        if (name != layer) continue;
        switch (kind) {
        case Kind::Quadruped: return std::make_unique<Quadruped>(model);
        case Kind::Sheep: return std::make_unique<Sheep>(model);
        case Kind::Chicken: return std::make_unique<Chicken>(model);
        case Kind::Wolf: return std::make_unique<Wolf>(model);
        case Kind::Feline: return std::make_unique<Feline>(model);
        case Kind::Cat: return std::make_unique<Cat>(model);
        case Kind::Fox: return std::make_unique<Fox>(model);
        case Kind::Equine: return std::make_unique<Equine>(model);
        case Kind::Donkey: return std::make_unique<Donkey>(model);
        case Kind::EquineSaddle: return std::make_unique<EquineSaddle>(model);
        }
    }
    return nullptr;
}

std::span<const MobType> animal_mob_types() { return kAnimalTypes; }

} // namespace mcpp::entity
