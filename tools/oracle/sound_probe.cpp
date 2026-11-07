#include "audio/attenuation.hpp"
#include "audio/sounds.hpp"
#include "audio/voices.hpp"

#include "mcpp/assets/object_store.hpp"
#include "mcpp/jmath.hpp"
#include "mcpp/protocol_versions.hpp"

#include <bit>
#include <cstdint>
#include <iostream>
#include <string>

// The C++ half of SoundPickOracle: the same events, seeds and volumes in the
// same order, values only.
int main() {
    using namespace mcpp::audio;
    const char* events[] = {
        "music.creative", "music.game", "music.menu", "music.nether.basalt_deltas",
        "block.note_block.imitate.creeper", "block.note_block.harp", "ambient.basalt_deltas.additions",
        "ambient.cave", "ambient.crimson_forest.additions", "block.stone.step", "block.wood.break",
        "block.grass.step", "block.anvil.land", "block.bell.use", "entity.zombie.ambient",
        "entity.generic.explode", "ui.button.click", "block.cave_vines.pick_berries",
        "entity.player.levelup", "weather.rain", "music_disc.cat", "entity.ghast.scream",
        "block.portal.ambient", "entity.experience_orb.pickup", "block.hanging_sign.waxed_interact_fail",
        "intentionally_empty", "no.such.event",
    };
    const int64_t seeds[] = {0, 1, 42, -1, 123456789, 7046029254386353131LL, INT64_MIN,
                             -4490259591351732256LL};
    const float volumes[] = {1.0f, 0.25f, 2.0f, 4.0f, 0.15f};
    const float distances[] = {0.0f, 4.0f, 8.0f, 15.5f, 16.0f, 31.0f, 64.0f, 100.0f};

    const auto store = mcpp::assets::ObjectStore::for_version(mcpp::protocol::kTargetVersionName);
    const auto text = store ? store->read("minecraft/sounds.json") : std::nullopt;
    if (!text) {
        std::cerr << "no 1.21.11 assets in a launcher\n";
        return 1;
    }
    SoundIndex index;
    index.parse(std::string_view(reinterpret_cast<const char*>(text->data()), text->size()));
    index.prune([&](std::string_view name) { return store->find(name) != nullptr; });

    size_t n = 0;
    for (const char* event : events) {
        for (const int64_t seed : seeds) {
            const float volume = volumes[n++ % std::size(volumes)];
            std::cout << index.total_weight(event) << '\n';
            mcpp::jmath::LegacyRandom random(seed);
            const auto sound = index.pick(event, random);
            if (!sound) {
                std::cout << "none\n";
                continue;
            }
            std::string path = sound->file;
            path[path.find('/')] = ':';
            const float instance_volume = volume * sound->volume;
            const float instance_pitch = 1.0f * sound->pitch;
            const float range = audible_range(instance_volume, sound->attenuation_distance);
            std::cout << path << '\n';
            std::cout << (sound->stream ? "true" : "false") << '\n';
            std::cout << std::bit_cast<int32_t>(instance_volume) << '\n';
            std::cout << std::bit_cast<int32_t>(instance_pitch) << '\n';
            std::cout << std::bit_cast<int32_t>(range) << '\n';
            for (const float d : distances) {
                std::cout << std::bit_cast<int32_t>(clamp_volume(instance_volume) * distance_gain(d, range))
                          << '\n';
            }
        }
    }

    for (const char* effects : {"hurt", "thorns", "drowning", "burning", "poking", "freezing"}) {
        std::cout << damage_effect_sound(effects) << '\n';
    }
    for (const int64_t seed : seeds) {
        for (const bool baby : {false, true}) {
            mcpp::jmath::LegacyRandom random(seed);
            std::cout << std::bit_cast<int32_t>(voice_pitch(baby, random)) << '\n';
        }
    }
}
