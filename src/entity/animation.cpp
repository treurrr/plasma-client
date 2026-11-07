#include "mcpp/entity/animation.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <format>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace mcpp::entity {
namespace {

std::vector<std::string_view> words(std::string_view line) {
    std::vector<std::string_view> out;
    size_t at = 0;
    while (at < line.size()) {
        while (at < line.size() && (line[at] == ' ' || line[at] == '\t')) ++at;
        size_t end = at;
        while (end < line.size() && line[end] != ' ' && line[end] != '\t') ++end;
        if (end > at) out.push_back(line.substr(at, end - at));
        at = end;
    }
    return out;
}

struct Reader {
    std::vector<std::string_view> tokens;
    size_t at = 0;
    size_t line = 0;

    [[noreturn]] void fail(std::string_view what) const {
        throw std::runtime_error(std::format("animations, line {}: {}", line, what));
    }
    bool done() const { return at >= tokens.size(); }
    std::string_view peek() const { return done() ? std::string_view{} : tokens[at]; }
    std::string_view word() {
        if (done()) fail("line ends too soon");
        return tokens[at++];
    }
    float number() {
        const auto text = word();
        float value = 0.0f;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (error != std::errc{} || end != text.data() + text.size()) {
            fail(std::format("'{}' is not a number", text));
        }
        return value;
    }
    std::array<float, 3> three() { return {number(), number(), number()}; }
};

AnimationTarget read_target(Reader& reader) {
    const auto word = reader.word();
    if (word == "position") return AnimationTarget::Position;
    if (word == "rotation") return AnimationTarget::Rotation;
    if (word == "scale") return AnimationTarget::Scale;
    reader.fail(std::format("'{}' is no animation target", word));
}

Interpolation read_interpolation(Reader& reader) {
    const auto word = reader.word();
    if (word == "linear") return Interpolation::Linear;
    if (word == "catmullrom") return Interpolation::CatmullRom;
    reader.fail(std::format("'{}' is no interpolation", word));
}

// `Mth.binarySearch`: the first index the predicate holds at, or `to`.
template <class Predicate>
int32_t binary_search(int32_t from, int32_t to, Predicate test) {
    int32_t count = to - from;
    while (count > 0) {
        const int32_t half = count / 2;
        const int32_t at = from + half;
        if (test(at)) {
            count = half;
        } else {
            from = at + 1;
            count -= half + 1;
        }
    }
    return from;
}

// `Mth.catmullrom`.
float catmullrom(float t, float a, float b, float c, float d) {
    return 0.5f * (2.0f * b + (c - a) * t + (2.0f * a - 5.0f * b + 4.0f * c - d) * t * t +
                   (3.0f * b - a - 3.0f * c + d) * t * t * t);
}

// `AnimationChannel.Interpolation.apply`: the post-target of the keyframe
// behind, lerped toward the pre-target of the one ahead, then scaled.
std::array<float, 3> linear(float t, const std::vector<Keyframe>& keys, int32_t from, int32_t to,
                            float scale) {
    const auto& a = keys[static_cast<size_t>(from)].post;
    const auto& b = keys[static_cast<size_t>(to)].pre;
    std::array<float, 3> out{};
    for (size_t i = 0; i < 3; ++i) out[i] = ((b[i] - a[i]) * t + a[i]) * scale;
    return out;
}

// `Mth.catmullrom`: the four post-targets around
// the pair, through the game's spline.
std::array<float, 3> catmull_rom(float t, const std::vector<Keyframe>& keys, int32_t from,
                                 int32_t to, float scale) {
    const int32_t last = static_cast<int32_t>(keys.size()) - 1;
    const auto& p0 = keys[static_cast<size_t>(std::max(0, from - 1))].post;
    const auto& p1 = keys[static_cast<size_t>(from)].post;
    const auto& p2 = keys[static_cast<size_t>(to)].post;
    const auto& p3 = keys[static_cast<size_t>(std::min(last, to + 1))].post;
    std::array<float, 3> out{};
    for (size_t i = 0; i < 3; ++i) out[i] = catmullrom(t, p0[i], p1[i], p2[i], p3[i]) * scale;
    return out;
}

AnimationSet& store() {
    static AnimationSet set;
    return set;
}

bool& loaded() {
    static bool flag = false;
    return flag;
}

} // namespace

