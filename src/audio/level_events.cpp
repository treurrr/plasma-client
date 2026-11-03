#include "audio/level_events.hpp"

#include "audio/block_sounds.hpp"

#include <cmath>
#include <format>

namespace mcpp::audio {
namespace {

using Random = jmath::LegacyRandom;

// The spreads the game draws pitches from, each in its own order of draws.
float wobble(Random& r) {
    const float a = r.next_float();
    const float b = r.next_float();
    return (a - b) * 0.2f + 1.0f;
}
float hiss(Random& r) {
    const float a = r.next_float();
    const float b = r.next_float();
    return 2.6f + (a - b) * 0.8f;
}
float tenth(Random& r) { return r.next_float() * 0.1f + 0.9f; }

PlayRequest at_block(const LevelEvent& e, std::string_view sound, Category category, float volume,
                     float pitch, bool distance_delay) {
    PlayRequest request;
    request.event = std::string(sound);
    request.category = category;
    request.x = e.x + 0.5;
    request.y = e.y + 0.5;
    request.z = e.z + 0.5;
    request.volume = volume;
    request.pitch = pitch;
    if (distance_delay) {
        const double dx = e.camera[0] - request.x;
        const double dy = e.camera[1] - request.y;
        const double dz = e.camera[2] - request.z;
        const double squared = dx * dx + dy * dy + dz * dz;
        if (squared > 100.0) request.delay = static_cast<int32_t>(std::sqrt(squared) / 40.0 * 20.0);
    }
    return request;
}

bool is_air(std::string_view block) {
    return block.empty() || block == "minecraft:air" || block == "minecraft:cave_air" ||
           block == "minecraft:void_air";
}

} // namespace

std::optional<PlayRequest> level_event_sound(const LevelEvent& e, Random& r) {
    constexpr auto B = Category::Block;
    constexpr auto H = Category::Hostile;
    constexpr auto N = Category::Neutral;
    switch (e.type) {
    case 1000: return at_block(e, "block.dispenser.dispense", B, 1.0f, 1.0f, false);
    case 1001: return at_block(e, "block.dispenser.fail", B, 1.0f, 1.2f, false);
    case 1002: return at_block(e, "block.dispenser.launch", B, 1.0f, 1.2f, false);
    case 1004: return at_block(e, "entity.firework_rocket.shoot", N, 1.0f, 1.2f, false);
    case 1009:
        if (e.data == 0) return at_block(e, "block.fire.extinguish", B, 0.5f, hiss(r), false);
        if (e.data == 1) {
            const float a = r.next_float();
            const float b = r.next_float();
            return at_block(e, "entity.generic.extinguish_fire", B, 0.7f, 1.6f + (a - b) * 0.4f, false);
        }
        return std::nullopt;
    case 1015: return at_block(e, "entity.ghast.warn", H, 10.0f, wobble(r), false);
    case 1016: return at_block(e, "entity.ghast.shoot", H, 10.0f, wobble(r), false);
    case 1017: return at_block(e, "entity.ender_dragon.shoot", H, 10.0f, wobble(r), false);
    case 1018: return at_block(e, "entity.blaze.shoot", H, 2.0f, wobble(r), false);
    case 1019: return at_block(e, "entity.zombie.attack_wooden_door", H, 2.0f, wobble(r), false);
    case 1020: return at_block(e, "entity.zombie.attack_iron_door", H, 2.0f, wobble(r), false);
    case 1021: return at_block(e, "entity.zombie.break_wooden_door", H, 2.0f, wobble(r), false);
    case 1022: return at_block(e, "entity.wither.break_block", H, 2.0f, wobble(r), false);
    case 1024: return at_block(e, "entity.wither.shoot", H, 2.0f, wobble(r), false);
    case 1025: return at_block(e, "entity.bat.takeoff", N, 0.05f, wobble(r), false);
    case 1026: return at_block(e, "entity.zombie.infect", H, 2.0f, wobble(r), false);
    case 1027: return at_block(e, "entity.zombie_villager.converted", H, 2.0f, wobble(r), false);
    case 1029: return at_block(e, "block.anvil.destroy", B, 1.0f, tenth(r), false);
    case 1030: return at_block(e, "block.anvil.use", B, 1.0f, tenth(r), false);
    case 1031: return at_block(e, "block.anvil.land", B, 0.3f, tenth(r), false);
    case 1032: return PlayRequest::ambience("block.portal.travel", r.next_float() * 0.4f + 0.8f, 0.25f);
    case 1033: return at_block(e, "block.chorus_flower.grow", B, 1.0f, 1.0f, false);
    case 1034: return at_block(e, "block.chorus_flower.death", B, 1.0f, 1.0f, false);
    case 1035: return at_block(e, "block.brewing_stand.brew", B, 1.0f, 1.0f, false);
    case 1039: return at_block(e, "entity.phantom.bite", H, 0.3f, tenth(r), false);
    case 1040: return at_block(e, "entity.zombie.converted_to_drowned", H, 2.0f, wobble(r), false);
    case 1041: return at_block(e, "entity.husk.converted_to_zombie", H, 2.0f, wobble(r), false);
    case 1042: return at_block(e, "block.grindstone.use", B, 1.0f, tenth(r), false);
    case 1043: return at_block(e, "item.book.page_turn", B, 1.0f, tenth(r), false);
    case 1044: return at_block(e, "block.smithing_table.use", B, 1.0f, tenth(r), false);
    case 1045: return at_block(e, "block.pointed_dripstone.land", B, 2.0f, tenth(r), false);
    case 1046:
        return at_block(e, "block.pointed_dripstone.drip_lava_into_cauldron", B, 2.0f, tenth(r), false);
    case 1047:
        return at_block(e, "block.pointed_dripstone.drip_water_into_cauldron", B, 2.0f, tenth(r), false);
    case 1048: return at_block(e, "entity.skeleton.converted_to_stray", H, 2.0f, wobble(r), false);
    case 1049: return at_block(e, "block.crafter.craft", B, 1.0f, 1.0f, false);
    case 1050: return at_block(e, "block.crafter.fail", B, 1.0f, 1.0f, false);
    case 1051:
        return at_block(e, "entity.wind_charge.throw", B, 0.5f, 0.4f / (r.next_float() * 0.4f + 0.8f), false);
    case 1500:
        return at_block(e, e.data > 0 ? "block.composter.fill_success" : "block.composter.fill", B, 1.0f,
                        1.0f, false);
    case 1501: return at_block(e, "block.lava.extinguish", B, 0.5f, hiss(r), false);
    case 1502: return at_block(e, "block.redstone_torch.burnout", B, 0.5f, hiss(r), false);
    case 1503: return at_block(e, "block.end_portal_frame.fill", B, 1.0f, 1.0f, false);
    case 1505: return at_block(e, "item.bone_meal.use", B, 1.0f, 1.0f, false);
    case 2001: {
        if (is_air(e.data_block)) return std::nullopt;
        const auto sound = cue(BlockSounds::installed().find(e.data_block), BlockSoundKind::Break);
        if (!sound) return std::nullopt;
        return at_block(e, sound->event, B, sound->volume, sound->pitch, false);
    }
    case 2002:
    case 2007: return at_block(e, "entity.splash_potion.break", N, 1.0f, tenth(r), false);
    case 2006:
        if (e.data != 1) return std::nullopt;
        return at_block(e, "entity.dragon_fireball.explode", H, 1.0f, tenth(r), false);
    case 3000: {
        const float a = r.next_float();
        const float b = r.next_float();
        return at_block(e, "block.end_gateway.spawn", B, 10.0f, (1.0f + (a - b) * 0.2f) * 0.7f, false);
    }
    case 3001: return at_block(e, "entity.ender_dragon.growl", H, 64.0f, 0.8f + r.next_float() * 0.3f, false);
    case 3003: return at_block(e, "item.honeycomb.wax_on", B, 1.0f, 1.0f, false);
    case 3006: {
        const int32_t charge = e.data >> 6;
        if (charge <= 0) return at_block(e, "block.sculk.charge", B, 1.0f, 1.0f, false);
        if (r.next_float() >= 0.3f + static_cast<float>(charge) * 0.1f) return std::nullopt;
        const float volume = 0.15f + 0.02f * static_cast<float>(charge * charge) * r.next_float();
        const float pitch = 0.4f + 0.3f * static_cast<float>(charge) * r.next_float();
        return at_block(e, "block.sculk.charge", B, volume, pitch, false);
    }
    case 3007: {
        if (e.waterlogged_here) return std::nullopt;
        auto request = at_block(e, "block.sculk_shrieker.shriek", B, 2.0f, 0.6f + r.next_float() * 0.4f, false);
        request.y = e.y + 0.5;
        return request;
    }
    case 3008:
        if (e.data_block == "minecraft:suspicious_sand") {
            return at_block(e, "item.brush.brushing.sand", Category::Player, 1.0f, 1.0f, false);
        }
        if (e.data_block == "minecraft:suspicious_gravel") {
            return at_block(e, "item.brush.brushing.gravel", Category::Player, 1.0f, 1.0f, false);
        }
        return std::nullopt;
    case 3012: return at_block(e, "block.trial_spawner.spawn_mob", B, 1.0f, wobble(r), true);
    case 3013: return at_block(e, "block.trial_spawner.detect_player", B, 1.0f, wobble(r), true);
    case 3014: return at_block(e, "block.trial_spawner.eject_item", B, 1.0f, wobble(r), true);
    case 3015:
        if (e.block_here != "minecraft:vault") return std::nullopt;
        return at_block(e, "block.vault.activate", B, 1.0f, wobble(r), true);
    case 3016: return at_block(e, "block.vault.deactivate", B, 1.0f, wobble(r), true);
    case 3018: return at_block(e, "block.cobweb.place", B, 1.0f, wobble(r), true);
    case 3019: return at_block(e, "block.trial_spawner.detect_player", B, 1.0f, wobble(r), true);
    case 3020:
        return at_block(e, "block.trial_spawner.ominous_activate", B, e.data == 0 ? 0.3f : 1.0f, wobble(r), true);
    case 3021: return at_block(e, "block.trial_spawner.spawn_item", B, 1.0f, wobble(r), true);
    default: return std::nullopt;
    }
}

std::optional<PlayRequest> global_level_event_sound(const LevelEvent& e) {
    const char* sound = nullptr;
    float volume = 1.0f;
    switch (e.type) {
    case 1023: sound = "entity.wither.spawn"; break;
    case 1038: sound = "block.end_portal.spawn"; break;
    case 1028:
        sound = "entity.ender_dragon.death";
        volume = 5.0f;
        break;
    default: return std::nullopt;
    }
    double dx = e.x + 0.5 - e.camera[0];
    double dy = e.y + 0.5 - e.camera[1];
    double dz = e.z + 0.5 - e.camera[2];
    const double length = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (length < 1.0e-5) {
        dx = dy = dz = 0.0;
    } else {
        dx /= length;
        dy /= length;
        dz /= length;
    }
    PlayRequest request;
    request.event = sound;
    request.category = Category::Hostile;
    request.x = e.camera[0] + dx * 2.0;
    request.y = e.camera[1] + dy * 2.0;
    request.z = e.camera[2] + dz * 2.0;
    request.volume = volume;
    return request;
}

std::string jukebox_sound_event(std::string_view song, const std::string* sound_event) {
    if (sound_event != nullptr && !sound_event->empty()) return std::string(bare_name(*sound_event));
    return std::format("music_disc.{}", bare_name(song));
}

void Jukeboxes::play(SoundEngine& engine, int32_t x, int32_t y, int32_t z, std::string_view event) {
    stop(engine, x, y, z);
    PlayRequest request;
    request.event = std::string(event);
    request.category = Category::Record;
    request.x = x + 0.5;
    request.y = y + 0.5;
    request.z = z + 0.5;
    request.volume = 4.0f;
    if (const uint64_t voice = engine.play(request)) playing_.push_back({x, y, z, voice});
}

void Jukeboxes::stop(SoundEngine& engine, int32_t x, int32_t y, int32_t z) {
    std::erase_if(playing_, [&](const Record& record) {
        if (record.x != x || record.y != y || record.z != z) return false;
        engine.stop_voice(record.voice);
        return true;
    });
}

void play_level_event(SoundEngine& engine, Jukeboxes& jukeboxes, const LevelEvent& event,
                      bool global, const Registries& registries, jmath::LegacyRandom& random) {
    if (global) {
        if (const auto request = global_level_event_sound(event)) {
            jmath::LegacyRandom seeded(random.next_long());
            engine.play(*request, seeded);
        }
        return;
    }
    if (event.type == kLevelEventPlayRecord) {
        const auto* song = event.data >= 0
                               ? registries.at("minecraft:jukebox_song", static_cast<size_t>(event.data))
                               : nullptr;
        if (song == nullptr) return;
        const std::string* named = nullptr;
        if (song->data) {
            if (const auto* field = song->data->find("sound_event")) named = field->as_string();
        }
        jukeboxes.play(engine, event.x, event.y, event.z, jukebox_sound_event(song->name, named));
        return;
    }
    if (event.type == kLevelEventStopRecord) {
        jukeboxes.stop(engine, event.x, event.y, event.z);
        return;
    }
    // The pitch draws come first, then the seed the sound picks its variant by.
    if (const auto request = level_event_sound(event, random)) {
        jmath::LegacyRandom seeded(random.next_long());
        engine.play(*request, seeded);
    }
}

} // namespace mcpp::audio
