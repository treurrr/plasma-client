#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace mcpp::audio {

// A whole sound decoded up front: interleaved 16-bit frames.
struct Pcm {
    int channels = 1;
    int rate = 44100;
    std::vector<int16_t> samples;
    size_t frames() const { return channels > 0 ? samples.size() / static_cast<size_t>(channels) : 0; }
};

// Nothing when the bytes are not Ogg Vorbis.
std::optional<Pcm> decode_ogg(std::span<const uint8_t> bytes);

// A long sound decoded as it plays: music and records. It keeps the file's
// bytes and decodes a little ahead of what has been asked for.
class OggStream {
public:
    static std::unique_ptr<OggStream> open(std::vector<uint8_t> bytes);
    ~OggStream();
    OggStream(const OggStream&) = delete;
    OggStream& operator=(const OggStream&) = delete;

    int channels() const { return channels_; }
    int rate() const { return rate_; }
    // Up to frames.size() / channels frames, fewer at the end of the file.
    size_t read(std::span<int16_t> frames);
    void rewind();

private:
    OggStream() = default;
    std::vector<uint8_t> bytes_;
    void* decoder_ = nullptr;
    int channels_ = 0;
    int rate_ = 0;
};

} // namespace mcpp::audio
