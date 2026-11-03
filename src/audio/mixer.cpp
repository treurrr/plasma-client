#include "audio/mixer.hpp"

#include "miniaudio.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>
#include <utility>

namespace mcpp::audio {

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

constexpr float kSampleScale = 1.0f / 32768.0f;

} // namespace

Mixer::Mixer() = default;
Mixer::~Mixer() { close(); }

bool Mixer::open(const Options& options) {
    close();
    lost_.store(false);
    rate_ = options.rate;
    if (options.no_device) return false;
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
    device_ = std::move(device);
    if (ma_device_start(&device_->device) != MA_SUCCESS) {
        close();
        return false;
    }
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

void Mixer::start(uint32_t slot, std::shared_ptr<const Pcm> pcm, float pitch, float left,
                  float right, float flat, bool looping) {
    set(slot, pitch, left, right, flat);
    Command command;
    command.slot = slot;
    command.pcm = pcm.get();
    command.looping = looping;
    held_[slot] = std::move(pcm);
    while (!commands_.push(command)) std::this_thread::yield();
    if (!device_) apply_commands();
}

void Mixer::start(uint32_t slot, StreamBuffer* stream, float pitch, float left, float right,
                  float flat) {
    set(slot, pitch, left, right, flat);
    Command command;
    command.slot = slot;
    command.stream = stream;
    streams_[slot] = stream;
    while (!commands_.push(command)) std::this_thread::yield();
    if (!device_) apply_commands();
}

void Mixer::set(uint32_t slot, float pitch, float left, float right, float flat) {
    auto& params = params_[slot];
    params.pitch.store(pitch, std::memory_order_relaxed);
    params.left.store(left, std::memory_order_relaxed);
    params.right.store(right, std::memory_order_relaxed);
    params.flat.store(flat, std::memory_order_relaxed);
}

void Mixer::stop(uint32_t slot) {
    Command command;
    command.kind = Command::Kind::Stop;
    command.slot = slot;
    while (!commands_.push(command)) std::this_thread::yield();
    if (!device_) apply_commands();
}

void Mixer::released(std::vector<uint32_t>& slots) {
    while (const auto slot = released_.pop()) {
        held_[*slot].reset();
        if (auto* stream = std::exchange(streams_[*slot], nullptr)) {
            stream->closed.store(true, std::memory_order_release);
        }
        slots.push_back(*slot);
    }
}

void Mixer::release(uint32_t slot) {
    voices_[slot].active = false;
    released_.push(slot);
}

void Mixer::apply_commands() {
    while (const auto command = commands_.pop()) {
        auto& voice = voices_[command->slot];
        if (command->kind == Command::Kind::Stop) {
            if (voice.active) release(command->slot);
            continue;
        }
        const auto& params = params_[command->slot];
        voice = {};
        voice.active = true;
        voice.pcm = command->pcm;
        voice.stream = command->stream;
        voice.looping = command->looping;
        const bool stereo = voice.pcm ? voice.pcm->channels > 1 : false;
        voice.left = stereo ? params.flat.load(std::memory_order_relaxed)
                            : params.left.load(std::memory_order_relaxed);
        voice.right = stereo ? params.flat.load(std::memory_order_relaxed)
                             : params.right.load(std::memory_order_relaxed);
    }
}

namespace {

// Adds a voice into the output, gains moving in a straight line from where the
// last callback left them to where they are asked to be, so a sound passing
// by does not click.
template <int Channels>
struct Accumulate {
    float* out;
    float left;
    float right;
    float left_step;
    float right_step;

    void add(uint32_t i, const int16_t* a, const int16_t* b, float blend) {
        const float l0 = a[0] * kSampleScale;
        const float l = l0 + (b[0] * kSampleScale - l0) * blend;
        float r = l;
        if constexpr (Channels == 2) {
            const float r0 = a[1] * kSampleScale;
            r = r0 + (b[1] * kSampleScale - r0) * blend;
        }
        out[i * 2] += l * (left + left_step * static_cast<float>(i));
        out[i * 2 + 1] += r * (right + right_step * static_cast<float>(i));
    }
};

constexpr int16_t kSilence[2] = {0, 0};

template <int Channels>
bool mix_pcm(const Pcm& pcm, bool looping, double& cursor, double step, Accumulate<Channels> acc,
             uint32_t frames) {
    const size_t length = pcm.frames();
    const int16_t* data = pcm.samples.data();
    for (uint32_t i = 0; i < frames; ++i) {
        auto frame = static_cast<size_t>(cursor);
        if (frame >= length) {
            if (!looping) return false;
            cursor = std::fmod(cursor, static_cast<double>(length));
            frame = static_cast<size_t>(cursor);
        }
        const size_t next = frame + 1;
        const int16_t* b = next < length ? data + next * Channels : looping ? data : kSilence;
        acc.add(i, data + frame * Channels, b,
                static_cast<float>(cursor - static_cast<double>(frame)));
        cursor += step;
    }
    return true;
}

// A stream's cursor counts from the ring's read position. Running dry before
// the decoder has caught up holds the voice where it is; running dry after
// the decoder has finished ends it.
template <int Channels>
bool mix_stream(StreamBuffer& stream, double& cursor, double step, Accumulate<Channels> acc,
                uint32_t frames) {
    uint64_t read = stream.read.load(std::memory_order_relaxed);
    const bool ended = stream.ended.load(std::memory_order_acquire);
    const uint64_t written = stream.written.load(std::memory_order_acquire);
    const int16_t* data = stream.samples.data();
    constexpr uint64_t mask = StreamBuffer::kFrames - 1;
    bool alive = true;
    for (uint32_t i = 0; i < frames; ++i) {
        const auto whole = static_cast<uint64_t>(cursor);
        read += whole;
        cursor -= static_cast<double>(whole);
        if (read >= written) {
            alive = !ended;
            if (read > written) read = written;
            break;
        }
        if (read + 1 >= written && !ended) break;
        const int16_t* a = data + (read & mask) * Channels;
        const int16_t* b = read + 1 < written ? data + ((read + 1) & mask) * Channels : kSilence;
        acc.add(i, a, b, static_cast<float>(cursor));
        cursor += step;
    }
    stream.read.store(read, std::memory_order_release);
    return alive;
}

} // namespace

bool Mixer::mix_voice(Voice& voice, const Params& params, float* out, uint32_t frames) {
    const int channels = voice.pcm ? voice.pcm->channels
                                   : voice.stream->channels.load(std::memory_order_acquire);
    if (channels == 0) return !voice.stream->ended.load(std::memory_order_acquire);
    const int source_rate = voice.pcm ? voice.pcm->rate : voice.stream->rate.load(std::memory_order_relaxed);
    const double step = params.pitch.load(std::memory_order_relaxed) * source_rate /
                        static_cast<double>(rate_);
    const bool stereo = channels > 1;
    const float left = stereo ? params.flat.load(std::memory_order_relaxed)
                              : params.left.load(std::memory_order_relaxed);
    const float right = stereo ? params.flat.load(std::memory_order_relaxed)
                               : params.right.load(std::memory_order_relaxed);
    const float span = frames > 0 ? 1.0f / static_cast<float>(frames) : 0.0f;
    const auto run = [&](auto acc) {
        if (voice.pcm) return mix_pcm(*voice.pcm, voice.looping, voice.cursor, step, acc, frames);
        return mix_stream(*voice.stream, voice.cursor, step, acc, frames);
    };
    bool alive = false;
    if (stereo) {
        alive = run(Accumulate<2>{out, voice.left, voice.right, (left - voice.left) * span,
                                  (right - voice.right) * span});
    } else {
        alive = run(Accumulate<1>{out, voice.left, voice.right, (left - voice.left) * span,
                                  (right - voice.right) * span});
    }
    voice.left = left;
    voice.right = right;
    return alive;
}

void Mixer::mix(float* out, uint32_t frames) {
    const auto begin = std::chrono::steady_clock::now();
    apply_commands();
    std::fill(out, out + static_cast<size_t>(frames) * 2, 0.0f);
    uint32_t playing = 0;
    for (uint32_t slot = 0; slot < kSlots; ++slot) {
        auto& voice = voices_[slot];
        if (!voice.active) continue;
        ++playing;
        if (!mix_voice(voice, params_[slot], out, frames)) release(slot);
    }
    for (size_t i = 0; i < static_cast<size_t>(frames) * 2; ++i) {
        out[i] = std::clamp(out[i], -1.0f, 1.0f);
    }
    const auto spent = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - begin)
            .count());
    callbacks_.fetch_add(1, std::memory_order_relaxed);
    frames_.fetch_add(frames, std::memory_order_relaxed);
    nanoseconds_.fetch_add(spent, std::memory_order_relaxed);
    if (spent > worst_nanoseconds_.load(std::memory_order_relaxed)) {
        worst_nanoseconds_.store(spent, std::memory_order_relaxed);
        worst_voices_.store(playing, std::memory_order_relaxed);
    }
}

Mixer::Stats Mixer::stats() const {
    Stats stats;
    stats.callbacks = callbacks_.load(std::memory_order_relaxed);
    stats.frames = frames_.load(std::memory_order_relaxed);
    stats.nanoseconds = nanoseconds_.load(std::memory_order_relaxed);
    stats.worst_nanoseconds = worst_nanoseconds_.load(std::memory_order_relaxed);
    stats.worst_voices = worst_voices_.load(std::memory_order_relaxed);
    return stats;
}

} // namespace mcpp::audio
