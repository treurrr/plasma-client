#include "audio/mixer.hpp"

#include "miniaudio.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace mcpp::audio {

struct Mixer::Voice {
    uint64_t id = 0;
    std::shared_ptr<const Pcm> pcm;
    std::unique_ptr<OggStream> stream;
    // A stream keeps a window of decoded frames starting at window_start.
    std::vector<int16_t> window;
    size_t window_start = 0;
    bool stream_ended = false;
    int channels = 1;
    int rate = 44100;
    double cursor = 0.0;
    float pitch = 1.0f;
    float left = 1.0f;
    float right = 1.0f;
    bool looping = false;
    bool done = false;

    size_t frames_in_window() const { return window.size() / static_cast<size_t>(channels); }

    // Makes sure frames up to `last` are decoded, or knows they never will be.
    void fill(size_t last) {
        if (!stream) return;
        constexpr size_t kChunk = 4096;
        while (!stream_ended && window_start + frames_in_window() <= last) {
            const size_t have = window.size();
            window.resize(have + kChunk * static_cast<size_t>(channels));
            const size_t got = stream->read(std::span(window).subspan(have));
            window.resize(have + got * static_cast<size_t>(channels));
            if (got == 0) stream_ended = true;
        }
        const auto keep_from = static_cast<size_t>(cursor);
        if (keep_from > window_start + kChunk * 4) {
            const size_t drop = keep_from - window_start - 1;
            window.erase(window.begin(),
                         window.begin() + static_cast<ptrdiff_t>(drop * static_cast<size_t>(channels)));
            window_start += drop;
        }
    }

    // Total length when it is known: always for a decoded sound, at the end
    // for a stream.
    bool ended_at(size_t frame) const {
        if (pcm) return frame >= pcm->frames();
        return stream_ended && frame >= window_start + frames_in_window();
    }

    float sample(size_t frame, int channel) const {
        if (pcm) {
            if (frame >= pcm->frames()) return 0.0f;
            return pcm->samples[frame * static_cast<size_t>(channels) + static_cast<size_t>(channel)] /
                   32768.0f;
        }
        if (frame < window_start || frame >= window_start + frames_in_window()) return 0.0f;
        return window[(frame - window_start) * static_cast<size_t>(channels) +
                      static_cast<size_t>(channel)] /
               32768.0f;
    }
};

struct Mixer::Device {
    ma_context context{};
    ma_device device{};
    bool context_ready = false;
    bool device_ready = false;
};

namespace {

void data_callback(ma_device* device, void* output, const void*, ma_uint32 frames) {
    static_cast<Mixer*>(device->pUserData)->mix(static_cast<float*>(output), frames);
}

} // namespace

Mixer::Mixer() = default;
Mixer::~Mixer() { close(); }

bool Mixer::open(const Options& options) {
    close();
    lost_.store(false);
    rate_ = options.rate;
    auto device = std::make_unique<Device>();
    ma_backend null_backend[] = {ma_backend_null};
    if (ma_context_init(options.null_output ? null_backend : nullptr,
                        options.null_output ? 1 : 0, nullptr, &device->context) != MA_SUCCESS) {
        return false;
    }
    device->context_ready = true;

    ma_device_id chosen{};
    bool have_chosen = false;
    if (!options.null_output && !options.device.empty()) {
        ma_device_info* playback = nullptr;
        ma_uint32 count = 0;
        if (ma_context_get_devices(&device->context, &playback, &count, nullptr, nullptr) ==
            MA_SUCCESS) {
            for (ma_uint32 i = 0; i < count; ++i) {
                if (options.device == playback[i].name) {
                    chosen = playback[i].id;
                    have_chosen = true;
                }
            }
        }
    }

    auto config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = 2;
    config.playback.pDeviceID = have_chosen ? &chosen : nullptr;
    config.sampleRate = options.rate;
    config.dataCallback = data_callback;
    config.pUserData = this;
    config.notificationCallback = [](const ma_device_notification* notification) {
        if (notification->type == ma_device_notification_type_stopped ||
            notification->type == ma_device_notification_type_rerouted) {
            static_cast<Mixer*>(notification->pDevice->pUserData)->lost_.store(true);
        }
    };
    if (ma_device_init(&device->context, &config, &device->device) != MA_SUCCESS) {
        ma_context_uninit(&device->context);
        return false;
    }
    device->device_ready = true;
    rate_ = device->device.sampleRate;
    char name[256]{};
    ma_device_get_name(&device->device, ma_device_type_playback, name, sizeof(name), nullptr);
    device_name_ = name;
    if (ma_device_start(&device->device) != MA_SUCCESS) {
        ma_device_uninit(&device->device);
        ma_context_uninit(&device->context);
        return false;
    }
    device_ = std::move(device);
    lost_.store(false);
    return true;
}

