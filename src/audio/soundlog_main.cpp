// plasma-soundlog: records what a server makes heard, walks a second player
// past a listener, and replays a recording through the sound engine with no
// output device, writing down every voice instead of playing it.
#include "audio/block_sounds.hpp"
#include "audio/engine.hpp"
#include "audio/footsteps.hpp"
#include "audio/level_events.hpp"
#include "audio/voices.hpp"

#include "mcpp/assets/object_store.hpp"
#include "mcpp/protocol_versions.hpp"
#include "mcpp/session.hpp"
#include "mcpp/world/block_table.hpp"
#include "mcpp/world/chunk.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <format>
#include <fstream>
#include <iostream>
#include <map>
#include <numbers>
#include <string>
#include <thread>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

std::atomic<bool> g_stop{false};
void stop_on_signal(int) { g_stop.store(true); }

struct Address {
    std::string host;
    uint16_t port = 25565;
};

Address parse_address(const std::string& target) {
    Address address;
    const auto colon = target.rfind(':');
    address.host = target.substr(0, colon);
    if (colon != std::string::npos) address.port = static_cast<uint16_t>(std::stoi(target.substr(colon + 1)));
    return address;
}

mcpp::SessionOptions offline_options(const std::string& target, const std::string& name) {
    const auto address = parse_address(target);
    mcpp::SessionOptions options;
    options.host = address.host;
    options.port = address.port;
    options.username = name;
    return options;
}

// Pumps until the socket is quiet, so a tick's position goes out on time.
bool drain(mcpp::Session& session) {
    while (session.has_data()) {
        try {
            if (!session.pump()) return false;
        } catch (const mcpp::TimeoutError&) {
            break;
        }
    }
    return true;
}

