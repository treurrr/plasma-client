// M: a session with no window. The client's whole frame loop - the screen
// manager, the raw input events, the one tick clock - run against the real
// server, with the keys, the clicks, the pointer and glfw's clock coming from
// a script instead of a keyboard. Nothing is reached around: a phase presses W
// and the player walks because the window reports W held, the way it does
// when a hand is on it. Frames go to PNG along the way so they can be looked
// at.
#include "client/livetest.hpp"

#include "client/game.hpp"

#include "mcpp/gpu/world_renderer.hpp"
#include "mcpp/image_png.hpp"
#include "mcpp/protocol_versions.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iostream>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace client {
namespace {

using Clock = std::chrono::steady_clock;

struct Console {
    std::filesystem::path pipe;
    bool run(const std::string& command) {
        if (pipe.empty()) return false;
        std::ofstream out(pipe, std::ios::app);
        if (!out) return false;
        out << command << '\n';
        out.flush();
        return true;
    }
};

// A phase of the session: what it is called, how many frames it lasts, what it
// does on the frame it starts, what it holds down while it runs, and what it
// lets go at the end.
struct Phase {
    std::string name;
    int frames = 60;
    std::function<void()> enter;
    std::function<void(int)> each;
    std::function<void()> leave;
    // Frames inside the phase whose picture is kept, counted from its start.
    std::vector<int> shots;
    // Also keep the same moment from three more yaws, world only.
    bool angles = false;
};

void rotate(const mcpp::gpu::Camera& from, mcpp::gpu::Camera& to, float degrees) {
    to = from;
    const float dx = from.target[0] - from.eye[0];
    const float dz = from.target[2] - from.eye[2];
    const float radians = degrees * 3.14159265358979f / 180.0f;
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    to.target[0] = from.eye[0] + dx * c - dz * s;
    to.target[2] = from.eye[2] + dx * s + dz * c;
}

} // namespace

