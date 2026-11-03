// plasma-soundlog: records what a server makes heard, walks a second player
// past a listener, and replays a recording through the sound engine with no
// output device, writing down every voice instead of playing it.
#include "audio/block_sounds.hpp"
#include "audio/engine.hpp"
#include "audio/footsteps.hpp"
#include "audio/level_events.hpp"

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
    mixer_options.null_output = true;
    const bool device = engine.open(std::move(index), [&](std::string_view name) { return store->read(name); },
                                    mixer_options);
    if (!realtime) engine.mixer().close();
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
    events.player.on_position = [&](double x, double y, double z, float yaw, float pitch) {
        engine.set_listener({x, y + 1.62, z, yaw, pitch});
        std::cout << std::format("{:8.3f} listener at {:.2f} {:.2f} {:.2f} yaw {:.1f}\n", now, x,
                                 y + 1.62, z, yaw);
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
    const auto feed = [&](const mcpp::Entity& entity, bool teleported) {
        const int32_t player_type =
            session_ptr->registries().index_of("minecraft:entity_type", "minecraft:player");
        if (entity.type != player_type || entity.id == session_ptr->entity_id()) return;
        if (teleported) walkers[entity.id].reset();
        const auto sounds = walkers[entity.id].tick({entity.x, entity.y, entity.z, entity.on_ground},
                                                    mcpp::audio::BlockSounds::builtin(),
                                                    block_name_at, in_tag);
        for (const auto& sound : sounds) {
            ++predicted;
            std::cout << std::format("{:8.3f} model   {} at {:.3f} {:.3f} {:.3f} volume {:.4f} pitch {:.4f}\n",
                                     now, sound.cue.event, sound.x, sound.y, sound.z,
                                     sound.cue.volume, sound.cue.pitch);
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
        mcpp::audio::PlayRequest request;
        request.x = x + 0.5;
        request.y = y + 0.5;
        request.z = z + 0.5;
        if (type == mcpp::audio::kLevelEventBlockBreak) {
            const auto& type_of = mcpp::audio::BlockSounds::builtin().find(
                table.is_open() ? std::string(table.block_name(data)) : std::string());
            if (const auto sound = mcpp::audio::cue(type_of, mcpp::audio::BlockSoundKind::Break)) {
                request.event = sound->event;
                request.category = mcpp::audio::Category::Block;
                request.volume = sound->volume;
                request.pitch = sound->pitch;
                engine.play(request, client_random);
            }
            return;
        }
        if (const auto sound = mcpp::audio::level_event_sound(type)) {
            request.event = sound->event;
            request.category = sound->category;
            request.volume = sound->volume;
            request.pitch = sound->pitch_low + client_random.next_float() * (sound->pitch_high - sound->pitch_low);
            engine.play(request, client_random);
        }
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
                const auto frames = static_cast<uint32_t>((next_tick - mixed_until) * engine.mixer().rate());
                scratch.resize(static_cast<size_t>(frames) * 2);
                if (frames > 0) engine.mixer().mix(scratch.data(), frames);
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
    std::cout << std::format("# {} packets, {} voices still playing, {} footsteps from the model\n",
                             packet, engine.active(), predicted);
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
