#pragma once
#include "audio/pcm.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace mcpp::audio {

// Voices summed into one stereo stream. The output device pulls from mix(); a
// mixer with no device, or with the null device that plays to nothing, does
// all the same work, which is how it is tested without making a sound.
class Mixer {
public:
    struct Options {
        // A device that consumes the stream at the real rate and plays nothing.
        bool null_output = false;
        // Empty for the system's default.
        std::string device;
        uint32_t rate = 48000;
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

    uint64_t start(std::shared_ptr<const Pcm> pcm, float pitch, float left, float right,
                   bool looping);
    uint64_t start(std::unique_ptr<OggStream> stream, float pitch, float left, float right,
                   bool looping);
    void set(uint64_t id, float pitch, float left, float right);
    void stop(uint64_t id);
    // Voices that ran out since the last call, now gone.
    std::vector<uint64_t> finished();
    size_t voices() const;

    // Interleaved stereo floats, frames of them, overwritten.
    void mix(float* out, uint32_t frames);

private:
    struct Voice;
    struct Device;
    uint64_t add(std::unique_ptr<Voice> voice);

    mutable std::mutex guard_;
    std::vector<std::unique_ptr<Voice>> voices_;
    std::vector<uint64_t> finished_;
    uint64_t next_id_ = 1;
    uint32_t rate_ = 48000;
    std::unique_ptr<Device> device_;
    std::string device_name_;
    std::atomic<bool> lost_{false};
};

} // namespace mcpp::audio
