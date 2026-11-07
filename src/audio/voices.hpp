#pragma once
// What a living thing is heard saying when it is hit and when it dies.
//
// Almost none of this is the client's work. The game plays a hurt or death
// sound from `LivingEntity.handleDamageEvent` and `handleEntityEvent`, but
// both go through `Entity.playSound`, which hands `null` to
// `ClientLevel.playSeededSound`, and that plays nothing unless the entity it
// is given is the player at the keyboard. For every mob and every other
// player the server has already played the same sound into a `sound` packet,
// which the sound area plays like any other.
//
// `Player.playSound` is the one override that hands itself over instead, and
// the server's broadcast leaves that player out. So the player's own hurt and
// death sound exist nowhere but here, which is why this is only about them.
#include "mcpp/jmath.hpp"
#include "mcpp/registries.hpp"

#include <string_view>

namespace mcpp::audio {

// One sound a body makes about itself, ready to be placed where it stands.
struct Voice {
    std::string_view event;
    float volume = 1.0f;
    float pitch = 1.0f;
};

// `DamageEffects`: which of the five hurt sounds a damage type asks for.
// Takes the `effects` field of a `minecraft:damage_type` registry entry;
// anything else, the field left out included, is the game's own default.
std::string_view damage_effect_sound(std::string_view effects);

// The `effects` the server gave a damage type, empty when it gave none. The
// field is optional and defaults to hurt, so an empty one lands there anyway.
std::string_view damage_type_effects(const Registries& registries, std::string_view damage_type);

// `LivingEntity.getVoicePitch`: two draws either side of one, a baby higher.
// Both draws are always made, in this order.
float voice_pitch(bool baby, jmath::LegacyRandom& random);

// `LivingEntity.handleDamageEvent`, `Player.getHurtSound`,
// `LivingEntity.getSoundVolume`: the hurt sound the damage type picks, at the
// volume and the pitch that method plays it with.
Voice player_hurt_voice(const Registries& registries, std::string_view damage_type,
                        jmath::LegacyRandom& random);

// `LivingEntity.handleEntityEvent`, `Player.getDeathSound`: status 3.
Voice player_death_voice(jmath::LegacyRandom& random);

} // namespace mcpp::audio