// The listener: stands where the server puts it and records everything, with
// the moment each packet arrived beside the recording, which has no clock.
int do_record(const std::string& target, const std::string& name, const std::string& file,
              double seconds) {
    auto options = offline_options(target, name);
    options.record = file;
    double at[3] = {0, 0, 0};
    float facing[2] = {0, 0};
    bool placed = false;
    mcpp::SessionEvents events;
    events.on_status = [](std::string_view line) { std::cout << "- " << line << '\n'; };
    events.player.on_position = [&](double x, double y, double z, float yaw, float pitch) {
        at[0] = x;
        at[1] = y;
        at[2] = z;
        facing[0] = yaw;
        facing[1] = pitch;
        placed = true;
        std::cout << std::format("- placed at {:.2f} {:.2f} {:.2f}\n", x, y, z);
    };
    size_t sounds = 0;
    events.sound.on_sound = [&](const mcpp::SessionEvents::Sound::Cue&) { ++sounds; };

    mcpp::Session session(options, events);
    session.connect();
    std::ofstream times(file + ".times");
    const auto start = Clock::now();
    auto next_tick = start;
    std::signal(SIGINT, stop_on_signal);
    std::signal(SIGTERM, stop_on_signal);
    while (!g_stop.load() && Clock::now() - start < std::chrono::duration<double>(seconds)) {
        while (session.has_data()) {
            try {
                if (!session.pump()) break;
            } catch (const mcpp::TimeoutError&) {
                break;
            }
            times << std::format("{:.4f}\n",
                                 std::chrono::duration<double>(Clock::now() - start).count());
        }
        if (session.state() == mcpp::SessionState::Closed) break;
        if (Clock::now() >= next_tick) {
            next_tick += std::chrono::milliseconds(50);
            if (placed) session.send_position(at[0], at[1], at[2], facing[0], facing[1], true);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    session.close();
    std::cout << std::format("- {} sound packets recorded to {}\n", sounds, file);
    return 0;
}

// The walker: once placed, waits for a teleport to settle, then walks a
// straight line on flat ground at walking speed, or drops from where it is.
int do_walk(const std::string& target, const std::string& name, double wait, double dx,
            double dz, double blocks, double speed, double drop) {
    auto options = offline_options(target, name);
    double at[3] = {0, 0, 0};
    bool placed = false;
    auto placed_at = Clock::now();
    mcpp::SessionEvents events;
    events.on_status = [](std::string_view line) { std::cout << "- " << line << '\n'; };
    events.player.on_position = [&](double x, double y, double z, float, float) {
        at[0] = x;
        at[1] = y;
        at[2] = z;
        placed = true;
        placed_at = Clock::now();
        std::cout << std::format("- {} placed at {:.2f} {:.2f} {:.2f}\n", name, x, y, z);
    };
    size_t heard = 0;
    events.sound.on_sound = [&](const mcpp::SessionEvents::Sound::Cue& cue) {
        ++heard;
        std::cout << std::format("  self hears {} at {:.2f} {:.2f} {:.2f} v{:.3f} p{:.3f}\n",
                                 cue.event, cue.x, cue.y, cue.z, cue.volume, cue.pitch);
    };
    mcpp::Session session(options, events);
    session.connect();

    const double length = std::hypot(dx, dz);
    const float yaw = static_cast<float>(std::atan2(-dx, dz) * 180.0 / std::numbers::pi);
    double walked = 0.0;
    double fallen = 0.0;
    double fall_speed = 0.0;
    bool walking = false;
    auto next_tick = Clock::now();
    const auto give_up = Clock::now() + std::chrono::seconds(120);
    while (Clock::now() < give_up) {
        if (!drain(session) || session.state() == mcpp::SessionState::Closed) break;
        if (Clock::now() < next_tick) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }
        next_tick += std::chrono::milliseconds(50);
        if (!placed) continue;
        if (!walking) {
            if (Clock::now() - placed_at < std::chrono::duration<double>(wait)) {
                session.send_position(at[0], at[1], at[2], yaw, 0.0f, drop <= 0.0);
                continue;
            }
            walking = true;
            std::cout << std::format("- {} sets off from {:.2f} {:.2f} {:.2f}\n", name, at[0],
                                     at[1], at[2]);
        }
        if (drop > 0.0) {
            // Gravity the way a falling player is sent: 0.08 a tick, 2% drag.
            fall_speed = (fall_speed + 0.08) * 0.98;
            const double step = std::min(fall_speed, drop - fallen);
            fallen += step;
            at[1] -= step;
            const bool landed = fallen >= drop - 1e-9;
            session.send_position(at[0], at[1], at[2], yaw, 0.0f, landed);
            if (landed) {
                for (int i = 0; i < 40; ++i) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    drain(session);
                    session.send_position(at[0], at[1], at[2], yaw, 0.0f, true);
                }
                break;
            }
            continue;
        }
        const double step = std::min(speed, blocks - walked);
        walked += step;
        at[0] += dx / length * step;
        at[2] += dz / length * step;
        session.send_position(at[0], at[1], at[2], yaw, 0.0f, true);
        if (walked >= blocks - 1e-9) {
            for (int i = 0; i < 20; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                drain(session);
                session.send_position(at[0], at[1], at[2], yaw, 0.0f, true);
            }
            break;
        }
    }
    std::cout << std::format("- {} walked {:.2f} blocks, fell {:.2f}, heard {} of its own\n",
                             name, walked, fallen, heard);
    session.close();
    return 0;
}


