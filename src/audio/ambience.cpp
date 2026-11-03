#include "audio/ambience.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace mcpp::audio {
namespace {

int32_t next_int(jmath::LegacyRandom& random, int32_t low, int32_t high) {
    return low >= high ? low : random.next_int(high - low + 1) + low;
}

int32_t max_delay(MusicFrequency frequency) {
    switch (frequency) {
    case MusicFrequency::Default: return 20 * 1200;
    case MusicFrequency::Frequent: return 10 * 1200;
    case MusicFrequency::Constant: return 0;
    }
    return 0;
}

int32_t block_floor(double value) { return static_cast<int32_t>(std::floor(value)); }

} // namespace

std::optional<Music> BackgroundMusic::select(bool creative_mode, bool under_water) const {
    if (under_water && underwater) return underwater;
    return creative_mode && creative ? creative : default_music;
}

std::optional<Music> situational_music(const MusicSituation& situation) {
    if (situation.screen) return situation.screen;
    if (!situation.in_world) return Music::menu();
    if (situation.end_boss) return Music::end_boss();
    return situation.background.select(situation.creative, situation.under_water);
}

MusicManager::MusicManager()
    : random_(std::chrono::steady_clock::now().time_since_epoch().count()) {}

int32_t MusicManager::next_delay(const std::optional<Music>& music) {
    const int32_t most = max_delay(frequency_);
    if (!music) return most;
    if (frequency_ == MusicFrequency::Constant) return 100;
    return next_int(random_, std::min(music->min_delay, most), std::min(music->max_delay, most));
}

void MusicManager::set_frequency(MusicFrequency frequency, const std::optional<Music>& music) {
    frequency_ = frequency;
    delay_ = next_delay(music);
}

void MusicManager::tick(SoundEngine& engine, const std::optional<Music>& music, float music_volume) {
    if (voice_ != 0 && gain_ != music_volume && !fade(engine, music_volume)) return;
    if (!music) {
        delay_ = std::max(delay_, 100);
        return;
    }
    if (voice_ != 0) {
        if (music->replace_current && music->event != event_) {
            engine.stop_voice(voice_);
            delay_ = next_int(random_, 0, music->min_delay / 2);
        }
        if (!engine.playing(voice_)) {
            voice_ = 0;
            event_.clear();
            delay_ = std::min(delay_, next_delay(music));
        }
    }
    delay_ = std::min(delay_, next_delay(music));
    if (voice_ == 0 && delay_-- <= 0) start(engine, *music);
}

void MusicManager::start(SoundEngine& engine, const Music& music) {
    const bool audible = engine.category_volume(Category::Music) > 0.0f;
    voice_ = engine.play(PlayRequest::music(music.event));
    event_ = voice_ != 0 ? music.event : std::string();
    if (voice_ != 0) {
        toast_shown_ = audible;
        if (audible && on_now_playing) on_now_playing();
    }
    delay_ = std::numeric_limits<int32_t>::max();
}

void MusicManager::stop(SoundEngine& engine) {
    if (voice_ != 0) {
        engine.stop_voice(voice_);
        voice_ = 0;
        event_.clear();
        if (on_stopped) on_stopped();
    }
    delay_ += 100;
}

bool MusicManager::fade(SoundEngine& engine, float target) {
    if (voice_ == 0) return false;
    if (gain_ == target) return true;
    if (gain_ < target) {
        gain_ += std::clamp(gain_, 5.0e-4f, 0.005f);
        if (gain_ > target) gain_ = target;
    } else {
        gain_ = 0.03f * target + 0.97f * gain_;
        if (std::abs(gain_ - target) < 1.0e-4f || gain_ < target) gain_ = target;
    }
    gain_ = std::clamp(gain_, 0.0f, 1.0f);
    if (gain_ <= 1.0e-4f) {
        stop(engine);
        return false;
    }
    engine.set_category_gain(Category::Music, gain_);
    return true;
}

void Ambience::reset(SoundEngine& engine) {
    for (const auto& loop : loops_) engine.stop_voice(loop.voice);
    loops_.clear();
    for (const uint64_t voice : underwater_sounds_) engine.stop_voice(voice);
    underwater_sounds_.clear();
    if (underwater_loop_ != 0) engine.stop_voice(underwater_loop_);
    *this = Ambience();
}

