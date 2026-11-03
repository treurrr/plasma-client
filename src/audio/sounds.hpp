#pragma once
#include "audio/random.hpp"

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mcpp::audio {

// One line of a sound event in sounds.json: a file, or another event, with
// what it adds to the volume and pitch of whatever plays it.
struct SoundEntry {
    std::string name;
    bool is_event = false;
    float volume = 1.0f;
    float pitch = 1.0f;
    int32_t weight = 1;
    bool stream = false;
    int32_t attenuation_distance = 16;
    bool preload = false;
};

struct SoundEventDefinition {
    std::vector<SoundEntry> entries;
    std::string subtitle;
};

// What a sound event comes to once a variant is picked: one file, and the
// volume and pitch every level of the pick multiplied in.
struct ResolvedSound {
    std::string event;
    std::string file;   // the object store's name: minecraft/sounds/<path>.ogg
    float volume = 1.0f;
    float pitch = 1.0f;
    bool stream = false;
    int32_t attenuation_distance = 16;
    bool preload = false;
};

// Strips the minecraft: namespace, which sounds.json leaves out and the
// protocol writes in.
std::string_view bare_name(std::string_view name);

class SoundIndex {
public:
    // Throws ProtocolError on a file that is not sounds.json.
    void parse(std::string_view text);

    size_t size() const { return events_.size(); }
    const SoundEventDefinition* find(std::string_view event) const;
    // The event's weights summed: what nextInt is asked for.
    int32_t total_weight(std::string_view event) const;
    // Picks a variant the way the weights say, one draw per level, following
    // event references to the file they end in. Nothing when the event is not
    // defined, which is what intentionally_empty is.
    std::optional<ResolvedSound> pick(std::string_view event, LegacyRandom& random) const;

private:
    std::optional<ResolvedSound> pick(std::string_view event, LegacyRandom& random,
                                      int depth) const;
    std::map<std::string, SoundEventDefinition, std::less<>> events_;
};

} // namespace mcpp::audio
