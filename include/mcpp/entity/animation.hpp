#pragma once
#include "mcpp/entity/model.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace mcpp::entity {

// `AnimationChannel.Targets`: which of a part's three triples a channel adds to.
enum class AnimationTarget : uint8_t { Position, Rotation, Scale };

// `AnimationChannel.Interpolations`: the two curves a keyframe is reached by.
enum class Interpolation : uint8_t { Linear, CatmullRom };

// `Keyframe`: a moment and the value the animation holds there, in the units
// `KeyframeAnimations.posVec`, `.degreeVec` and `.scaleVec` already put it in.
struct Keyframe {
    float timestamp = 0.0f;
    std::array<float, 3> pre{};
    std::array<float, 3> post{};
    Interpolation interpolation = Interpolation::Linear;
};

// `AnimationChannel`, carrying the bone the definition declared it on.
struct AnimationChannel {
    std::string bone;
    AnimationTarget target = AnimationTarget::Rotation;
    std::vector<Keyframe> keyframes;
};

// `AnimationDefinition`.
struct AnimationDefinition {
    std::string name;   // "ArmadilloAnimation.ARMADILLO_WALK"
    float length_in_seconds = 0.0f;
    bool looping = false;
    std::vector<AnimationChannel> channels;
};

// Every definition in animations-<protocol>.txt, as tools/export/
// export_animations.py writes it.
class AnimationSet {
public:
    // Throws with the line number on anything it does not understand.
    void parse(std::string_view text);
    bool open(const std::filesystem::path& path);
    const AnimationDefinition* find(std::string_view name) const;
    const std::vector<AnimationDefinition>& all() const { return definitions_; }

    static std::filesystem::path default_path(int32_t protocol);

private:
    std::vector<AnimationDefinition> definitions_;
};

// The baked animations, read once on first use. `Assets` carries the model
// layers and knows nothing of these, so a model class reaches them here.
const AnimationSet& animations();
bool load_animations(const std::filesystem::path& path);
// Animations from a data file instead of the baked copy, for working on them.
void use_animations(std::string_view text);

// `ModelPart.createPartLookup`: an animation names a part by its own short
// name, so the layer is walked the way the game walks it, every part's direct
// children before their children's children, and the first name to land wins.
// -1 when the layer has no such part.
int32_t animated_part(const Model& model, std::string_view bone);

// `KeyframeAnimation`: one definition with every bone already resolved to a
// part of one layer, ready to pose it.
class KeyframeAnimation {
public:
    KeyframeAnimation() = default;
    // `KeyframeAnimation.bake`: throws when the layer has no part a channel
    // names. An animation the data file does not hold poses nothing.
    KeyframeAnimation(const AnimationDefinition* definition, const Model& model);
    KeyframeAnimation(std::string_view name, const Model& model)
        : KeyframeAnimation(animations().find(name), model) {}

    // `KeyframeAnimation.apply(AnimationState, float, float)`, with the
    // milliseconds the caller has already read off its `AnimationState`; the
    // game applies nothing at all while the state is stopped, so the caller
    // asks only when it runs.
    void apply(float elapsed_ms, Pose& pose, float scale = 1.0f) const;
    // `KeyframeAnimation.applyWalk`.
    void apply_walk(float walk_position, float walk_speed, float time_scale, float scale_factor,
                    Pose& pose) const;
    // `KeyframeAnimation.applyStatic`.
    void apply_static(Pose& pose) const { apply(0.0f, pose); }

    bool empty() const { return entries_.empty(); }

private:
    struct Entry {
        int32_t part = -1;
        AnimationTarget target = AnimationTarget::Rotation;
        std::vector<Keyframe> keyframes;
    };

    void apply_millis(int64_t time_ms, float scale, Pose& pose) const;

    float length_in_seconds_ = 0.0f;
    bool looping_ = false;
    std::vector<Entry> entries_;
};

} // namespace mcpp::entity
