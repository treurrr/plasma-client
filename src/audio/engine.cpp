#include "audio/engine.hpp"

#include "audio/attenuation.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>
#include <utility>

namespace mcpp::audio {

PlayRequest PlayRequest::ui(std::string_view event, float pitch, float volume) {
    PlayRequest request;
    request.event = std::string(event);
    request.category = Category::Ui;
    request.volume = volume;
    request.pitch = pitch;
    request.relative = true;
    request.attenuate = false;
    return request;
}

PlayRequest PlayRequest::music(std::string_view event) {
    PlayRequest request = ui(event, 1.0f, 1.0f);
    request.category = Category::Music;
    return request;
}

PlayRequest PlayRequest::ambience(std::string_view event, float pitch, float volume) {
    PlayRequest request = ui(event, pitch, volume);
    request.category = Category::Ambient;
    return request;
}

SoundEngine::~SoundEngine() { close(); }

bool SoundEngine::open(SoundIndex index, FileSource files, const Mixer::Options& options) {
    close();
    index_ = std::move(index);
    files_ = std::move(files);
    options_ = options;
    own_random_.set_seed(std::chrono::steady_clock::now().time_since_epoch().count());
    const bool device = mixer_.open(options);
    threaded_ = device;
    decoder_ = std::make_unique<Decoder>(files_, threaded_);
    free_slots_.clear();
    for (uint32_t slot = Mixer::kSlots; slot-- > 0;) free_slots_.push_back(slot);
    slot_owner_.fill(0);
    slot_stream_.fill(false);
    return device;
}

void SoundEngine::close() {
    mixer_.close();
    for (const auto& [id, voice] : voices_) {
        if (voice.started) mixer_.stop(voice.slot);
    }
    released_.clear();
    mixer_.released(released_);
    decoder_.reset();
    voices_.clear();
    waiting_.clear();
    delayed_.clear();
    cache_.clear();
    streams_ = 0;
    stats_ = {};
    category_gain_.fill(1.0f);
}

uint64_t SoundEngine::play(const PlayRequest& request) { return play(request, own_random_); }

uint64_t SoundEngine::play(const PlayRequest& request, jmath::LegacyRandom& random) {
    if (request.delay > 0) {
        delayed_.push_back({request, random, ticks_ + request.delay});
        Active voice;
        voice.request = request;
        voice.sound.event = std::string(bare_name(request.event));
        log("delay", 0, voice, {});
        return 0;
    }
    return start(request, random);
}

uint64_t SoundEngine::start(const PlayRequest& request, jmath::LegacyRandom& random) {
    Active voice;
    voice.request = request;
    voice.request.delay = 0;
    const auto sound = decoder_ ? index_.pick(request.event, random) : std::nullopt;
    if (!sound) {
        voice.sound.event = std::string(bare_name(request.event));
        log("missing", 0, voice, {});
        return 0;
    }
    voice.sound = *sound;
    voice.volume = request.volume * sound->volume;
    voice.pitch = clamp_pitch(request.pitch * sound->pitch);
    voice.range = audible_range(voice.volume, sound->attenuation_distance);

    const float gain = clamp_volume(voice.volume) * category_volume(request.category);
    if (gain == 0.0f && !request.start_silent && request.category != Category::Music) {
        log("silent", 0, voice, gains_of(voice));
        return 0;
    }
    const size_t static_voices = (Mixer::kSlots - free_slots_.size()) - streams_;
    const bool full = sound->stream ? streams_ >= kStreamVoices : static_voices >= kStaticVoices;
    if (full || free_slots_.empty()) {
        log("drop", 0, voice, gains_of(voice));
        return 0;
    }

    const uint64_t id = next_id_++;
    voice.slot = free_slots_.back();
    free_slots_.pop_back();
    slot_owner_[voice.slot] = id;
    auto& placed = voices_.emplace(id, std::move(voice)).first->second;

    if (placed.sound.stream) {
        auto* buffer = new StreamBuffer();
        buffer->file = placed.sound.file;
        buffer->looping = request.looping;
        if (!decoder_->open(buffer)) {
            delete buffer;
            finish(id, "missing");
            return 0;
        }
        ++streams_;
        slot_stream_[placed.slot] = true;
        const auto gains = gains_of(placed);
        mixer_.start(placed.slot, buffer, placed.pitch, gains.left, gains.right, gains.flat);
        placed.started = true;
        log("start", id, placed, gains);
        return id;
    }
    const std::string file = placed.sound.file;
    if (const auto cached = cache_.find(file); cached != cache_.end()) {
        if (!cached->second) {
            finish(id, "missing");
            return 0;
        }
        begin(id, placed, cached->second);
        return id;
    }
    auto& waiting = waiting_[file];
    waiting.push_back(id);
    if (waiting.size() == 1 && !decoder_->decode(file)) {
        waiting_.erase(file);
        finish(id, "missing");
        return 0;
    }
    if (!threaded_) pump();
    return voices_.contains(id) ? id : 0;
}

void SoundEngine::begin(uint64_t id, Active& voice, std::shared_ptr<const Pcm> pcm) {
    voice.channels = pcm->channels;
    voice.started = true;
    const auto gains = gains_of(voice);
    mixer_.start(voice.slot, std::move(pcm), voice.pitch, gains.left, gains.right, gains.flat,
                 voice.request.looping);
    log("start", id, voice, gains);
}

void SoundEngine::finish(uint64_t id, std::string_view what) {
    const auto found = voices_.find(id);
    if (found == voices_.end()) return;
    auto& voice = found->second;
    log(what, id, voice, gains_of(voice));
    if (voice.started) {
        mixer_.stop(voice.slot);
    } else {
        slot_owner_[voice.slot] = 0;
        free_slots_.push_back(voice.slot);
    }
    voices_.erase(found);
}

void SoundEngine::pump() {
    if (!decoder_) return;
    if (threaded_ && mixer_.lost()) mixer_.open(options_);
    if (!threaded_) decoder_->service();
    while (auto decoded = decoder_->poll()) {
        ++stats_.decodes;
        stats_.decode_ms += decoded->milliseconds;
        if (decoded->milliseconds > stats_.worst_decode_ms) {
            stats_.worst_decode_ms = decoded->milliseconds;
            stats_.worst_decode = decoded->file;
        }
        auto pcm = decoded->pcm;
        if (pcm && (pcm->channels < 1 || pcm->channels > 2 || pcm->frames() == 0)) pcm.reset();
        if (pcm) {
            ++stats_.cached_sounds;
            stats_.cache_bytes += pcm->samples.size() * sizeof(int16_t);
        }
        cache_.insert_or_assign(decoded->file, pcm);
        auto waiting = waiting_.extract(decoded->file);
        if (waiting.empty()) continue;
        for (const uint64_t id : waiting.mapped()) {
            const auto found = voices_.find(id);
            if (found == voices_.end() || found->second.started) continue;
            if (pcm) {
                begin(id, found->second, pcm);
            } else {
                finish(id, "missing");
            }
        }
    }

    released_.clear();
    mixer_.released(released_);
    for (const uint32_t slot : released_) {
        const uint64_t id = std::exchange(slot_owner_[slot], 0);
        free_slots_.push_back(slot);
        if (std::exchange(slot_stream_[slot], false)) --streams_;
        if (const auto found = voices_.find(id); found != voices_.end()) {
            log("end", id, found->second, gains_of(found->second));
            voices_.erase(found);
        }
    }

    for (auto& [id, voice] : voices_) {
        if (!voice.started) continue;
        const auto gains = gains_of(voice);
        mixer_.set(voice.slot, voice.pitch, gains.left, gains.right, gains.flat);
    }
}

void SoundEngine::update(const EntityPosition& entity_position) {
    ++ticks_;
    if (!delayed_.empty()) {
        std::vector<Delayed> due;
        std::erase_if(delayed_, [&](Delayed& waiting) {
            if (waiting.due > ticks_) return false;
            due.push_back(std::move(waiting));
            return true;
        });
        for (auto& waiting : due) start(waiting.request, waiting.random);
    }
    if (entity_position) {
        std::vector<uint64_t> gone;
        for (auto& [id, voice] : voices_) {
            if (voice.request.entity < 0) continue;
            const auto where = entity_position(voice.request.entity);
            if (!where) {
                gone.push_back(id);
                continue;
            }
            voice.request.x = static_cast<float>((*where)[0]);
            voice.request.y = static_cast<float>((*where)[1]);
            voice.request.z = static_cast<float>((*where)[2]);
        }
        for (const uint64_t id : gone) finish(id, "stop");
    }
    pump();
}

void SoundEngine::stop(std::optional<Category> category, std::string_view event) {
    const auto name = bare_name(event);
    if (!category && name.empty()) delayed_.clear();
    std::vector<uint64_t> matched;
    for (const auto& [id, voice] : voices_) {
        const bool category_matches = !category || voice.request.category == *category;
        const bool event_matches = name.empty() || voice.sound.event == name;
        if (category_matches && event_matches) matched.push_back(id);
    }
    for (const uint64_t id : matched) stop_voice(id);
}

void SoundEngine::stop_voice(uint64_t id) {
    const auto found = voices_.find(id);
    if (found != voices_.end()) finish(id, "stop");
}

float SoundEngine::category_volume(Category category) const {
    return std::clamp(volumes.gain(category), 0.0f, 1.0f) * category_gain_[static_cast<size_t>(category)];
}

void SoundEngine::set_category_gain(Category category, float gain) {
    category_gain_[static_cast<size_t>(category)] = std::clamp(gain, 0.0f, 1.0f);
}

void SoundEngine::set_volume(uint64_t id, float volume) {
    const auto found = voices_.find(id);
    if (found == voices_.end()) return;
    found->second.request.volume = volume;
    found->second.volume = volume * found->second.sound.volume;
}

void SoundEngine::set_position(uint64_t id, double x, double y, double z) {
    const auto found = voices_.find(id);
    if (found == voices_.end()) return;
    found->second.request.x = x;
    found->second.request.y = y;
    found->second.request.z = z;
}

void SoundEngine::preload() {
    if (!decoder_) return;
    for (const auto& file : index_.preload_files()) {
        if (cache_.contains(file) || waiting_.contains(file)) continue;
        if (decoder_->decode(file)) waiting_[file];
    }
    if (!threaded_) pump();
}

SoundEngine::Gains SoundEngine::gains_of(const Active& voice) const {
    Gains gains;
    const float slider = category_volume(voice.request.category);
    gains.flat = clamp_volume(voice.volume) * slider;

    double dx = voice.request.x;
    double dy = voice.request.y;
    double dz = voice.request.z;
    if (!voice.request.relative) {
        dx -= listener_.x;
        dy -= listener_.y;
        dz -= listener_.z;
    }
    const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    gains.distance = static_cast<float>(distance);
    gains.gain = gains.flat;
    if (voice.request.attenuate) gains.gain *= distance_gain(gains.distance, voice.range);

    float side = 0.0f;
    if (distance > 1e-6) {
        if (voice.request.relative) {
            side = static_cast<float>(dx / distance);
        } else {
            const double yaw = listener_.yaw * std::numbers::pi / 180.0;
            side = static_cast<float>((dx * -std::cos(yaw) + dz * -std::sin(yaw)) / distance);
        }
    }
    const Pan pan = stereo_pan(side);
    gains.left = gains.gain * pan.left;
    gains.right = gains.gain * pan.right;
    return gains;
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
    const bool flat = voice.channels > 1;
    line.gain = flat ? gains.flat : gains.gain;
    line.left = flat ? gains.flat : gains.left;
    line.right = flat ? gains.flat : gains.right;
    line.pitch = voice.pitch;
    line.relative = voice.request.relative;
    line.stream = voice.sound.stream;
    line.entity = voice.request.entity;
    on_log(line);
}

} // namespace mcpp::audio
