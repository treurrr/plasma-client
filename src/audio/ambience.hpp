#pragma once
#include "audio/engine.hpp"

#include "mcpp/jmath.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

// Music and the sounds of the place the player is in. What plays where is the
// world's data: the background_music, music_volume and ambient_sounds
// environment attributes, which the world area evaluates and hands in here
// each tick. Until it does, the client passes the overworld's defaults.
namespace mcpp::audio {

struct Music {
    std::string event;
    int32_t min_delay = 12000;
    int32_t max_delay = 24000;
    bool replace_current = false;

    static Music menu() { return {"music.menu", 20, 600, true}; }
    static Music creative() { return {"music.creative", 12000, 24000, false}; }
    static Music credits() { return {"music.credits", 0, 0, true}; }
    static Music end_boss() { return {"music.dragon", 0, 0, true}; }
    static Music end() { return {"music.end", 6000, 24000, true}; }
    static Music game(std::string event = "music.game") { return {std::move(event), 12000, 24000, false}; }
};

// The background_music attribute.
struct BackgroundMusic {
    std::optional<Music> default_music;
    std::optional<Music> creative;
    std::optional<Music> underwater;

    static BackgroundMusic overworld() { return {Music::game(), Music::creative(), std::nullopt}; }
    std::optional<Music> select(bool creative_mode, bool under_water) const;
};

// What decides the music this tick, in the order the game asks.
struct MusicSituation {
    std::optional<Music> screen;   // a screen with music of its own: the title, the credits
    bool in_world = false;
    bool end_boss = false;         // in the end with the dragon's bar up
    BackgroundMusic background;
    bool creative = false;         // creative and allowed to fly
    bool under_water = false;
};
std::optional<Music> situational_music(const MusicSituation& situation);

// options.txt's musicFrequency.
enum class MusicFrequency { Default, Frequent, Constant };

class MusicManager {
public:
    MusicManager();
    explicit MusicManager(int64_t seed) : random_(seed) {}

    // music_volume is the attribute (1 on a screen with its own music).
    void tick(SoundEngine& engine, const std::optional<Music>& music, float music_volume);
    void stop(SoundEngine& engine);
    void set_frequency(MusicFrequency frequency, const std::optional<Music>& music);
    bool playing() const { return voice_ != 0; }
    // For the "Now playing" toast.
    std::function<void()> on_now_playing;
    std::function<void()> on_stopped;

private:
    int32_t next_delay(const std::optional<Music>& music);
    void start(SoundEngine& engine, const Music& music);
    bool fade(SoundEngine& engine, float target);

    jmath::LegacyRandom random_;
    MusicFrequency frequency_ = MusicFrequency::Default;
    uint64_t voice_ = 0;
    std::string event_;
    float gain_ = 1.0f;
    int32_t delay_ = 100;
    bool toast_shown_ = false;
};

// The ambient_sounds attribute.
struct AmbientMood {
    std::string event = "ambient.cave";
    int32_t tick_delay = 6000;
    int32_t block_search_extent = 8;
    double sound_position_offset = 2.0;
};
struct AmbientAddition {
    std::string event;
    double tick_chance = 0.0;
};
struct AmbientSounds {
    std::optional<std::string> loop;
    std::optional<AmbientMood> mood;
    std::vector<AmbientAddition> additions;

    // The game's own default when a biome says nothing.
    static AmbientSounds legacy_cave() { return {std::nullopt, AmbientMood{}, {}}; }
};

struct AmbientPlayer {
    double x = 0.0;
    double y = 0.0;   // the feet
    double z = 0.0;
    double eye_y = 0.0;
    bool under_water = false;
    // A bubble column in the player's box: whether it drags down.
    std::optional<bool> bubble_column;
    bool spectator = false;
};

// Sky and block light, 0 to 15, at a block.
using LightAt = std::function<int32_t(int32_t, int32_t, int32_t)>;

// The local player's three ambient handlers, in the game's order: under water,
// bubble columns, the biome's loop, mood and additions.
class Ambience {
public:
    void tick(SoundEngine& engine, const AmbientSounds& sounds, const AmbientPlayer& player,
              const LightAt& sky_light, const LightAt& block_light, jmath::LegacyRandom& level_random);
    void reset(SoundEngine& engine);
    float moodiness() const { return moodiness_; }

private:
    struct Fading {
        uint64_t voice = 0;
        std::string event;
        int32_t fade = 0;
        int32_t direction = 0;
    };

    // Under water.
    int32_t underwater_delay_ = 0;
    bool was_under_water_ = false;
    uint64_t underwater_loop_ = 0;
    int32_t underwater_fade_ = 0;
    std::vector<uint64_t> underwater_sounds_;
    // Bubble columns.
    bool was_in_column_ = false;
    bool first_tick_ = true;
    // The biome.
    std::vector<Fading> loops_;
    std::optional<std::string> previous_loop_;
    float moodiness_ = 0.0f;
};

} // namespace mcpp::audio
