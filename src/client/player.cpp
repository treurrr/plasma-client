#include "client/game.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <iostream>
#include <iterator>
#include <numbers>

namespace client {

void player_bind(Game& game, mcpp::SessionEvents& events) {
    events.player.on_position = [&game](double x, double y, double z, float yaw, float pitch) {
        auto& player = game.player;
        player.spawn[0] = x;
        player.spawn[1] = y;
        player.spawn[2] = z;
        player.spawn_yaw = yaw;
        player.spawn_pitch = pitch;
        player.placed = true;
        std::cout << std::format("- the server put us at {:.2f} {:.2f} {:.2f}\n", x, y, z);
    };
}

void player_start(Game& game) {
    auto& player = game.player;
    const auto& chunks = game.world.chunks;

    float centre_x = 0.0f;
    float centre_z = 0.0f;
    for (const auto& chunk : chunks) {
        centre_x += (chunk.x + 0.5f) * 16.0f;
        centre_z += (chunk.z + 0.5f) * 16.0f;
    }
    centre_x /= static_cast<float>(chunks.size());
    centre_z /= static_cast<float>(chunks.size());

    // Stand where the server put us, not where the chunks happen to be centred.
    float* eye = player.eye;
    eye[0] = static_cast<float>(player.spawn[0]);
    eye[1] = static_cast<float>(player.spawn[1]) + 1.62f;
    eye[2] = static_cast<float>(player.spawn[2]);
    if (!player.placed) {
        eye[0] = centre_x;
        eye[1] = ground_near_centre(chunks, game.table, centre_x, centre_z) + 4.0f;
        eye[2] = centre_z;
        std::cout << "- no teleport from the server; standing at the centre of the chunks\n";
    }
    player.yaw = player.placed ? player.spawn_yaw + 90.0f : 90.0f;
    player.pitch = player.placed ? -player.spawn_pitch : -8.0f;
    player.dig_started = std::chrono::steady_clock::now();

    player.body.x = eye[0];
    player.body.y = eye[1] - mcpp::world::kEyeHeight;
    player.body.z = eye[2];
}

void player_input(Game& game) {
    auto& player = game.player;
    const auto& abilities = game.session->abilities();
    // The server decides whether the player may fly and, in spectator,
    // that the player does; double tapping only asks within that.
    if (game.input.toggle_fly && abilities.may_fly) player.flying = !player.flying;
    if (!abilities.may_fly) player.flying = false;
    if (abilities.flying && abilities.may_fly && !abilities.instant_build) {
        player.flying = true;
    }
}

void player_move(Game& game) {
    auto& player = game.player;
    auto& session = *game.session;
    const auto& input = game.input;
    auto& body = player.body;
    float* eye = player.eye;

    player.yaw += input.yaw_delta * 0.12f;
    player.pitch = std::clamp(player.pitch - input.pitch_delta * 0.12f, -89.0f, 89.0f);

    const float yr = player.yaw * std::numbers::pi_v<float> / 180.0f;
    const float pr = player.pitch * std::numbers::pi_v<float> / 180.0f;
    const float forward[3] = {std::cos(yr) * std::cos(pr), std::sin(pr),
                              std::sin(yr) * std::cos(pr)};
    const float right[3] = {-std::sin(yr), 0.0f, std::cos(yr)};
    // The camera measures yaw from +x, the game from +z (south) turning
    // towards -x. Anything that leaves this loop wants the game's.
    player.game_yaw = player.yaw - 90.0f;

    // Movement runs on the game's own clock, not the frame's, and the eye is
    // drawn between the last two ticks. Without that the same key covers a
    // different distance at sixty frames and at three hundred.
    player.physics_accumulator = std::min(player.physics_accumulator + game.dt, 0.25f);
    while (player.physics_accumulator >= mcpp::world::kTickSeconds) {
        player.physics_accumulator -= static_cast<float>(mcpp::world::kTickSeconds);
        {
            mcpp::world::Intent intent;
            intent.forward = input.forward;
            intent.strafe = input.right;
            intent.yaw_degrees = player.game_yaw;
            intent.jump = input.up > 0.0f && !player.flying;
            intent.sprint = input.sprint;
            intent.flying = player.flying;
            intent.rise = input.up;
            intent.flying_speed = session.abilities().flying_speed;
            mcpp::world::tick(body, intent, game.world.block_at, game.table);
        }
    }
    const float blend =
        player.physics_accumulator / static_cast<float>(mcpp::world::kTickSeconds);
    const auto between = [blend](double was, double is) {
        return static_cast<float>(was + (is - was) * blend);
    };
    eye[0] = between(body.previous_x, body.x);
    eye[1] = between(body.previous_y, body.y) + static_cast<float>(mcpp::world::kEyeHeight);
    eye[2] = between(body.previous_z, body.z);

    const auto reach = static_cast<float>(
        session.attribute(session.entity_id(), "minecraft:block_interaction_range", 4.5));
    std::fill(std::begin(player.aimed), std::end(player.aimed), 0);
    player.aiming = aimed_block(eye, forward, reach, game.world.block_at, game.table,
                                player.aimed);
    game.renderer->set_outlined_block(player.aimed, player.aiming);

    // Walking rocks the view: sideways with each step, down as each foot
    // lands, leaning into the step and nodding with it. Without it the
    // camera floats through the world instead of being carried through it.
    const float sway = between(body.previous_sway, body.sway);
    const float phase = -between(body.previous_walked, body.walked) *
                        std::numbers::pi_v<float>;
    const float across = std::sin(phase) * sway * 0.5f;
    const float down = -std::abs(std::cos(phase) * sway);
    const float lean = std::sin(phase) * sway * 3.0f;
    const float nod = std::abs(std::cos(phase - 0.2f) * sway) * 5.0f;

    const float pr_bob = (player.pitch - nod) * std::numbers::pi_v<float> / 180.0f;
    const float look[3] = {std::cos(yr) * std::cos(pr_bob), std::sin(pr_bob),
                           std::sin(yr) * std::cos(pr_bob)};

    auto& camera = game.camera;
    camera = mcpp::gpu::Camera{};
    for (int i = 0; i < 3; ++i) {
        camera.eye[i] = eye[i] + right[i] * across;
        camera.target[i] = camera.eye[i] + look[i];
    }
    camera.eye[1] += down;
    camera.target[1] += down;
    camera.roll_degrees = lean;
    // The view widens while you sprint and eases back when you stop, which
    // is most of how sprinting reads on screen at all.
    const float wanted_fov =
        70.0f * (body.sprinting || (player.flying && input.sprint) ? 1.15f : 1.0f);
    player.fov += (wanted_fov - player.fov) * std::min(1.0f, game.dt * 12.0f);
    camera.fov_degrees = player.fov;
}

void player_tick(Game& game) {
    auto& player = game.player;
    auto& session = *game.session;
    const auto& input = game.input;
    const auto& screens = game.screens;
    const int32_t* aimed = player.aimed;

    // Mining and building. The game sends a start and a finish for a dig
    // and leaves the block standing until the server says otherwise, which
    // is what keeps a client honest about what it may break.
    if (player.aiming && !screens.menu_open && !screens.inventory_open &&
        !game.window->typing()) {
        if (input.attack && !player.attacking) {
            session.send_dig(aimed[0], aimed[1], aimed[2], 1, false);
            session.send_swing();
            player.dig_started = game.now;
        }
        if (input.attack && player.attacking &&
            std::chrono::duration<float>(game.now - player.dig_started).count() > 0.25f) {
            session.send_dig(aimed[0], aimed[1], aimed[2], 1, true);
            player.dig_started = game.now;
            // The game takes the block out straight away and lets the
            // server put it back if it disagrees, which is the only way
            // mining feels immediate over a network.
            world_predict_block(game, aimed[0], aimed[1], aimed[2], 0);
        }
        if (input.use && !player.using_item) {
            session.send_place(aimed[0], aimed[1], aimed[2], 1, 0.5f, 1.0f, 0.5f);
            session.send_swing();
        }
    }
    player.attacking = input.attack;
    player.using_item = input.use;
}

void player_draw(Game&) {}

void player_stop(Game&) {}

bool player_send(Game& game) {
    auto& player = game.player;
    const float* eye = player.eye;
    try {
        game.session->send_position(eye[0], eye[1] - 1.62, eye[2], player.game_yaw,
                                    -player.pitch, false);
        ++player.ticks_sent;
    } catch (const std::exception& error) {
        std::cerr << std::format("movement stopped: {}\n", error.what());
        return false;
    }
    return true;
}

} // namespace client
