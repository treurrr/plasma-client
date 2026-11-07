#include "audio/voices.hpp"

namespace mcpp::audio {

// `DamageEffects.sound`: the sound each of the six effects carries. Two of
// them carry the same one, which is why hurt and thorns sound alike.
std::string_view damage_effect_sound(std::string_view effects) {
    if (effects == "drowning") return "entity.player.hurt_drown";
    if (effects == "burning") return "entity.player.hurt_on_fire";
    if (effects == "poking") return "entity.player.hurt_sweet_berry_bush";
    if (effects == "freezing") return "entity.player.hurt_freeze";
    return "entity.player.hurt";
}

std::string_view damage_type_effects(const Registries& registries, std::string_view damage_type) {
    if (damage_type.empty()) return {};
    const int32_t index = registries.index_of("minecraft:damage_type", damage_type);
    if (index < 0) return {};
    const auto* entry = registries.at("minecraft:damage_type", static_cast<size_t>(index));
    if (entry == nullptr || !entry->data.has_value()) return {};
    const auto* value = entry->data->find("effects");
    if (value == nullptr) return {};
    const std::string* name = value->as_string();
    return name != nullptr ? std::string_view(*name) : std::string_view();
}

// `LivingEntity.getVoicePitch`: the difference of two draws, around 1 or 1.5.
float voice_pitch(bool baby, jmath::LegacyRandom& random) {
    const float first = random.next_float();
    const float second = random.next_float();
    return (first - second) * 0.2f + (baby ? 1.5f : 1.0f);
}

// `LivingEntity.handleDamageEvent`, `Player.getHurtSound`: the damage type's
// effect decides the sound; a player is never louder or quieter and never a
// baby, so the volume is one and the pitch sits around one.
Voice player_hurt_voice(const Registries& registries, std::string_view damage_type,
                        jmath::LegacyRandom& random) {
    Voice voice;
    voice.event = damage_effect_sound(damage_type_effects(registries, damage_type));
    voice.volume = 1.0f;
    voice.pitch = voice_pitch(false, random);
    return voice;
}

// `LivingEntity.handleEntityEvent`, `Player.getDeathSound`: one sound,
// whatever killed them, at the same volume and pitch as a hurt.
Voice player_death_voice(jmath::LegacyRandom& random) {
    Voice voice;
    voice.event = "entity.player.death";
    voice.volume = 1.0f;
    voice.pitch = voice_pitch(false, random);
    return voice;
}

} // namespace mcpp::audio