void AnimationSet::parse(std::string_view text) {
    AnimationDefinition* definition = nullptr;
    AnimationChannel* channel = nullptr;
    size_t line_number = 0;
    while (!text.empty()) {
        ++line_number;
        const size_t end = text.find('\n');
        std::string_view line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        Reader reader{words(line), 0, line_number};
        if (reader.done() || reader.tokens[0].starts_with('#')) continue;
        const auto head = reader.word();
        if (head.starts_with('[')) {
            if (!head.ends_with(']') || reader.tokens.size() != 1) reader.fail("bad section");
            definitions_.push_back({});
            definition = &definitions_.back();
            channel = nullptr;
            definition->name = std::string(head.substr(1, head.size() - 2));
            continue;
        }
        if (definition == nullptr) reader.fail("outside any [animation]");
        if (head == "length") {
            definition->length_in_seconds = reader.number();
        } else if (head == "loop") {
            definition->looping = true;
        } else if (head == "channel") {
            AnimationChannel next;
            next.bone = std::string(reader.word());
            next.target = read_target(reader);
            definition->channels.push_back(std::move(next));
            channel = &definition->channels.back();
        } else if (head == "key") {
            if (channel == nullptr) reader.fail("key before any channel");
            Keyframe key;
            key.timestamp = reader.number();
            key.post = reader.three();
            key.pre = key.post;
            if (reader.peek() == "->") {
                reader.word();
                key.pre = key.post;
                key.post = reader.three();
            }
            key.interpolation = read_interpolation(reader);
            if (!reader.done()) reader.fail("too much on one key");
            channel->keyframes.push_back(key);
        } else {
            reader.fail(std::format("unknown line '{}'", head));
        }
    }
}

