#pragma once
#include "audio/decoder.hpp"
#include "audio/pcm.hpp"
#include "audio/ring.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace mcpp::audio {

// Voices summed into one stereo stream. The game's thread starts and stops
// voices through a queue and moves them through per-slot atomics; the output
// device's callback pulls from mix(). Neither ever waits for the other. A
// mixer with no device, or with the null device that plays to nothing, does
// all the same work, which is how it is tested without making a sound.
class Mixer {
public:
    // The game's own limit: 247 sounds and 8 streams at once.
    static constexpr uint32_t kSlots = 255;

    struct Options {
        // A device that consumes the stream at the real rate and plays nothing.
        bool null_output = false;
        // No device at all: whoever owns the mixer calls mix() by hand.
        bool no_device = false;
        // Empty for the system's default.
        std::string device;
        uint32_t rate = 48000;
    };

    // What the callback costs, summed since the mixer opened.
    struct Stats {
        uint64_t callbacks = 0;
        uint64_t frames = 0;
        uint64_t nanoseconds = 0;
        uint64_t worst_nanoseconds = 0;
        uint32_t worst_voices = 0;
    };

    Mixer();
    ~Mixer();
    Mixer(const Mixer&) = delete;
    Mixer& operator=(const Mixer&) = delete;

    // False when no device would open; mix() still works then.
    bool open(const Options& options);
    void close();
    bool has_device() const { return device_ != nullptr; }
    // The device went away: unplugged, or the system took it.
    bool lost() const { return lost_.load(); }
    const std::string& device_name() const { return device_name_; }
    uint32_t rate() const { return rate_; }

    // Names of the output devices the system offers, for the options screen.
    static std::vector<std::string> devices();

    // The game's thread. A slot is the caller's until released() hands it back.
    // A mono voice is heard at left and right; one with two channels is never
    // placed and is heard at flat on both sides.
    void start(uint32_t slot, std::shared_ptr<const Pcm> pcm, float pitch, float left, float right,
               float flat, bool looping);
    void start(uint32_t slot, StreamBuffer* stream, float pitch, float left, float right,
               float flat);
    void set(uint32_t slot, float pitch, float left, float right, float flat);
    void stop(uint32_t slot);
    // Slots whose voice ran out or was stopped since the last call, now free.
    // A stream's buffer is closed by then, so the decoder lets go of it.
    void released(std::vector<uint32_t>& slots);

    // Interleaved stereo floats, frames of them, overwritten. Called by the
    // device; by the caller only when there is no device.
    void mix(float* out, uint32_t frames);
    Stats stats() const;

private:
    struct Params {
        std::atomic<float> pitch{1.0f};
        std::atomic<float> left{0.0f};
        std::atomic<float> right{0.0f};
        std::atomic<float> flat{0.0f};
    };
    struct Command {
        enum class Kind : uint8_t { Start, Stop } kind = Kind::Start;
        uint32_t slot = 0;
        const Pcm* pcm = nullptr;
        StreamBuffer* stream = nullptr;
        bool looping = false;
    };
    // Only the callback touches these.
    struct Voice {
        bool active = false;
        const Pcm* pcm = nullptr;
        StreamBuffer* stream = nullptr;
        bool looping = false;
        double cursor = 0.0;
        float left = 0.0f;
        float right = 0.0f;
    };
    struct Device;

    void apply_commands();
    bool mix_voice(Voice& voice, const Params& params, float* out, uint32_t frames);
    void release(uint32_t slot);

    std::array<Params, kSlots> params_;
    std::array<Voice, kSlots> voices_;
    // Holds every sound a voice is playing until its slot comes back.
    std::array<std::shared_ptr<const Pcm>, kSlots> held_;
    std::array<StreamBuffer*, kSlots> streams_{};
    Ring<Command, 1024> commands_;
    Ring<uint32_t, 1024> released_;
    std::vector<float> scratch_;

    std::atomic<uint64_t> callbacks_{0};
    std::atomic<uint64_t> frames_{0};
    std::atomic<uint64_t> nanoseconds_{0};
    std::atomic<uint64_t> worst_nanoseconds_{0};
    std::atomic<uint32_t> worst_voices_{0};

    uint32_t rate_ = 48000;
    std::unique_ptr<Device> device_;
    std::string device_name_;
    std::atomic<bool> lost_{false};
};

} // namespace mcpp::audio