void Ambience::tick(SoundEngine& engine, const AmbientSounds& sounds, const AmbientPlayer& player,
                    const LightAt& sky_light, const LightAt& block_light, jmath::LegacyRandom& level_random) {
    // Entering and leaving the water, and its loop fading with the time spent in it.
    if (!was_under_water_ && player.under_water) {
        PlayRequest enter;
        enter.event = "ambient.underwater.enter";
        enter.category = Category::Ambient;
        enter.x = player.x;
        enter.y = player.y;
        enter.z = player.z;
        jmath::LegacyRandom seeded(level_random.next_long());
        engine.play(enter, seeded);
        if (underwater_loop_ != 0) engine.stop_voice(underwater_loop_);
        auto loop = PlayRequest::ambience("ambient.underwater.loop", 1.0f, 1.0f);
        loop.looping = true;
        underwater_loop_ = engine.play(loop);
        underwater_fade_ = 0;
    }
    if (was_under_water_ && !player.under_water) {
        PlayRequest exit;
        exit.event = "ambient.underwater.exit";
        exit.category = Category::Ambient;
        exit.x = player.x;
        exit.y = player.y;
        exit.z = player.z;
        jmath::LegacyRandom seeded(level_random.next_long());
        engine.play(exit, seeded);
    }
    was_under_water_ = player.under_water;
    if (underwater_loop_ != 0) {
        if (underwater_fade_ >= 0) {
            underwater_fade_ += player.under_water ? 1 : -2;
            underwater_fade_ = std::min(underwater_fade_, 40);
            engine.set_volume(underwater_loop_,
                              std::max(0.0f, std::min(static_cast<float>(underwater_fade_) / 40.0f, 1.0f)));
        } else {
            engine.stop_voice(underwater_loop_);
            underwater_loop_ = 0;
        }
    }
    std::erase_if(underwater_sounds_, [&](uint64_t voice) {
        if (!engine.playing(voice)) return true;
        if (player.under_water) return false;
        engine.stop_voice(voice);
        return true;
    });

    // The underwater handler.
    --underwater_delay_;
    if (underwater_delay_ <= 0 && player.under_water) {
        const float roll = level_random.next_float();
        const char* event = roll < 1.0e-4f  ? "ambient.underwater.loop.additions.ultra_rare"
                            : roll < 0.001f ? "ambient.underwater.loop.additions.rare"
                            : roll < 0.01f  ? "ambient.underwater.loop.additions"
                                            : nullptr;
        if (event != nullptr) {
            underwater_delay_ = 0;
            if (const uint64_t voice = engine.play(PlayRequest::ambience(event, 1.0f, 1.0f))) {
                underwater_sounds_.push_back(voice);
            }
        }
    }

    // The bubble column handler.
    if (player.bubble_column) {
        if (!was_in_column_ && !first_tick_ && !player.spectator) {
            PlayRequest inside;
            inside.event = *player.bubble_column ? "block.bubble_column.whirlpool_inside"
                                                 : "block.bubble_column.upwards_inside";
            inside.category = Category::Player;
            inside.x = player.x;
            inside.y = player.y;
            inside.z = player.z;
            jmath::LegacyRandom seeded(level_random.next_long());
            engine.play(inside, seeded);
        }
        was_in_column_ = true;
    } else {
        was_in_column_ = false;
    }
    first_tick_ = false;

    // The biome handler: the loop cross-fades over two seconds.
    std::erase_if(loops_, [&](Fading& loop) {
        if (loop.fade < 0 || !engine.playing(loop.voice)) {
            engine.stop_voice(loop.voice);
            return true;
        }
        loop.fade += loop.direction;
        engine.set_volume(loop.voice, std::clamp(static_cast<float>(loop.fade) / 40.0f, 0.0f, 1.0f));
        return false;
    });
    if (sounds.loop != previous_loop_) {
        previous_loop_ = sounds.loop;
        for (auto& loop : loops_) {
            loop.fade = std::min(loop.fade, 40);
            loop.direction = -1;
        }
        if (sounds.loop) {
            auto found = std::find_if(loops_.begin(), loops_.end(),
                                      [&](const Fading& loop) { return loop.event == *sounds.loop; });
            if (found == loops_.end()) {
                auto request = PlayRequest::ambience(*sounds.loop, 1.0f, 1.0f);
                request.looping = true;
                loops_.push_back({engine.play(request), *sounds.loop, 0, 0});
                found = std::prev(loops_.end());
            }
            found->fade = std::max(0, found->fade);
            found->direction = 1;
        }
    }

    for (const auto& addition : sounds.additions) {
        if (level_random.next_double() < addition.tick_chance) {
            engine.play(PlayRequest::ambience(addition.event, 1.0f, 1.0f));
        }
    }

    if (sounds.mood) {
        const auto& mood = *sounds.mood;
        const int32_t span = mood.block_search_extent * 2 + 1;
        const int32_t dx = level_random.next_int(span);
        const int32_t dy = level_random.next_int(span);
        const int32_t dz = level_random.next_int(span);
        const int32_t bx = block_floor(player.x + dx - mood.block_search_extent);
        const int32_t by = block_floor(player.eye_y + dy - mood.block_search_extent);
        const int32_t bz = block_floor(player.z + dz - mood.block_search_extent);
        const int32_t sky = sky_light(bx, by, bz);
        if (sky > 0) {
            moodiness_ -= static_cast<float>(sky) / 15.0f * 0.001f;
        } else {
            moodiness_ -= static_cast<float>(block_light(bx, by, bz) - 1) / static_cast<float>(mood.tick_delay);
        }
        if (moodiness_ >= 1.0f) {
            const double ox = bx + 0.5 - player.x;
            const double oy = by + 0.5 - player.eye_y;
            const double oz = bz + 0.5 - player.z;
            const double distance = std::sqrt(ox * ox + oy * oy + oz * oz);
            const double reach = distance + mood.sound_position_offset;
            PlayRequest request;
            request.event = mood.event;
            request.category = Category::Ambient;
            request.x = player.x + ox / distance * reach;
            request.y = player.eye_y + oy / distance * reach;
            request.z = player.z + oz / distance * reach;
            engine.play(request, level_random);
            moodiness_ = 0.0f;
        } else {
            moodiness_ = std::max(moodiness_, 0.0f);
        }
    }
}

} // namespace mcpp::audio
