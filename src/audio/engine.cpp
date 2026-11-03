#include "audio/engine.hpp"

#include "audio/attenuation.hpp"

#include <chrono>
#include <cmath>
#include <numbers>

namespace mcpp::audio {

bool SoundEngine::open(SoundIndex index, FileSource files, const Mixer::Options& options) {
    index_ = std::move(index);
    files_ = std::move(files);
    own_random_.set_seed(std::chrono::steady_clock::now().time_since_epoch().count());
    return mixer_.open(options);
}

void SoundEngine::close() {
    mixer_.close();
    voices_.clear();
    cache_.clear();
}

std::shared_ptr<const Pcm> SoundEngine::load(const std::string& file) {
    if (const auto found = cache_.find(file); found != cache_.end()) return found->second;
    std::shared_ptr<const Pcm> pcm;
    if (files_) {
        if (const auto bytes = files_(file)) {
            if (auto decoded = decode_ogg(*bytes)) pcm = std::make_shared<Pcm>(std::move(*decoded));
        }
    }
    cache_.emplace(file, pcm);
    return pcm;
}

uint64_t SoundEngine::play(const PlayRequest& request) { return play(request, own_random_); }

uint64_t SoundEngine::play(const PlayRequest& request, jmath::LegacyRandom& random) {
    Active voice;
    voice.request = request;
    const auto sound = index_.pick(request.event, random);
    if (!sound) {
        voice.sound.event = std::string(bare_name(request.event));
        log("missing", 0, voice, {});
        return 0;
    }
    voice.sound = *sound;
    voice.base_gain = clamp_volume(request.volume * sound->volume);
    voice.range = audible_range(request.volume, sound->attenuation_distance);
    voice.pitch = clamp_pitch(request.pitch * sound->pitch);

    size_t streams = 0;
    for (const auto& [id, other] : voices_) streams += other.sound.stream ? 1 : 0;
    const bool full = sound->stream ? streams >= kStreamVoices
                                    : voices_.size() - streams >= kStaticVoices;
    if (full) {
        log("drop", 0, voice, gains_of(voice));
        return 0;
    }

    uint64_t id = 0;
    Gains gains;
    if (sound->stream) {
        auto bytes = files_ ? files_(sound->file) : std::nullopt;
        auto stream = bytes ? OggStream::open(std::move(*bytes)) : nullptr;
        if (stream) {
            voice.channels = stream->channels();
            voice.positional = voice.channels == 1 && !request.relative;
            gains = gains_of(voice);
            id = mixer_.start(std::move(stream), voice.pitch, gains.left, gains.right,
                              request.looping);
        }
    } else if (const auto pcm = load(sound->file)) {
        voice.channels = pcm->channels;
        // A stereo file is never placed in the world, wherever it was asked
        // to play: only mono sounds get quieter with distance.
        voice.positional = voice.channels == 1 && !request.relative;
        gains = gains_of(voice);
        id = mixer_.start(pcm, voice.pitch, gains.left, gains.right, request.looping);
    }
    if (id == 0) {
        log("missing", 0, voice, {});
        return 0;
    }
    log("start", id, voice, gains);
    voices_.emplace(id, std::move(voice));
    return id;
}

SoundEngine::Gains SoundEngine::gains_of(const Active& voice) const {
    Gains gains;
    const float slider = volumes.gain(voice.request.category);
    if (!voice.positional) {
        gains.gain = voice.base_gain * slider;
        const Pan centre = voice.channels > 1 ? Pan{} : stereo_pan(0.0f);
        gains.left = gains.gain * centre.left;
        gains.right = gains.gain * centre.right;
        return gains;
    }
    const double dx = voice.request.x - listener_.x;
    const double dy = voice.request.y - listener_.y;
    const double dz = voice.request.z - listener_.z;
    const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    gains.distance = static_cast<float>(distance);
    gains.gain = voice.base_gain * slider * distance_gain(gains.distance, voice.range);

    float side = 0.0f;
    if (distance > 1e-6) {
        const double yaw = listener_.yaw * std::numbers::pi / 180.0;
        const double right_x = -std::cos(yaw);
        const double right_z = -std::sin(yaw);
        side = static_cast<float>((dx * right_x + dz * right_z) / distance);
    }
    const Pan pan = stereo_pan(side);
    gains.left = gains.gain * pan.left;
    gains.right = gains.gain * pan.right;
    return gains;
}

void SoundEngine::update(const EntityPosition& entity_position) {
    for (const uint64_t id : mixer_.finished()) {
        if (const auto found = voices_.find(id); found != voices_.end()) {
            log("end", id, found->second, gains_of(found->second));
            voices_.erase(found);
        }
    }
    for (auto it = voices_.begin(); it != voices_.end();) {
        auto& voice = it->second;
        if (voice.request.entity >= 0 && entity_position) {
            const auto where = entity_position(voice.request.entity);
            if (!where) {
                log("stop", it->first, voice, gains_of(voice));
                mixer_.stop(it->first);
                it = voices_.erase(it);
                continue;
            }
            voice.request.x = (*where)[0];
            voice.request.y = (*where)[1];
            voice.request.z = (*where)[2];
        }
        const auto gains = gains_of(voice);
        mixer_.set(it->first, voice.pitch, gains.left, gains.right);
        ++it;
    }
}

void SoundEngine::stop(std::optional<Category> category, std::string_view event) {
    const auto name = bare_name(event);
    for (auto it = voices_.begin(); it != voices_.end();) {
        const auto& voice = it->second;
        const bool category_matches = !category || voice.request.category == *category;
        const bool event_matches = name.empty() || voice.sound.event == name;
        if (category_matches && event_matches) {
            log("stop", it->first, voice, gains_of(voice));
            mixer_.stop(it->first);
            it = voices_.erase(it);
        } else {
            ++it;
        }
    }
}

void SoundEngine::stop_voice(uint64_t id) {
    if (const auto found = voices_.find(id); found != voices_.end()) {
        log("stop", id, found->second, gains_of(found->second));
        mixer_.stop(id);
        voices_.erase(found);
    }
}

void SoundEngine::log(std::string_view what, uint64_t id, const Active& voice,
                      const Gains& gains) {
    if (!on_log) return;
    VoiceLog line;
    line.time = time_;
    line.what = std::string(what);
    line.id = id;
    line.event = voice.sound.event;
    line.file = voice.sound.file;
    line.category = voice.request.category;
    line.x = voice.request.x;
    line.y = voice.request.y;
    line.z = voice.request.z;
    line.distance = gains.distance;
    line.range = voice.range;
    line.gain = gains.gain;
    line.pitch = voice.pitch;
    line.left = gains.left;
    line.right = gains.right;
    line.relative = voice.request.relative;
    line.stream = voice.sound.stream;
    line.entity = voice.request.entity;
    on_log(line);
}

} // namespace mcpp::audio
