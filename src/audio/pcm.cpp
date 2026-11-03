#include "audio/pcm.hpp"

#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

#include <cstdlib>

namespace mcpp::audio {

std::optional<Pcm> decode_ogg(std::span<const uint8_t> bytes) {
    int channels = 0;
    int rate = 0;
    short* output = nullptr;
    const int frames = stb_vorbis_decode_memory(bytes.data(), static_cast<int>(bytes.size()),
                                                &channels, &rate, &output);
    if (frames < 0 || output == nullptr) return std::nullopt;
    Pcm pcm;
    pcm.channels = channels;
    pcm.rate = rate;
    pcm.samples.assign(output, output + static_cast<size_t>(frames) * static_cast<size_t>(channels));
    std::free(output);
    return pcm;
}

std::unique_ptr<OggStream> OggStream::open(std::vector<uint8_t> bytes) {
    std::unique_ptr<OggStream> stream(new OggStream());
    stream->bytes_ = std::move(bytes);
    int error = 0;
    auto* decoder = stb_vorbis_open_memory(stream->bytes_.data(),
                                           static_cast<int>(stream->bytes_.size()), &error, nullptr);
    if (decoder == nullptr) return nullptr;
    const auto info = stb_vorbis_get_info(decoder);
    stream->decoder_ = decoder;
    stream->channels_ = info.channels;
    stream->rate_ = static_cast<int>(info.sample_rate);
    return stream;
}

OggStream::~OggStream() {
    if (decoder_ != nullptr) stb_vorbis_close(static_cast<stb_vorbis*>(decoder_));
}

size_t OggStream::read(std::span<int16_t> frames) {
    const int got = stb_vorbis_get_samples_short_interleaved(
        static_cast<stb_vorbis*>(decoder_), channels_, frames.data(), static_cast<int>(frames.size()));
    return got > 0 ? static_cast<size_t>(got) : 0;
}

void OggStream::rewind() { stb_vorbis_seek_start(static_cast<stb_vorbis*>(decoder_)); }

} // namespace mcpp::audio
