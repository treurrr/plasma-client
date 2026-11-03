#pragma once
#include "audio/categories.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace mcpp::audio {

// The sound a numbered level event plays, from the client's own table. The
// pitch is drawn between low and high by the client's random. Only events
// whose volume and pitch are published are here; 2001 (a block breaking)
// and 1010/1011 (records) need the block or the song and are played by the
// caller.
struct LevelEventSound {
    std::string event;
    Category category = Category::Block;
    float volume = 1.0f;
    float pitch_low = 1.0f;
    float pitch_high = 1.0f;
};

std::optional<LevelEventSound> level_event_sound(int32_t type);

constexpr int32_t kLevelEventBlockBreak = 2001;
constexpr int32_t kLevelEventPlayRecord = 1010;
constexpr int32_t kLevelEventStopRecord = 1011;

} // namespace mcpp::audio