void Mixer::close() {
    if (!device_) return;
    if (device_->device_ready) ma_device_uninit(&device_->device);
    if (device_->context_ready) ma_context_uninit(&device_->context);
    device_.reset();
}

std::vector<std::string> Mixer::devices() {
    std::vector<std::string> names;
    ma_context context;
    if (ma_context_init(nullptr, 0, nullptr, &context) != MA_SUCCESS) return names;
    ma_device_info* playback = nullptr;
    ma_uint32 count = 0;
    if (ma_context_get_devices(&context, &playback, &count, nullptr, nullptr) == MA_SUCCESS) {
        for (ma_uint32 i = 0; i < count; ++i) names.emplace_back(playback[i].name);
    }
    ma_context_uninit(&context);
    return names;
}

uint64_t Mixer::add(std::unique_ptr<Voice> voice) {
    std::lock_guard lock(guard_);
    voice->id = next_id_++;
    const uint64_t id = voice->id;
    voices_.push_back(std::move(voice));
    return id;
}

uint64_t Mixer::start(std::shared_ptr<const Pcm> pcm, float pitch, float left, float right,
                      bool looping) {
    if (!pcm || pcm->frames() == 0) return 0;
    auto voice = std::make_unique<Voice>();
    voice->channels = pcm->channels;
    voice->rate = pcm->rate;
    voice->pcm = std::move(pcm);
    voice->pitch = pitch;
    voice->left = left;
    voice->right = right;
    voice->looping = looping;
    return add(std::move(voice));
}

uint64_t Mixer::start(std::unique_ptr<OggStream> stream, float pitch, float left, float right,
                      bool looping) {
    if (!stream) return 0;
    auto voice = std::make_unique<Voice>();
    voice->channels = stream->channels();
    voice->rate = stream->rate();
    voice->stream = std::move(stream);
    voice->pitch = pitch;
    voice->left = left;
    voice->right = right;
    voice->looping = looping;
    voice->fill(8192);
    return add(std::move(voice));
}

void Mixer::set(uint64_t id, float pitch, float left, float right) {
    std::lock_guard lock(guard_);
    for (auto& voice : voices_) {
        if (voice->id != id) continue;
        voice->pitch = pitch;
        voice->left = left;
        voice->right = right;
    }
}

void Mixer::stop(uint64_t id) {
    std::lock_guard lock(guard_);
    std::erase_if(voices_, [id](const auto& voice) { return voice->id == id; });
}

std::vector<uint64_t> Mixer::finished() {
    std::lock_guard lock(guard_);
    std::erase_if(voices_, [this](const auto& voice) {
        if (voice->done) finished_.push_back(voice->id);
        return voice->done;
    });
    return std::exchange(finished_, {});
}

size_t Mixer::voices() const {
    std::lock_guard lock(guard_);
    return voices_.size();
}

void Mixer::mix(float* out, uint32_t frames) {
    std::fill(out, out + static_cast<size_t>(frames) * 2, 0.0f);
    std::lock_guard lock(guard_);
    for (auto& voice : voices_) {
        if (voice->done) continue;
        const double step = voice->pitch * static_cast<double>(voice->rate) / rate_;
        voice->fill(static_cast<size_t>(voice->cursor + step * frames) + 2);
        for (uint32_t i = 0; i < frames; ++i) {
            auto frame = static_cast<size_t>(voice->cursor);
            if (voice->ended_at(frame)) {
                if (!voice->looping) {
                    voice->done = true;
                    break;
                }
                voice->cursor = 0.0;
                frame = 0;
                if (voice->stream) {
                    voice->stream->rewind();
                    voice->window.clear();
                    voice->window_start = 0;
                    voice->stream_ended = false;
                    voice->fill(static_cast<size_t>(step * (frames - i)) + 2);
                }
            }
            const auto blend = static_cast<float>(voice->cursor - static_cast<double>(frame));
            const auto at = [&](int channel) {
                const float a = voice->sample(frame, channel);
                const float b = voice->sample(frame + 1, channel);
                return a + (b - a) * blend;
            };
            const float l = at(0);
            const float r = voice->channels > 1 ? at(1) : l;
            out[i * 2] += l * voice->left;
            out[i * 2 + 1] += r * voice->right;
            voice->cursor += step;
        }
    }
    for (uint32_t i = 0; i < frames * 2; ++i) out[i] = std::clamp(out[i], -1.0f, 1.0f);
}

} // namespace mcpp::audio
