#pragma once
#include "audio/engine.hpp"

#include "mcpp/jmath.hpp"
#include "mcpp/registries.hpp"

#include <cstdint>
#include <optional>
#include <string_view>

namespace mcpp::audio {

// A numbered level event from the server and what the client knows around it.
struct LevelEvent {
    int32_t type = 0;
    int32_t x = 0;
    int32_t y = 0;
    int32_t z = 0;
    int32_t data = 0;
    // Registry names: of the block state `data` names (a block breaking, a
    // brush finishing), and of the block at the position.
    std::string_view data_block;
    std::string_view block_here;
    bool waterlogged_here = false;
    // Where the camera is: some events wait for the sound to travel there.
    double camera[3] = {0.0, 0.0, 0.0};
};

// The sound the client plays for a level event, its volume and pitch drawn
// from the level's random in the game's order. Nothing for events that only
// make particles, and for the jukebox, which play_record handles.
std::optional<PlayRequest> level_event_sound(const LevelEvent& event, jmath::LegacyRandom& random);

// The three events every client hears wherever it is (a wither spawning, the
// dragon dying, the end portal opening): placed two blocks from the camera in
// the direction of the event.
std::optional<PlayRequest> global_level_event_sound(const LevelEvent& event);

// The sound event a jukebox_song entry plays: its own sound_event when the
// server sent the entry's data, else the vanilla music_disc.<song>.
std::string jukebox_sound_event(std::string_view song, const std::string* sound_event);

// Records, one per jukebox position.
class Jukeboxes {
public:
    void play(SoundEngine& engine, int32_t x, int32_t y, int32_t z, std::string_view event);
    void stop(SoundEngine& engine, int32_t x, int32_t y, int32_t z);
    void clear() { playing_.clear(); }

private:
    struct Record {
        int32_t x, y, z;
        uint64_t voice;
    };
    std::vector<Record> playing_;
};

// Plays what a level event sounds like, a record included: the song comes
// from the server's jukebox_song registry.
void play_level_event(SoundEngine& engine, Jukeboxes& jukeboxes, const LevelEvent& event,
                      bool global, const Registries& registries, jmath::LegacyRandom& random);

constexpr int32_t kLevelEventBlockBreak = 2001;
constexpr int32_t kLevelEventPlayRecord = 1010;
constexpr int32_t kLevelEventStopRecord = 1011;

} // namespace mcpp::audio
