#pragma once
#include "audio/categories.hpp"
#include "audio/decoder.hpp"
#include "audio/mixer.hpp"
#include "audio/sounds.hpp"

#include "mcpp/jmath.hpp"

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
    // The position is measured from the listener, not the world: the
    // interface and music sit at 0 0 0, right on top of it.
    bool relative = false;
    // Linear fall-off to the range; off, a sound is as loud anywhere.
    bool attenuate = true;
    bool looping = false;
    // Follows this entity while it plays; -1 for a sound that stays put.
    int32_t entity = -1;
    // Waits this many game ticks before it starts.
    int32_t delay = 0;
    // Starts even with its sliders at zero, so turning them up is heard.
    bool start_silent = false;

    // The game's own kinds of sound that are not placed in the world.
    static PlayRequest ui(std::string_view event, float pitch, float volume = 0.25f);
    static PlayRequest music(std::string_view event);
    static PlayRequest ambience(std::string_view event, float pitch, float volume);
};

// One line of the voice log: what started, stopped or was turned away, and
// how loud, how high and where it was at that moment.
struct VoiceLog {
    double time = 0.0;
    std::string what;   // start, stop, end, drop, missing, silent, delay
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

// Where an entity is now, or nothing once it has gone.
using EntityPosition = std::function<std::optional<std::array<double, 3>>(int32_t)>;

class SoundEngine {
public:
    static constexpr size_t kStaticVoices = 247;
    static constexpr size_t kStreamVoices = 8;

    struct Stats {
        size_t cached_sounds = 0;
        size_t cache_bytes = 0;
        size_t decodes = 0;
        double decode_ms = 0.0;
        double worst_decode_ms = 0.0;
        std::string worst_decode;
    };

    SoundEngine() = default;
    ~SoundEngine();
    SoundEngine(const SoundEngine&) = delete;
    SoundEngine& operator=(const SoundEngine&) = delete;

    // Takes the parsed sounds.json and where to read the files from. The
    // mixer is opened with the options; without a device it still mixes, and
    // then decoding happens on the caller's thread, the same way every time.
    bool open(SoundIndex index, FileSource files, const Mixer::Options& options);
    void close();

    const SoundIndex& index() const { return index_; }
    Mixer& mixer() { return mixer_; }

    // The packet's seed, or the client's own random for sounds it makes itself.
    uint64_t play(const PlayRequest& request, jmath::LegacyRandom& random);
    uint64_t play(const PlayRequest& request);
    // No category and an empty event stop everything; either narrows it.
    void stop(std::optional<Category> category, std::string_view event);
    void stop_voice(uint64_t id);
    bool playing(uint64_t id) const { return voices_.contains(id); }
    size_t active() const { return voices_.size(); }

    // A sound that changes as it plays: a loop fading in or out, a voice
    // that moves. The range stays what it was when it started.
    void set_volume(uint64_t id, float volume);
    void set_position(uint64_t id, double x, double y, double z);
    // A gain on top of a category's slider, 0 to 1: how music fades.
    void set_category_gain(Category category, float gain);
    // What a voice of this category at volume one is heard at: its slider
    // times master times the gain above. Zero means a sound would not start.
    float category_volume(Category category) const;

    void set_listener(const Listener& listener) { listener_ = listener; }
    const Listener& listener() const { return listener_; }
    // Once a frame: decoded sounds start, finished voices are let go, and every
    // voice is heard from where the listener is now.
    void pump();
    // Once a game tick: delayed sounds come due and voices on entities follow
    // them. Also pumps.
    void update(const EntityPosition& entity_position = {});
    // The clock the log is written against, in seconds.
    void set_time(double seconds) { time_ = seconds; }
    // Decodes every sound sounds.json marks for loading up front.
    void preload();
    Stats stats() const { return stats_; }

    // The options screen writes these; they are heard on the next frame.
    CategoryVolumes volumes;
    std::function<void(const VoiceLog&)> on_log;

private:
    struct Active {
        PlayRequest request;
        ResolvedSound sound;
        uint32_t slot = 0;
        bool started = false;
        float volume = 1.0f;   // the request's times the sound's own
        float range = 16.0f;
        float pitch = 1.0f;
        int channels = 0;
    };
    struct Gains {
        float distance = 0.0f;
        float gain = 0.0f;
        float left = 0.0f;
        float right = 0.0f;
        float flat = 0.0f;
    };
    struct Delayed {
        PlayRequest request;
        jmath::LegacyRandom random;
        int64_t due = 0;
    };

    uint64_t start(const PlayRequest& request, jmath::LegacyRandom& random);
    Gains gains_of(const Active& voice) const;
    void begin(uint64_t id, Active& voice, std::shared_ptr<const Pcm> pcm);
    void finish(uint64_t id, std::string_view what);
    void log(std::string_view what, uint64_t id, const Active& voice, const Gains& gains);

    SoundIndex index_;
    FileSource files_;
    Mixer::Options options_;
    Mixer mixer_;
    std::unique_ptr<Decoder> decoder_;
    bool threaded_ = false;
    Listener listener_;
    jmath::LegacyRandom own_random_{0};

    std::map<std::string, std::shared_ptr<const Pcm>, std::less<>> cache_;
    std::map<std::string, std::vector<uint64_t>, std::less<>> waiting_;
    std::map<uint64_t, Active> voices_;
    std::array<uint64_t, Mixer::kSlots> slot_owner_{};
    std::array<bool, Mixer::kSlots> slot_stream_{};
    std::vector<uint32_t> free_slots_;
    std::vector<uint32_t> released_;
    size_t streams_ = 0;
    std::vector<Delayed> delayed_;
    std::array<float, kCategoryCount> category_gain_{1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    int64_t ticks_ = 0;
    uint64_t next_id_ = 1;
    double time_ = 0.0;
    Stats stats_;
};

} // namespace mcpp::audio