bool AnimationSet::open(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    const std::string text((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
    definitions_.clear();
    parse(text);
    return true;
}

const AnimationDefinition* AnimationSet::find(std::string_view name) const {
    for (const auto& definition : definitions_) {
        if (definition.name == name) return &definition;
    }
    return nullptr;
}

std::filesystem::path AnimationSet::default_path(int32_t protocol) {
    std::filesystem::path base;
    if (const char* data_home = std::getenv("XDG_DATA_HOME")) {
        base = data_home;
    } else if (const char* home = std::getenv("HOME")) {
        base = std::filesystem::path(home) / ".local" / "share";
    }
    return base / "plasma-client" / "data" / std::format("animations-{}.txt", protocol);
}

const AnimationSet& animations() {
    if (!loaded()) {
        loaded() = true;
        store().open(AnimationSet::default_path(774));
    }
    return store();
}

bool load_animations(const std::filesystem::path& path) {
    store() = AnimationSet{};
    loaded() = true;
    return store().open(path);
}

void use_animations(std::string_view text) {
    store() = AnimationSet{};
    loaded() = true;
    store().parse(text);
}

namespace {

// The game's own walk over a part tree: every part's direct children before
// any of their children, with the first name to land keeping it.
bool walk_children(const Model& model, int32_t parent, std::string_view bone, int32_t& found) {
    for (size_t i = 0; i < model.parts.size(); ++i) {
        if (model.parts[i].parent != parent) continue;
        const std::string& path = model.parts[i].path;
        const size_t slash = path.rfind('/');
        const std::string_view name =
            slash == std::string::npos ? std::string_view(path) : std::string_view(path).substr(slash + 1);
        if (name == bone && found < 0) found = static_cast<int32_t>(i);
    }
    for (size_t i = 0; i < model.parts.size(); ++i) {
        if (model.parts[i].parent == parent) {
            walk_children(model, static_cast<int32_t>(i), bone, found);
        }
    }
    return found >= 0;
}

} // namespace

// `ModelPart.createPartLookup`: "root" is the layer's own root, every other
// name is a part's last path segment.
int32_t animated_part(const Model& model, std::string_view bone) {
    const int32_t root = model.find(kRootPath);
    if (bone == "root") return root;
    int32_t found = -1;
    walk_children(model, root, bone, found);
    return found;
}

// `KeyframeAnimation.bake`: every channel's bone looked up in the layer once,
// so posing it later is a walk over an array.
KeyframeAnimation::KeyframeAnimation(const AnimationDefinition* definition, const Model& model) {
    if (definition == nullptr) return;
    length_in_seconds_ = definition->length_in_seconds;
    looping_ = definition->looping;
    for (const AnimationChannel& channel : definition->channels) {
        const int32_t part = animated_part(model, channel.bone);
        if (part < 0) {
            throw std::runtime_error(std::format("cannot animate {}, which does not exist in {}",
                                                 channel.bone, model.name));
        }
        entries_.push_back({part, channel.target, channel.keyframes});
    }
}

// `KeyframeAnimation.apply(long, float)` with `getElapsedSeconds` folded in:
// a looping animation wraps, a one-shot runs off its own end and holds.
void KeyframeAnimation::apply_millis(int64_t time_ms, float scale, Pose& pose) const {
    const float seconds = static_cast<float>(time_ms) / 1000.0f;
    const float elapsed = looping_ ? std::fmod(seconds, length_in_seconds_) : seconds;
    for (const Entry& entry : entries_) {
        const std::vector<Keyframe>& keys = entry.keyframes;
        const auto count = static_cast<int32_t>(keys.size());
        const int32_t from = std::max(
            0, binary_search(0, count,
                             [&](int32_t i) {
                                 return elapsed <= keys[static_cast<size_t>(i)].timestamp;
                             }) -
                   1);
        const int32_t to = std::min(count - 1, from + 1);
        const Keyframe& behind = keys[static_cast<size_t>(from)];
        const Keyframe& ahead = keys[static_cast<size_t>(to)];
        const float since = elapsed - behind.timestamp;
        const float t = to != from
                            ? std::clamp(since / (ahead.timestamp - behind.timestamp), 0.0f, 1.0f)
                            : 0.0f;
        const std::array<float, 3> value = ahead.interpolation == Interpolation::CatmullRom
                                               ? catmull_rom(t, keys, from, to, scale)
                                               : linear(t, keys, from, to, scale);
        PartPose& part = pose[entry.part];
        switch (entry.target) {
        case AnimationTarget::Position:
            part.x += value[0];
            part.y += value[1];
            part.z += value[2];
            break;
        case AnimationTarget::Rotation:
            part.x_rot += value[0];
            part.y_rot += value[1];
            part.z_rot += value[2];
            break;
        case AnimationTarget::Scale:
            part.x_scale += value[0];
            part.y_scale += value[1];
            part.z_scale += value[2];
            break;
        }
    }
}

void KeyframeAnimation::apply(float elapsed_ms, Pose& pose, float scale) const {
    apply_millis(static_cast<int64_t>(elapsed_ms), scale, pose);
}

// `KeyframeAnimation.applyWalk`: the walk cycle runs off the distance walked
// and fades in with how fast the legs are moving.
void KeyframeAnimation::apply_walk(float walk_position, float walk_speed, float time_scale,
                                   float scale_factor, Pose& pose) const {
    const auto time = static_cast<int64_t>(walk_position * 50.0f * time_scale);
    apply_millis(time, std::min(walk_speed * scale_factor, 1.0f), pose);
}

} // namespace mcpp::entity
