#pragma once
#include <algorithm>
#include <cmath>

namespace mcpp::audio {

// What a vanilla client does to the numbers a server sends: volume is capped
// to 0..1 and pitch to 0.5..2, and a volume above 1 makes the sound reach
// further instead of louder.
inline float clamp_volume(float volume) { return std::clamp(volume, 0.0f, 1.0f); }
// `SoundEngine.calculatePitch`: pitch held to 0.5..2.
inline float clamp_pitch(float pitch) { return std::clamp(pitch, 0.5f, 2.0f); }

// How far a sound carries: the event's attenuation distance, 16 unless
// sounds.json says otherwise, times the volume once that is above 1.
inline float audible_range(float volume, int attenuation_distance) {
    return static_cast<float>(attenuation_distance) * std::max(volume, 1.0f);
}

// Full gain at the source, falling in a straight line to nothing at the edge
// of the range, and nothing beyond it.
inline float distance_gain(float distance, float range) {
    if (range <= 0.0f) return 0.0f;
    return std::clamp(1.0f - distance / range, 0.0f, 1.0f);
}

struct Pan {
    float left = 1.0f;
    float right = 1.0f;
};

// Plain stereo: how far the source sits towards the listener's right, -1 to 1,
// spread over the two speakers with equal power so a sound that moves across
// does not dip in the middle. A source on top of the listener plays centred.
inline Pan stereo_pan(float side) {
    side = std::clamp(side, -1.0f, 1.0f);
    return {std::sqrt(0.5f * (1.0f - side)), std::sqrt(0.5f * (1.0f + side))};
}

} // namespace mcpp::audio
