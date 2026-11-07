#include "client/sound.hpp"

#include "client/game.hpp"

#include "audio/ambience.hpp"
#include "audio/engine.hpp"
#include "audio/footsteps.hpp"
#include "audio/level_events.hpp"
#include "audio/voices.hpp"
#include "client/breaking.hpp"

#include "mcpp/assets/object_store.hpp"
#include "mcpp/protocol_versions.hpp"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <format>
#include <iostream>
#include <numbers>

namespace client {

struct SoundRuntime {
    mcpp::audio::SoundEngine engine;
    std::optional<mcpp::assets::ObjectStore> store;
    mcpp::audio::Jukeboxes jukeboxes;
    mcpp::audio::Footsteps footsteps;
    mcpp::audio::MusicManager music;
    mcpp::audio::Ambience ambience;
    mcpp::jmath::LegacyRandom level_random{
        std::chrono::steady_clock::now().time_since_epoch().count()};
    bool open = false;
    bool have_body = false;
    double last[3] = {0.0, 0.0, 0.0};
};

namespace {

SoundRuntime* runtime(Game& game) {
    auto* sound = game.sound.runtime.get();
    return sound != nullptr && sound->open ? sound : nullptr;
}

// PLASMA_AUDIO=null plays to a device that consumes the stream and makes no
// sound; PLASMA_AUDIO=off opens no device at all.
mcpp::audio::Mixer::Options device_options(const std::string& device) {
    mcpp::audio::Mixer::Options options;
    options.device = device;
    if (const char* mode = std::getenv("PLASMA_AUDIO")) {
        options.null_output = std::string_view(mode) == "null";
        options.no_device = std::string_view(mode) == "off";
    }
    return options;
}

std::string block_name(Game& game, int32_t x, int32_t y, int32_t z) {
    if (!game.world.block_at || !game.table.is_open()) return "minecraft:air";
    const int32_t state = game.world.block_at(x, y, z);
    if (state < 0) return "minecraft:stone";
    return std::string(game.table.block_name(state));
}

bool in_block_tag(Game& game, std::string_view block, std::string_view tag) {
    const auto& session = *game.session;
    const int32_t id = session.registries().index_of("minecraft:block", block);
    return id >= 0 && session.tags().contains("minecraft:block", tag, id);
}

void play_at(Game& game, const std::string& event, mcpp::audio::Category category, double x, double y,
             double z, float volume, float pitch) {
    auto* sound = runtime(game);
    if (sound == nullptr) return;
    mcpp::audio::PlayRequest request;
    request.event = event;
    request.category = category;
    request.x = x;
    request.y = y;
    request.z = z;
    request.volume = volume;
    request.pitch = pitch;
    mcpp::jmath::LegacyRandom seeded(sound->level_random.next_long());
    sound->engine.play(request, seeded);
}

} // namespace

void sound_bind(Game& game, mcpp::SessionEvents& events) {
    auto sound = std::make_shared<SoundRuntime>();
    game.sound.runtime = sound;
    sound->store = mcpp::assets::ObjectStore::for_version(mcpp::protocol::kTargetVersionName);
    const auto text = sound->store ? sound->store->read("minecraft/sounds.json") : std::nullopt;
    if (!text) {
        std::cout << "- no sounds: the launcher has no 1.21.11 assets\n";
        return;
    }
    mcpp::audio::SoundIndex index;
    try {
        index.parse(std::string_view(reinterpret_cast<const char*>(text->data()), text->size()));
    } catch (const std::exception& error) {
        std::cout << "- no sounds: " << error.what() << '\n';
        return;
    }
    const auto* store = &*sound->store;
    index.prune([store](std::string_view name) { return store->find(name) != nullptr; });
    const bool device = sound->engine.open(
        std::move(index), [store](std::string_view name) { return store->read(name); }, device_options({}));
    sound->open = true;
    sound->engine.preload();
    std::cout << std::format("- sound: {} events, {}\n", sound->engine.index().size(),
                             device ? sound->engine.mixer().device_name() : std::string("no output device"));

    events.sound.on_sound = [&game](const mcpp::SessionEvents::Sound::Cue& cue) {
        auto* sound = runtime(game);
        if (sound == nullptr) return;
        mcpp::audio::PlayRequest request;
        request.event = cue.event;
        request.category = mcpp::audio::category_from_id(cue.category).value_or(mcpp::audio::Category::Master);
        request.x = cue.x;
        request.y = cue.y;
        request.z = cue.z;
        request.volume = cue.volume;
        request.pitch = cue.pitch;
        if (cue.entity >= 0) {
            const auto& entities = game.session->entities();
            const auto found = entities.find(cue.entity);
            if (found == entities.end()) return;
            request.entity = cue.entity;
            request.x = static_cast<float>(found->second.x);
            request.y = static_cast<float>(found->second.y);
            request.z = static_cast<float>(found->second.z);
        }
        mcpp::jmath::LegacyRandom seeded(cue.seed);
        sound->engine.play(request, seeded);
    };
    events.sound.on_stop = [&game](int32_t category, std::string_view name) {
        auto* sound = runtime(game);
        if (sound == nullptr) return;
        sound->engine.stop(category >= 0 ? mcpp::audio::category_from_id(category) : std::nullopt, name);
    };
    events.sound.on_level_event = [&game](int32_t type, int32_t x, int32_t y, int32_t z, int32_t data,
                                          bool global) {
        breaking_level_event(game, type, x, y, z, data);
        auto* sound = runtime(game);
        if (sound == nullptr) return;
        mcpp::audio::LevelEvent event;
        event.type = type;
        event.x = x;
        event.y = y;
        event.z = z;
        event.data = data;
        const std::string data_block =
            game.table.is_open() ? std::string(game.table.block_name(data)) : std::string();
        const std::string here = block_name(game, x, y, z);
        event.data_block = data_block;
        event.block_here = here;
        const auto& listener = sound->engine.listener();
        event.camera[0] = listener.x;
        event.camera[1] = listener.y;
        event.camera[2] = listener.z;
        mcpp::audio::play_level_event(sound->engine, sound->jukeboxes, event, global,
                                      game.session->registries(), sound->level_random);
    };
    // `LivingEntity.handleDamageEvent`: the hurt sound, at the voice pitch.
    // Only for the player at the keyboard: `Player.playSound` hands itself to
    // `ClientLevel.playSeededSound`, which is the one case that plays, and the
    // server's broadcast leaves that player out. Every other entity's hurt
    // sound arrives as a `sound` packet instead.
    events.entities.on_damage_type = [&game](int32_t id, std::string_view damage_type) {
        auto* sound = runtime(game);
        if (sound == nullptr || !game.session || id != game.session->entity_id()) return;
        const auto voice = mcpp::audio::player_hurt_voice(game.session->registries(), damage_type,
                                                          sound->level_random);
        const auto& body = game.player.body;
        play_at(game, std::string(voice.event), mcpp::audio::Category::Player, body.x, body.y, body.z,
                voice.volume, voice.pitch);
    };
    // `LivingEntity.handleEntityEvent`: status 3 is the death sound, played
    // client-side for the same reason the hurt sound is.
    auto entity_event = events.entities.on_event;
    events.entities.on_event = [&game, entity_event](int32_t id, uint8_t status) {
        if (entity_event) entity_event(id, status);
        auto* sound = runtime(game);
        if (sound == nullptr || status != 3) return;
        if (!game.session || id != game.session->entity_id()) return;
        const auto voice = mcpp::audio::player_death_voice(sound->level_random);
        const auto& body = game.player.body;
        play_at(game, std::string(voice.event), mcpp::audio::Category::Player, body.x, body.y, body.z,
                voice.volume, voice.pitch);
    };
}

void sound_start(Game&) {}

void sound_input(Game&) {}

void sound_tick(Game& game) {
    auto* sound = runtime(game);
    if (sound == nullptr) return;
    auto& engine = sound->engine;

    const auto& camera = game.camera;
    const double fx = camera.target[0] - camera.eye[0];
    const double fy = camera.target[1] - camera.eye[1];
    const double fz = camera.target[2] - camera.eye[2];
    mcpp::audio::Listener listener;
    listener.x = camera.eye[0];
    listener.y = camera.eye[1];
    listener.z = camera.eye[2];
    listener.yaw = static_cast<float>(std::atan2(-fx, fz) * 180.0 / std::numbers::pi);
    listener.pitch = static_cast<float>(-std::atan2(fy, std::hypot(fx, fz)) * 180.0 / std::numbers::pi);
    engine.set_listener(listener);

    if (!game.tick_due) {
        engine.pump();
        return;
    }

    const auto& session = *game.session;
    const auto& body = game.player.body;
    if (!sound->have_body) {
        sound->footsteps.reset();
        sound->have_body = true;
        sound->last[0] = body.x;
        sound->last[1] = body.y;
        sound->last[2] = body.z;
    }
    mcpp::audio::StepInput input;
    input.x = body.x;
    input.y = body.y;
    input.z = body.z;
    input.moved_x = body.x - sound->last[0];
    input.moved_y = body.y - sound->last[1];
    input.moved_z = body.z - sound->last[2];
    // A move no tick of walking covers is a teleport.
    if (input.moved_x * input.moved_x + input.moved_z * input.moved_z > 64.0) {
        sound->footsteps.reset();
        input.moved_x = input.moved_y = input.moved_z = 0.0;
    }
    sound->last[0] = body.x;
    sound->last[1] = body.y;
    sound->last[2] = body.z;
    input.on_ground = body.on_ground;
    input.flying = game.player.flying;
    input.may_fly = session.abilities().may_fly;
    mcpp::audio::StepWorld world;
    world.block_at = [&game](int32_t x, int32_t y, int32_t z) { return block_name(game, x, y, z); };
    world.in_tag = [&game](std::string_view block, std::string_view tag) {
        return in_block_tag(game, block, tag);
    };
    world.collides = [&game](int32_t x, int32_t y, int32_t z) {
        const int32_t state = game.world.block_at ? game.world.block_at(x, y, z) : -1;
        return state < 0 || game.table.full_cube(state);
    };
    for (const auto& step : sound->footsteps.tick(input, mcpp::audio::BlockSounds::installed(), world,
                                                  sound->level_random)) {
        play_at(game, step.event, mcpp::audio::Category::Player, step.x, step.y, step.z, step.volume,
                step.pitch);
    }

    mcpp::audio::MusicSituation situation;
    situation.in_world = true;
    const auto dimension = session.dimension().name;
    if (dimension == "minecraft:overworld") {
        situation.background = mcpp::audio::BackgroundMusic::overworld();
    } else if (dimension == "minecraft:the_end") {
        situation.background.default_music = mcpp::audio::Music::end();
    }
    situation.creative = session.abilities().instant_build && session.abilities().may_fly;
    sound->music.tick(engine, mcpp::audio::situational_music(situation), 1.0f);

    mcpp::audio::AmbientPlayer player;
    player.x = body.x;
    player.y = body.y;
    player.z = body.z;
    player.eye_y = body.y + body.eye_height();
    const auto ambient = dimension == "minecraft:overworld" ? mcpp::audio::AmbientSounds::legacy_cave()
                                                            : mcpp::audio::AmbientSounds{};
    const auto light = [&game](bool sky_layer) {
        return [&game, sky_layer](int32_t x, int32_t y, int32_t z) -> int32_t {
            auto& area = game.world;
            std::lock_guard lock(area.world_guard);
            const auto found = area.world.find({x >> 4, z >> 4});
            if (found == area.world.end()) return 0;
            const int32_t index = found->second.index_of(y);
            if (index < 0 || index >= static_cast<int32_t>(found->second.sections.size()) * 16) {
                return sky_layer ? 15 : 0;
            }
            return sky_layer ? found->second.sky_light_at(x & 15, index, z & 15)
                             : found->second.block_light_at(x & 15, index, z & 15);
        };
    };
    const auto sky = light(true);
    const auto block = light(false);
    sound->ambience.tick(engine, ambient, player, sky, block, sound->level_random);

    engine.update([&game](int32_t id) -> std::optional<std::array<double, 3>> {
        const auto& entities = game.session->entities();
        const auto found = entities.find(id);
        if (found == entities.end()) return std::nullopt;
        return std::array<double, 3>{found->second.x, found->second.y, found->second.z};
    });
}

void sound_draw(Game&) {}

void sound_stop(Game& game) {
    if (auto* sound = runtime(game)) {
        const auto mixing = sound->engine.mixer().stats();
        const auto stats = sound->engine.stats();
        if (mixing.callbacks > 0) {
            std::cout << std::format(
                "- sound: {:.2f} us a mix on average, worst {:.2f} us; {} sounds decoded, {} KiB cached\n",
                static_cast<double>(mixing.nanoseconds) / static_cast<double>(mixing.callbacks) / 1000.0,
                static_cast<double>(mixing.worst_nanoseconds) / 1000.0, stats.decodes, stats.cache_bytes / 1024);
        }
        sound->engine.close();
        sound->open = false;
    }
}

void sound_ui(Game& game, std::string_view event, float pitch, float volume) {
    if (auto* sound = runtime(game)) sound->engine.play(mcpp::audio::PlayRequest::ui(event, pitch, volume));
}

void sound_block(Game& game, int32_t x, int32_t y, int32_t z, int32_t state, mcpp::audio::BlockSoundKind kind) {
    auto* sound = runtime(game);
    if (sound == nullptr || !game.table.is_open()) return;
    const std::string name(game.table.block_name(state));
    if (kind == mcpp::audio::BlockSoundKind::Break) {
        mcpp::audio::LevelEvent event;
        event.type = mcpp::audio::kLevelEventBlockBreak;
        event.x = x;
        event.y = y;
        event.z = z;
        event.data = state;
        event.data_block = name;
        mcpp::audio::play_level_event(sound->engine, sound->jukeboxes, event, false,
                                      game.session->registries(), sound->level_random);
        return;
    }
    const auto cue = mcpp::audio::cue(mcpp::audio::BlockSounds::installed().find(name), kind);
    if (!cue) return;
    play_at(game, cue->event, mcpp::audio::Category::Block, x + 0.5, y + 0.5, z + 0.5, cue->volume, cue->pitch);
}

mcpp::audio::CategoryVolumes* sound_volumes(Game& game) {
    auto* sound = game.sound.runtime.get();
    return sound != nullptr ? &sound->engine.volumes : nullptr;
}

std::vector<std::string> sound_devices() { return mcpp::audio::Mixer::devices(); }

void sound_set_device(Game& game, const std::string& device) {
    if (auto* sound = runtime(game)) sound->engine.reopen(device_options(device));
}

} // namespace client