// Plays a recording back through the engine with no device, every voice
// logged: when it started, how loud on each side, how high, from where.
int do_replay(const std::string& file, bool realtime) {
    mcpp::world::BlockTable table;
    table.open(mcpp::world::BlockTable::default_path(mcpp::protocol::kTargetProtocol));
    const auto store = mcpp::assets::ObjectStore::for_version(mcpp::protocol::kTargetVersionName);
    const auto text = store ? store->read("minecraft/sounds.json") : std::nullopt;
    if (!text) {
        std::cerr << "no sounds.json: the launcher has no 1.21.11 assets\n";
        return 1;
    }
    mcpp::audio::SoundIndex index;
    index.parse(std::string_view(reinterpret_cast<const char*>(text->data()), text->size()));

    std::vector<double> times;
    {
        std::ifstream in(file + ".times");
        for (double t = 0; in >> t;) times.push_back(t);
    }

    mcpp::audio::SoundEngine engine;
    mcpp::audio::Mixer::Options mixer_options;
    mixer_options.null_output = realtime;
    mixer_options.no_device = !realtime;
    index.prune([&](std::string_view name) { return store->find(name) != nullptr; });
    const bool device = engine.open(std::move(index), [&](std::string_view name) { return store->read(name); },
                                    mixer_options);
    std::cout << std::format("# mixer: {}\n", realtime && device ? "null device (" + engine.mixer().device_name() + ")"
                                                                  : std::string("no device, mixed by hand"));
    engine.on_log = [](const mcpp::audio::VoiceLog& line) {
        std::cout << std::format(
            "{:8.3f} {:<7} #{:<3} {:<34} {:<8} at {:9.2f} {:7.2f} {:9.2f} d {:5.2f}/{:5.1f} gain {:.4f} "
            "L {:.4f} R {:.4f} pitch {:.4f}{}{} {}\n",
            line.time, line.what, line.id, line.event, mcpp::audio::category_name(line.category),
            line.x, line.y, line.z, line.distance, line.range, line.gain, line.left, line.right,
            line.pitch, line.relative ? " relative" : "", line.stream ? " stream" : "",
            line.file);
    };

    std::map<std::pair<int32_t, int32_t>, mcpp::world::Chunk> world;
    std::map<int32_t, mcpp::Entity> entities;
    mcpp::jmath::LegacyRandom client_random(1);
    mcpp::audio::Jukeboxes jukeboxes;
    mcpp::Session* session_ptr = nullptr;
    double now = 0.0;

    mcpp::SessionOptions options;
    options.replay = file;
    options.username = "PlasmaF";
    mcpp::SessionEvents events;
    events.on_status = [](std::string_view line) { std::cout << "# " << line << '\n'; };
    events.world.on_chunk = [&](const mcpp::world::Chunk& chunk) {
        world.insert_or_assign({chunk.x, chunk.z}, chunk);
    };
    events.world.on_block_change = [&](int32_t x, int32_t y, int32_t z, int32_t state) {
        const auto found = world.find({x >> 4, z >> 4});
        if (found != world.end()) {
            found->second.set_block_state(x & 15, found->second.index_of(y), z & 15, state);
        }
    };
    std::array<double, 3> self{0.0, 0.0, 0.0};
    size_t predicted_voices = 0;
    events.player.on_position = [&](double x, double y, double z, float yaw, float pitch) {
        self = {x, y, z};
        engine.set_listener({x, y + 1.62, z, yaw, pitch});
        std::cout << std::format("{:8.3f} listener at {:.2f} {:.2f} {:.2f} yaw {:.1f}\n", now, x,
                                 y + 1.62, z, yaw);
    };
    // The player's own voice, which the server never sends and the client is
    // the only one to play. Written down as a model line, like the footsteps.
    const auto voice = [&](const mcpp::audio::Voice& line) {
        mcpp::audio::PlayRequest request;
        request.event = std::string(line.event);
        request.category = mcpp::audio::Category::Player;
        request.x = self[0];
        request.y = self[1];
        request.z = self[2];
        request.volume = line.volume;
        request.pitch = line.pitch;
        ++predicted_voices;
        std::cout << std::format("{:8.3f} voice   {} at {:.3f} {:.3f} {:.3f} volume {:.4f} pitch {:.4f}\n",
                                 now, request.event, request.x, request.y, request.z, line.volume,
                                 line.pitch);
        engine.play(request, client_random);
    };
    events.entities.on_damage_type = [&](int32_t id, std::string_view damage_type) {
        if (id != session_ptr->entity_id()) return;
        std::cout << std::format("{:8.3f} packet  damage_event type '{}'\n", now, damage_type);
        voice(mcpp::audio::player_hurt_voice(session_ptr->registries(), damage_type, client_random));
    };
    events.entities.on_event = [&](int32_t id, uint8_t status) {
        if (status != 3 || id != session_ptr->entity_id()) return;
        std::cout << std::format("{:8.3f} packet  entity_event 3\n", now);
        voice(mcpp::audio::player_death_voice(client_random));
    };
    // Other players walk by the same rules the client uses for its own feet,
    // so the server's footsteps for them are what the model has to match.
    std::map<int32_t, mcpp::audio::Footsteps> walkers;
    size_t predicted = 0;
    const auto block_name_at = [&](int32_t x, int32_t y, int32_t z) -> std::string {
        const auto found = world.find({x >> 4, z >> 4});
        if (found == world.end() || !table.is_open()) return "minecraft:air";
        const int32_t row = found->second.index_of(y);
        if (row < 0 || row >= static_cast<int32_t>(found->second.sections.size()) * 16) {
            return "minecraft:air";
        }
        return std::string(table.block_name(found->second.block_state(x & 15, row, z & 15)));
    };
    const auto in_tag = [&](std::string_view block, std::string_view tag) {
        const int32_t id = session_ptr->registries().index_of("minecraft:block", block);
        return id >= 0 && session_ptr->tags().contains("minecraft:block", tag, id);
    };
    const auto state_at = [&](int32_t x, int32_t y, int32_t z) -> int32_t {
        const auto found = world.find({x >> 4, z >> 4});
        if (found == world.end()) return 0;
        const int32_t row = found->second.index_of(y);
        if (row < 0 || row >= static_cast<int32_t>(found->second.sections.size()) * 16) return 0;
        return found->second.block_state(x & 15, row, z & 15);
    };
    mcpp::audio::StepWorld step_world;
    step_world.block_at = block_name_at;
    step_world.in_tag = in_tag;
    // Full cubes only, as the client's physics has it until collision shapes land.
    step_world.collides = [&](int32_t x, int32_t y, int32_t z) {
        return table.is_open() && table.full_cube(state_at(x, y, z));
    };
    std::map<int32_t, std::array<double, 3>> last_seen;
    mcpp::jmath::LegacyRandom walker_random(7);
    const auto feed = [&](const mcpp::Entity& entity, bool teleported) {
        const int32_t player_type =
            session_ptr->registries().index_of("minecraft:entity_type", "minecraft:player");
        if (entity.type != player_type || entity.id == session_ptr->entity_id()) return;
        auto& walker = walkers[entity.id];
        auto& last = last_seen[entity.id];
        if (teleported) {
            walker.reset();
            last = {entity.x, entity.y, entity.z};
        }
        mcpp::audio::StepInput input;
        input.x = entity.x;
        input.y = entity.y;
        input.z = entity.z;
        input.moved_x = entity.x - last[0];
        input.moved_y = entity.y - last[1];
        input.moved_z = entity.z - last[2];
        input.on_ground = entity.on_ground;
        last = {entity.x, entity.y, entity.z};
        const auto sounds =
            walker.tick(input, mcpp::audio::BlockSounds::installed(), step_world, walker_random);
        for (const auto& sound : sounds) {
            ++predicted;
            std::cout << std::format("{:8.3f} model   {} at {:.3f} {:.3f} {:.3f} volume {:.4f} pitch {:.4f}\n",
                                     now, sound.event, sound.x, sound.y, sound.z, sound.volume,
                                     sound.pitch);
        }
    };
    events.entities.on_add = [&](const mcpp::Entity& entity) {
        entities[entity.id] = entity;
        feed(entity, true);
    };
    events.entities.on_move = [&](const mcpp::Entity& entity) {
        // The packets do not say which move was a teleport; no walk covers
        // two blocks in a tick.
        const auto was = entities.find(entity.id);
        const bool jumped = was != entities.end() &&
                            std::hypot(entity.x - was->second.x, entity.z - was->second.z) > 2.0;
        entities[entity.id] = entity;
        feed(entity, jumped);
    };
    events.entities.on_remove = [&](int32_t id) { entities.erase(id); };
    events.sound.on_sound = [&](const mcpp::SessionEvents::Sound::Cue& cue) {
        mcpp::audio::PlayRequest request;
        request.event = cue.event;
        request.category = mcpp::audio::category_from_id(cue.category).value_or(mcpp::audio::Category::Master);
        request.x = cue.x;
        request.y = cue.y;
        request.z = cue.z;
        request.volume = cue.volume;
        request.pitch = cue.pitch;
        request.entity = cue.entity;
        if (cue.entity >= 0) {
            if (const auto found = entities.find(cue.entity); found != entities.end()) {
                request.x = found->second.x;
                request.y = found->second.y;
                request.z = found->second.z;
            }
        }
        std::cout << std::format("{:8.3f} packet  {} {} at {:.3f} {:.3f} {:.3f} volume {:.4f} pitch {:.4f} seed {}{}\n",
                                 now, cue.entity >= 0 ? "sound_entity" : "sound", cue.event,
                                 request.x, request.y, request.z, cue.volume, cue.pitch, cue.seed,
                                 cue.entity >= 0 ? std::format(" entity {}", cue.entity) : "");
        mcpp::jmath::LegacyRandom seeded(cue.seed);
        engine.play(request, seeded);
    };
    events.sound.on_stop = [&](int32_t category, std::string_view name) {
        std::cout << std::format("{:8.3f} packet  stop_sound category {} name '{}'\n", now, category, name);
        engine.stop(category >= 0 ? mcpp::audio::category_from_id(category) : std::nullopt, name);
    };
    events.sound.on_level_event = [&](int32_t type, int32_t x, int32_t y, int32_t z, int32_t data, bool global) {
        std::cout << std::format("{:8.3f} packet  level_event {} at {} {} {} data {}{}\n", now, type, x, y, z,
                                 data, global ? " global" : "");
        mcpp::audio::LevelEvent event;
        event.type = type;
        event.x = x;
        event.y = y;
        event.z = z;
        event.data = data;
        const std::string data_block = table.is_open() ? std::string(table.block_name(data)) : std::string();
        const std::string here = block_name_at(x, y, z);
        event.data_block = data_block;
        event.block_here = here;
        event.camera[0] = engine.listener().x;
        event.camera[1] = engine.listener().y;
        event.camera[2] = engine.listener().z;
        mcpp::audio::play_level_event(engine, jukeboxes, event, global, session_ptr->registries(),
                                      client_random);
    };

    mcpp::Session session(options, events);
    session_ptr = &session;
    session.connect();
    const auto start = Clock::now();
    size_t packet = 0;
    double next_tick = 0.0;
    std::vector<float> scratch;
    double mixed_until = 0.0;
    const auto advance = [&](double until) {
        while (next_tick <= until) {
            engine.set_time(next_tick);
            if (!realtime) {
                // In the 10 ms pieces a device asks for.
                auto frames = static_cast<uint32_t>((next_tick - mixed_until) * engine.mixer().rate());
                scratch.resize(480 * 2);
                while (frames > 0) {
                    const uint32_t piece = std::min<uint32_t>(frames, 480);
                    engine.pump();
                    engine.mixer().mix(scratch.data(), piece);
                    frames -= piece;
                }
                mixed_until = next_tick;
            }
            engine.update([&](int32_t id) -> std::optional<std::array<double, 3>> {
                const auto found = entities.find(id);
                if (found == entities.end()) return std::nullopt;
                return std::array<double, 3>{found->second.x, found->second.y, found->second.z};
            });
            next_tick += 0.05;
        }
    };
    while (true) {
        const double at = packet < times.size() ? times[packet] : now;
        if (realtime) std::this_thread::sleep_until(start + std::chrono::duration<double>(at));
        advance(at);
        now = at;
        engine.set_time(now);
        if (!session.pump()) break;
        ++packet;
    }
    advance(now + 3.0);
    (void)session_ptr;
    std::cout << std::format(
        "# {} packets, {} voices still playing, {} footsteps and {} own voices from the model\n",
        packet, engine.active(), predicted, predicted_voices);
    const auto mixing = engine.mixer().stats();
    const auto sounds = engine.stats();
    std::cout << std::format(
        "# mixer: {} callbacks, {:.1f} frames each, {:.2f} us average, worst {:.2f} us with {} voices\n"
        "# decoder: {} sounds in {:.1f} ms, {:.3f} ms each, worst {:.2f} ms ({}); cache {} sounds, {} KiB\n",
        mixing.callbacks, mixing.callbacks ? double(mixing.frames) / double(mixing.callbacks) : 0.0,
        mixing.callbacks ? double(mixing.nanoseconds) / double(mixing.callbacks) / 1000.0 : 0.0,
        double(mixing.worst_nanoseconds) / 1000.0, mixing.worst_voices, sounds.decodes,
        sounds.decode_ms, sounds.decodes ? sounds.decode_ms / double(sounds.decodes) : 0.0,
        sounds.worst_decode_ms, sounds.worst_decode, sounds.cached_sounds, sounds.cache_bytes / 1024);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string command = argc > 1 ? argv[1] : "";
    try {
        if (command == "record" && argc > 5) {
            return do_record(argv[2], argv[3], argv[4], std::stod(argv[5]));
        }
        if (command == "walk" && argc > 7) {
            return do_walk(argv[2], argv[3], std::stod(argv[4]), std::stod(argv[5]),
                           std::stod(argv[6]), std::stod(argv[7]),
                           argc > 8 ? std::stod(argv[8]) : 0.21585,
                           argc > 9 ? std::stod(argv[9]) : 0.0);
        }
        if (command == "replay" && argc > 2) {
            return do_replay(argv[2], argc > 3 && std::string_view(argv[3]) == "--realtime");
        }
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
    std::cerr << "usage: plasma-soundlog record <host:port> <name> <out.plrc> <seconds>\n"
                 "       plasma-soundlog walk <host:port> <name> <wait> <dx> <dz> <blocks> "
                 "[speed] [drop]\n"
                 "       plasma-soundlog replay <file.plrc> [--realtime]\n";
    return 2;
}
