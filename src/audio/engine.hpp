#pragma once
#include "audio/categories.hpp"
#include "audio/mixer.hpp"
#include "audio/random.hpp"
#include "audio/sounds.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mcpp::audio {

// Where the player hears from. Angles are the game's own, in degrees: yaw 0
// faces south (+z) and 90 faces west.
struct Listener {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    float yaw = 0.0f;
    float pitch = 0.0f;
};

struct PlayRequest {
    std::string event;
    Category category = Category::Master;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    float volume = 1.0f;
    float pitch = 1.0f;
    // At the listener and never attenuated: the interface, music.
    bool relative = false;
    bool looping = false;
    // Follows this entity while it plays; -1 for a sound that stays put.
    int32_t entity = -1;
};

// One line of the voice log: what started, stopped or was turned away, and
// how loud, how high and where it was at that moment.
struct VoiceLog {
    double time = 0.0;
    std::string what;   // start, stop, end, drop, missing
    uint64_t id = 0;
    std::string event;
    std::string file;
    Category category = Category::Master;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    float distance = 0.0f;
    float range = 0.0f;
    float gain = 0.0f;
    float pitch = 1.0f;
    float left = 0.0f;
    float right = 0.0f;
    bool relative = false;
    bool stream = false;
    int32_t entity = -1;
};

// Reads a file out of the player's own assets, by its object store name.
using FileSource = std::function<std::optional<std::vector<uint8_t>>(std::string_view)>;
// Where an entity is now, or nothing once it has gone.
using EntityPosition = std::function<std::optional<std::array<double, 3>>(int32_t)>;

class SoundEngine {
public:
    // How many voices play at once: 247 for sounds, 8 for streams.
    static constexpr size_t kStaticVoices = 247;
    static constexpr size_t kStreamVoices = 8;

    // Takes the parsed sounds.json and where to read the files from. The
    // mixer is opened with the options; without a device it still mixes.
    bool open(SoundIndex index, FileSource files, const Mixer::Options& options);
    void close();

    const SoundIndex& index() const { return index_; }
    Mixer& mixer() { return mixer_; }

    // The packet's seed, or the client's own random for sounds it makes itself.
    uint64_t play(const PlayRequest& request, LegacyRandom& random);
    uint64_t play(const PlayRequest& request);
    // An empty event stops every event; no category stops every category.
    void stop(std::optional<Category> category, std::string_view event);
    void stop_voice(uint64_t id);
    bool playing(uint64_t id) const { return voices_.contains(id); }
    size_t active() const { return voices_.size(); }

    void set_listener(const Listener& listener) { listener_ = listener; }
    const Listener& listener() const { return listener_; }
    // Once a tick: follows entities, applies the sliders and the listener's
    // movement to every voice, and lets go of the ones that ended.
    void update(const EntityPosition& entity_position = {});
    // The clock the log is written against, in seconds.
    void set_time(double seconds) { time_ = seconds; }

    // The options screen writes these; they take effect on the next update.
    CategoryVolumes volumes;
    std::function<void(const VoiceLog&)> on_log;

private:
    struct Active {
        PlayRequest request;
        ResolvedSound sound;
        float base_gain = 0.0f;
        float range = 16.0f;
        float pitch = 1.0f;
        bool positional = true;
        int channels = 1;
    };
    struct Gains {
        float distance = 0.0f;
        float gain = 0.0f;
        float left = 0.0f;
        float right = 0.0f;
    };
    Gains gains_of(const Active& voice) const;
    std::shared_ptr<const Pcm> load(const std::string& file);
    void log(std::string_view what, uint64_t id, const Active& voice, const Gains& gains);

    SoundIndex index_;
    FileSource files_;
    Mixer mixer_;
    Listener listener_;
    LegacyRandom own_random_{0};
    std::map<std::string, std::shared_ptr<const Pcm>, std::less<>> cache_;
    std::map<uint64_t, Active> voices_;
    double time_ = 0.0;
};

} // namespace mcpp::audio