int livetest(const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cerr << "usage: livetest <host:port> [name] [out-dir] [w] [h] [phases]\n";
        return 2;
    }
    const std::string target = args[0];
    std::string username = args.size() > 1 ? args[1] : "PlasmaL";
    const std::filesystem::path out_dir = args.size() > 2 ? args[2] : "live";
    const uint32_t width = args.size() > 4 ? static_cast<uint32_t>(std::stoul(args[3])) : 1280;
    const uint32_t height = args.size() > 4 ? static_cast<uint32_t>(std::stoul(args[4])) : 720;
    const std::string only = args.size() > 5 ? args[5] : "";
    std::filesystem::create_directories(out_dir);

    Console console;
    if (const char* pipe = std::getenv("PLASMA_CONSOLE")) console.pipe = pipe;

    Game game(220);
    game.address = parse_address(target);
    if (!game.table.open(mcpp::world::BlockTable::default_path(mcpp::protocol::kTargetProtocol))) {
        std::cerr << "no block table; bake one with tools/bake_block_table.py\n";
        return 1;
    }
    if (!game.atlas.open(mcpp::world::Atlas::default_path(mcpp::protocol::kTargetProtocol))) {
        std::cerr << "no atlas; bake one with tools/bake_atlas.py\n";
        return 1;
    }
    game.options.host = game.address.host;
    game.options.port = game.address.port;
    game.options.username = username;

    // The same device every offscreen tool makes, and the window the client
    // asks everything of - only with no window behind it.
    game.device.create({});
    auto& window = game.window.emplace(game.device, width, height, mcpp::gpu::Window::Headless{});
    auto& renderer = game.renderer.emplace(game.device, width, height, game.atlas);

    mcpp::SessionEvents events;
    events.on_status = [](std::string_view line) { std::cout << "- " << line << '\n'; };
    world_bind(game, events);
    entities_bind(game, events);
    player_bind(game, events);
    screens_bind(game, events);
    sound_bind(game, events);
    hud_bind(game, events);

    auto& session = game.session.emplace(game.options, events);
    session.connect();
    const auto deadline = Clock::now() + std::chrono::seconds(30);
    while (!game.world.enough.load() && Clock::now() < deadline) {
        try {
            if (!session.pump()) break;
        } catch (const mcpp::TimeoutError&) {
            break;
        }
    }
    std::cout << std::format("- {} chunks fetched\n", game.world.chunks.size());
    if (game.world.chunks.empty()) {
        session.close();
        std::cerr << "no chunks: the server did not let us in\n";
        return 1;
    }

    world_load(game);
    world_start(game);
    entities_start(game);
    player_start(game);
    screens_start(game);
    sound_start(game);
    hud_start(game);

    // Written as they are taken, not collected: a frame is 3.7 MB and this
    // machine has no room to hold ninety of them.
    size_t kept = 0;
    const auto write = [&](const std::string& name, std::span<const uint8_t> rgba) {
        mcpp::write_png(out_dir / std::format("{}.png", name), renderer.width(),
                        renderer.height(), rgba);
        ++kept;
    };
    const auto keep = [&](const std::string& name) { write(name, window.pixels()); };
    const auto hold = [&](int key, bool down) {
        window.send_key(key, down ? GLFW_PRESS : GLFW_RELEASE);
    };
    // A tap has to last a whole frame: `poll` reads the key's state, so a key
    // pressed and let go between two polls was never held at all, the way a
    // key pressed and let go between two frames of a real window is not.
    std::vector<int> tapped_keys;
    std::vector<int> tapped_buttons;
    const auto tap = [&](int key) {
        window.send_key(key, GLFW_PRESS);
        tapped_keys.push_back(key);
    };
    const auto tap_button = [&](int button) {
        window.send_button(button, GLFW_PRESS);
        tapped_buttons.push_back(button);
    };

    // Where the player stands for the whole session, and the site built around
    // it. Everything is in one small box far from spawn, and taken down again.
    // High enough above the ground that no hill is in the way and the sky is
    // open, which is what the rain needs.
    const int32_t sx = 3000, sy = 140, sz = -3000;
    const auto say = [&](const std::string& command) {
        if (!console.run(command)) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
    };

    std::vector<Phase> script;
    const auto phase = [&](Phase p) {
        if (only.empty() || only.find(p.name) != std::string::npos) script.push_back(std::move(p));
    };

    phase({"site", 40, [&] {
               say(std::format("forceload add {} {} {} {}", sx - 20, sz - 20, sx + 20, sz + 20));
               say(std::format("fill {} {} {} {} {} {} minecraft:air", sx - 11, sy, sz - 11,
                               sx + 11, sy + 9, sz + 11));
               // Six deep, so a pool can be carved out of it and still hold.
               say(std::format("fill {} {} {} {} {} {} minecraft:stone", sx - 11, sy - 6, sz - 11,
                               sx + 11, sy - 1, sz + 11));
               say(std::format("gamemode survival {}", username));
               say(std::format("tp {} {}.5 {} {}.5 180 0", username, sx, sy, sz));
           }, {}, {}, {30}, false});

    // A wall with ladders, a pool, a chest, a crafting table, blocks to place
    // and mobs to look at.
    phase({"build", 60, [&] {
               say(std::format("fill {} {} {} {} {} {} minecraft:stone", sx + 3, sy, sz - 1,
                               sx + 3, sy + 4, sz + 1));
               say(std::format("fill {} {} {} {} {} {} minecraft:ladder[facing=north]", sx + 2, sy,
                               sz, sx + 2, sy + 4, sz));
               // Four deep and level with the floor, so it holds: every side
               // of it is still the slab.
               say(std::format("fill {} {} {} {} {} {} minecraft:water", sx - 6, sy - 4, sz - 3,
                               sx - 3, sy - 1, sz + 1));
               say(std::format("setblock {} {} {} minecraft:chest[facing=north]", sx + 1, sy,
                               sz - 3));
               say(std::format("item replace block {} {} {} container.0 with minecraft:diamond 5",
                               sx + 1, sy, sz - 3));
               say(std::format("item replace block {} {} {} container.13 with minecraft:golden_apple 2",
                               sx + 1, sy, sz - 3));
               say(std::format("setblock {} {} {} minecraft:crafting_table", sx - 1, sy, sz - 3));
               say(std::format("give {} minecraft:oak_planks 64", username));
               say(std::format("give {} minecraft:cobblestone 64", username));
               say(std::format("give {} minecraft:diamond_pickaxe", username));
               say(std::format("give {} minecraft:ladder 16", username));
               say(std::format("give {} minecraft:torch 32", username));
               say(std::format("summon minecraft:pig {} {} {}", sx - 2, sy, sz - 5));
               say(std::format("summon minecraft:pig {} {} {} {{variant:\"minecraft:cold\"}}",
                               sx - 1, sy, sz - 5));
               say(std::format("summon minecraft:cow {} {} {}", sx, sy, sz - 5));
               say(std::format("summon minecraft:cat {} {} {} {{variant:\"minecraft:siamese\",Age:0}}",
                               sx + 1, sy, sz - 5));
               say(std::format("summon minecraft:horse {} {} {}", sx + 2, sy, sz - 6));
               say(std::format("summon minecraft:zombie {} {} {} {{NoAI:1b}}", sx + 3, sy, sz - 5));
               // Armour with a trim on it, and a sword in a hand: both are
               // layers over a body that only another entity can show, since
               // the player never sees his own.
               say(std::format("item replace entity @e[type=minecraft:zombie,limit=1,x={},y={},"
                               "z={},distance=..15] armor.chest with minecraft:netherite_chestplate"
                               "[minecraft:trim={{material:\"minecraft:gold\","
                               "pattern:\"minecraft:eye\"}}]",
                               sx, sy, sz));
               say(std::format("item replace entity @e[type=minecraft:zombie,limit=1,x={},y={},"
                               "z={},distance=..15] weapon.mainhand with minecraft:diamond_sword",
                               sx, sy, sz));
               // A row of the blocks the game draws with a model of their own,
               // so one look says which of them draw nothing.
               const char* props[] = {"minecraft:spawner",
                                      "minecraft:beacon",
                                      "minecraft:campfire[lit=true]",
                                      "minecraft:lectern[has_book=true,facing=north]",
                                      "minecraft:piston[facing=up,extended=true]",
                                      "minecraft:piston_head[facing=up,type=normal]",
                                      "minecraft:end_portal_frame[facing=north]",
                                      "minecraft:vault[facing=north]",
                                      "minecraft:decorated_pot[facing=north]",
                                      "minecraft:bell[facing=north,attachment=floor]",
                                      "minecraft:shulker_box",
                                      "minecraft:oak_sign[rotation=8]",
                                      "minecraft:white_banner[rotation=8]",
                                      "minecraft:red_bed[facing=north,part=head]",
                                      "minecraft:skeleton_skull[rotation=8]",
                                      "minecraft:conduit[waterlogged=false]",
                                      "minecraft:enchanting_table",
                                      "minecraft:trial_spawner"};
               int32_t at = sx - 8;
               for (const char* prop : props) {
                   say(std::format("setblock {} {} {} {}", at, sy, sz + 6, prop));
                   ++at;
               }
               say(std::format("give {} minecraft:golden_apple 4", username));
           }, {}, {}, {50}, true});

    // The row of block-entity props, from in front and from both sides. A hole
    // in the row is a block the client draws nothing for.
    phase({"props", 70,
           [&] { say(std::format("tp {} {}.5 {} {} 0 15", username, sx - 1, sy, sz + 2)); }, {}, {},
           {40, 65}, true});

    phase({"stand", 30, {}, {}, {}, {20}, true});

    phase({"walk", 60, [&] { hold(GLFW_KEY_W, true); }, {}, [&] { hold(GLFW_KEY_W, false); },
           {20, 50}, false});

    phase({"sprint", 60,
           [&] {
               hold(GLFW_KEY_W, true);
               hold(GLFW_KEY_LEFT_CONTROL, true);
           },
           {},
           [&] {
               hold(GLFW_KEY_W, false);
               hold(GLFW_KEY_LEFT_CONTROL, false);
           },
           {30, 55}, false});

    phase({"jump", 50,
           [&] { hold(GLFW_KEY_SPACE, true); }, {}, [&] { hold(GLFW_KEY_SPACE, false); },
           {6, 12, 20}, false});

    phase({"sneak", 30, [&] { hold(GLFW_KEY_LEFT_SHIFT, true); }, {},
           [&] { hold(GLFW_KEY_LEFT_SHIFT, false); }, {20}, false});

    phase({"swim", 150,
           [&] {
               say(std::format("tp {} {}.5 {} {}.5 90 10", username, sx - 5, sy, sz - 1));
               hold(GLFW_KEY_W, true);
           },
           [&](int frame) {
               if (frame == 40) hold(GLFW_KEY_LEFT_SHIFT, true);
               if (frame == 100) hold(GLFW_KEY_LEFT_SHIFT, false);
           },
           [&] {
               hold(GLFW_KEY_W, false);
               hold(GLFW_KEY_LEFT_SHIFT, false);
           },
           {30, 70, 120}, true});

    phase({"ladder", 150,
           [&] {
               say(std::format("tp {} {}.5 {} {}.5 90 0", username, sx + 1, sy, sz));
               hold(GLFW_KEY_W, true);
           },
           {}, [&] { hold(GLFW_KEY_W, false); }, {40, 90, 140}, true});

    // Breaking and placing, aimed straight down at the floor the player is on.
    phase({"break", 120,
           [&] {
               say(std::format("tp {} {}.5 {} {}.5 180 60", username, sx, sy, sz));
               window.send_button(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
           },
           {}, [&] { window.send_button(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE); },
           {20, 60, 110}, true});

    phase({"place", 60,
           [&] {
               tap(GLFW_KEY_1);
               window.send_button(GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS);
           },
           {}, [&] { window.send_button(GLFW_MOUSE_BUTTON_RIGHT, GLFW_RELEASE); }, {30, 55},
           true});

    // The three bindings a player uses without thinking: drop, swap to the
    // offhand, and pick the block being looked at.
    phase({"drop", 120,
           [&] { say(std::format("tp {} {}.5 {} {}.5 180 20", username, sx, sy, sz)); },
           [&](int frame) {
               if (frame == 20 || frame == 40 || frame == 60) tap(GLFW_KEY_Q);
               if (frame == 80) tap(GLFW_KEY_F);
               if (frame == 95) tap_button(GLFW_MOUSE_BUTTON_MIDDLE);
           },
           {}, {30, 70, 90, 115}, false});

    // The mouse turning the view, which is the one input that is felt rather
    // than seen: the pointer walks right a few pixels a frame.
    phase({"look", 90,
           [&] { say(std::format("tp {} {}.5 {} {}.5 180 0", username, sx, sy, sz + 3)); },
           [&](int frame) { window.send_cursor(640.0 + frame * 6.0, 360.0); }, {},
           {20, 50, 85}, false});

    phase({"inventory", 60, [&] { tap(GLFW_KEY_E); }, {}, {}, {10, 40}, false});

    // A click in a slot, through the screen manager's own pointer path: move
    // the pointer there first, the way a hand does, then press and release.
    phase({"inv-click", 60,
           [&] {
               const int scale = mcpp::ui::gui_scale(static_cast<int>(width),
                                                     static_cast<int>(height),
                                                     game.screens.options.get_int("guiScale"),
                                                     false);
               // The first hotbar slot of the inventory screen, in pixels.
               const int gui_w = mcpp::ui::gui_extent(static_cast<int>(width), scale);
               const int gui_h = mcpp::ui::gui_extent(static_cast<int>(height), scale);
               const double gx = (gui_w - 176) / 2.0 + 8 + 8;
               const double gy = (gui_h - 166) / 2.0 + 142 + 8;
               window.send_cursor(gx * scale, gy * scale);
           },
           [&](int frame) {
               if (frame == 10) window.send_button(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
               if (frame == 13) window.send_button(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);
           },
           {}, {8, 20, 50}, false});

    phase({"inv-close", 20, [&] { tap(GLFW_KEY_E); }, {}, {}, {15}, false});

    phase({"craft", 150,
           [&] {
               say(std::format("tp {} {}.5 {} {} 180 35", username, sx - 1, sy, sz - 2));
           },
           [&](int frame) {
               if (frame == 20) window.send_button(GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS);
               if (frame == 24) window.send_button(GLFW_MOUSE_BUTTON_RIGHT, GLFW_RELEASE);
               // Only once a screen has the pointer, so moving it is not a look.
               if (frame == 50) window.send_cursor(width / 2.0, height / 2.0);
           },
           {}, {15, 60, 120}, false});

    phase({"craft-close", 20, [&] { tap(GLFW_KEY_ESCAPE); }, {}, {}, {15}, false});

    phase({"chest", 150,
           [&] {
               say(std::format("tp {} {}.5 {} {} 180 35", username, sx + 1, sy, sz - 2));
           },
           [&](int frame) {
               if (frame == 20) window.send_button(GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS);
               if (frame == 24) window.send_button(GLFW_MOUSE_BUTTON_RIGHT, GLFW_RELEASE);
               if (frame == 50) window.send_cursor(width / 2.0, height / 2.0);
           },
           {}, {15, 60, 120}, true});

    phase({"chest-close", 20, [&] { tap(GLFW_KEY_ESCAPE); }, {}, {}, {15}, false});

    phase({"mobs", 80,
           [&] { say(std::format("tp {} {}.5 {} {}.5 0 5", username, sx, sy, sz - 2)); }, {}, {},
           {40, 70}, true});

    // `minecraft:item` particles: a bite throws them. The chestplate carries a
    // trim, which is what the player's own body should show.
    phase({"eat", 170,
           [&] {
               say(std::format("tp {} {}.5 {} {}.5 180 10", username, sx, sy, sz));
               say(std::format("item replace entity {} armor.chest with "
                               "minecraft:netherite_chestplate[minecraft:trim={{material:"
                               "\"minecraft:gold\",pattern:\"minecraft:eye\"}}]",
                               username));
               say(std::format("effect give {} minecraft:hunger 2 5", username));
           },
           [&](int frame) {
               if (frame == 50) {
                   tap(GLFW_KEY_6);
                   window.send_button(GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS);
               }
               if (frame == 140) window.send_button(GLFW_MOUSE_BUTTON_RIGHT, GLFW_RELEASE);
           },
           [&] { window.send_button(GLFW_MOUSE_BUTTON_RIGHT, GLFW_RELEASE); },
           {70, 110, 135, 160}, false});

    phase({"hurt", 120,
           [&] { say(std::format("damage {} 7 minecraft:generic", username)); },
           [&](int frame) {
               if (frame == 60) say(std::format("effect give {} minecraft:instant_health 1 2",
                                                username));
           },
           {}, {5, 20, 55, 110}, false});

    // The weather is the whole server's, so it is turned on for as short a
    // window as a few frames of it need and put straight back.
    phase({"rain", 150,
           [&] { say("weather rain 8"); },
           {}, [&] { say("weather clear"); }, {40, 90, 140}, true});

    phase({"chat", 100,
           [&] { tap(GLFW_KEY_T); },
           [&](int frame) {
               if (frame >= 10 && frame < 10 + 11) {
                   const char* line = "hello world";
                   window.send_char(static_cast<uint32_t>(line[frame - 10]));
               }
               if (frame == 40) tap(GLFW_KEY_ENTER);
           },
           {}, {8, 30, 45, 90}, false});

    phase({"pause", 80, [&] { tap(GLFW_KEY_ESCAPE); }, {}, {}, {10, 40, 70}, false});

    phase({"pause-close", 20, [&] { tap(GLFW_KEY_ESCAPE); }, {}, {}, {15}, false});

    // The whole frame, in the order play.cpp runs it.
    auto previous = Clock::now();
    auto next = Clock::now();
    size_t frames = 0;
    const auto frame_budget = std::chrono::microseconds(16667);

    for (auto& step : script) {
        if (step.enter) step.enter();
        std::cout << std::format("# {}\n", step.name);
        // Setting a phase up talks to the server's console, which takes real
        // time. The frame clock starts after it, so the first frame of a phase
        // is a frame and not a two second jump.
        previous = Clock::now();
        next = previous;
        for (int inside = 0; inside < step.frames; ++inside) {
            if (!window.open()) break;
            next += frame_budget;
            std::this_thread::sleep_until(next);
            if (step.each) step.each(inside);

            const bool wanted =
                std::ranges::find(step.shots, inside) != step.shots.end();
            window.capture_next(wanted);

            game.input = window.poll();
            // The frame has seen whatever was tapped; now it comes up again.
            for (const int key : tapped_keys) window.send_key(key, GLFW_RELEASE);
            for (const int button : tapped_buttons) window.send_button(button, GLFW_RELEASE);
            tapped_keys.clear();
            tapped_buttons.clear();
            screens_input(game);
            player_input(game);
            world_input(game);
            entities_input(game);
            sound_input(game);

            game.now = Clock::now();
            game.dt = std::chrono::duration<float>(game.now - previous).count();
            previous = game.now;
            window.advance_clock(game.dt);
            game.ticks = game.clock.advance(game.dt);
            game.tick_due = game.ticks > 0;

            player_move(game);

            while (session.has_data() || session.in_bundle()) {
                try {
                    if (!session.pump()) break;
                } catch (const std::exception&) {
                    break;
                }
            }

            player_tick(game);
            screens_tick(game);
            world_tick(game);
            entities_tick(game);
            sound_tick(game);
            hud_tick(game);
            if (game.tick_due && !player_send(game)) {
                std::cerr << "the server dropped us\n";
                window.send_close();
            }

            game.fps = game.fps == 0.0 ? 1.0 / game.dt : game.fps * 0.92 + (1.0 / game.dt) * 0.08;
            world_draw(game);
            entities_draw(game);
            player_draw(game);
            sound_draw(game);
            screens_draw(game);
            window.present(renderer, game.camera,
                           [&](VkCommandBuffer command, uint32_t w, uint32_t h) {
                               screens_record(game, command, w, h);
                           });
            ++frames;
            if (!wanted) continue;
            keep(std::format("{}-{:03d}", step.name, inside));
            if (!step.angles) continue;
            for (const float degrees : {90.0f, 180.0f, 270.0f}) {
                mcpp::gpu::Camera turned{};
                rotate(game.camera, turned, degrees);
                write(std::format("{}-{:03d}-yaw{:03.0f}", step.name, inside, degrees),
                      renderer.render(turned));
            }
        }
        if (step.leave) step.leave();
        const auto& inventory = session.inventory();
        const auto& held = inventory.hotbar(static_cast<size_t>(inventory.held));
        std::cout << std::format(
            "  xyz {:.1f} {:.1f} {:.1f} yaw {:.0f} pitch {:.0f} hp {:.1f} food {} "
            "entities {} held {}x{} offhand {}x{} screen {} typing {}\n",
            game.player.body.x, game.player.body.y, game.player.body.z, game.player.body.yaw,
            game.player.body.pitch, session.vitals().health, session.vitals().food,
            session.entity_count(), held.id, held.count,
            inventory.slots[mcpp::Inventory::kOffhand].id,
            inventory.slots[mcpp::Inventory::kOffhand].count,
            game.screens.manager.current() != nullptr ? 1 : 0, window.typing() ? 1 : 0);
    }

    std::cout << std::format("- {} frames, {} shots, {} entities, health {:.1f}, food {}\n", frames,
                             kept, session.entity_count(), session.vitals().health,
                             session.vitals().food);

    world_stop(game);
    entities_stop(game);
    player_stop(game);
    screens_stop(game);
    sound_stop(game);
    hud_stop(game);
    session.close();

    say(std::format("kill @e[type=!player,x={},y={},z={},distance=..25]", sx, sy, sz));
    say(std::format("fill {} {} {} {} {} {} minecraft:air", sx - 11, sy - 6, sz - 11, sx + 11,
                    sy + 9, sz + 11));
    say(std::format("forceload remove {} {} {} {}", sx - 20, sz - 20, sx + 20, sz + 20));

    std::cout << std::format("- wrote {} pngs to {}\n", kept, out_dir.string());
    game.device.save_pipeline_cache();
    return 0;
}

} // namespace client
