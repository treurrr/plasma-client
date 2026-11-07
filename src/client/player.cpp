#include "client/game.hpp"
#include "client/player_rules.hpp"
#include "client/breaking.hpp"
#include "client/hand_view.hpp"

#include "mcpp/jmath.hpp"
#include "mcpp/world/shape_edges.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <iostream>
#include <iterator>
#include <numbers>

namespace client {
namespace {

using mcpp::world::BlockShapes;

const BlockShapes& shapes() { return BlockShapes::game(); }

// Blocks as movement asks for them: no lock per block, because the caller
// holds the world for the whole tick, and air outside the world's height.
int32_t block_unlocked(const Game& game, int32_t x, int32_t y, int32_t z) {
    const auto& area = game.world;
    const auto found = area.world.find({x >> 4, z >> 4});
    if (found == area.world.end()) return -1;
    const int32_t index = found->second.index_of(y);
    if (index < 0 || index >= static_cast<int32_t>(found->second.sections.size()) * 16) return 0;
    return found->second.block_state(x & 15, index, z & 15);
}

mcpp::world::Keys keys_of(const Game& game) {
    mcpp::world::Keys k;
    const auto& input = game.input;
    const auto& screens = game.screens;
    if (screens.menu_open || screens.inventory_open || game.window->typing()) return k;
    k.forward = input.forward > 0.0f;
    k.backward = input.forward < 0.0f;
    k.left = input.right < 0.0f;
    k.right = input.right > 0.0f;
    // The window folds jump and sneak into one axis; both held reads as 0.
    k.jump = input.up > 0.0f || (input.sneak && input.up == 0.0f);
    k.shift = input.sneak;
    k.sprint = input.sprint;
    return k;
}

// `SmoothDouble.getNewDeltaValue`: the cinematic camera's eased turn.
double smooth(PlayerArea::Smooth& s, double delta, double time) {
    s.target += delta;
    double step = s.target - s.remaining;
    const double eased = s.last + 0.5 * (step - s.last);
    const double sign = step > 0.0 ? 1.0 : step < 0.0 ? -1.0 : 0.0;
    if (sign * step > sign * s.last) step = eased;
    s.last = eased;
    s.remaining += step * time;
    return step * time;
}

void aim(Game& game, double partial) {
    auto& player = game.player;
    auto& session = *game.session;
    const auto& body = player.body;
    const double reach = session.attribute(session.entity_id(),
                                           "minecraft:block_interaction_range", 4.5);
    const double entity_reach = session.attribute(session.entity_id(),
                                                  "minecraft:entity_interaction_range", 3.0);
    const double distance = std::max(reach, entity_reach);
    double from[3];
    double look[3];
    eye_position(body, partial, from);
    view_vector(body.pitch, body.yaw, look);
    const double to[3] = {from[0] + look[0] * distance, from[1] + look[1] * distance,
                          from[2] + look[2] * distance};
    Hit hit;
    {
        std::lock_guard lock(game.world.world_guard);
        const mcpp::world::BlockAt at = [&game](int32_t x, int32_t y, int32_t z) {
            return block_unlocked(game, x, y, z);
        };
        hit = pick_block(shapes(), at, from, to);
    }
    if (hit.hit) {
        const double dx = hit.location[0] - from[0];
        const double dy = hit.location[1] - from[1];
        const double dz = hit.location[2] - from[2];
        if (dx * dx + dy * dy + dz * dz >= reach * reach) hit.hit = false;
    }
    player.aiming = hit.hit;
    std::copy(hit.pos, hit.pos + 3, player.aimed);
    player.hit_face = hit.face;
    std::copy(hit.location, hit.location + 3, player.hit_location);
}

// `LevelRenderer.extractBlockOutline`, `LevelRenderer.renderBlockOutline`, `ShapeRenderer.renderShape`: every edge of the aimed block's outline shape, where it stands.
void aim_outline(Game& game) {
    auto& player = game.player;
    auto& outline = game.world.outline;
    int32_t state = 0;
    if (player.aiming) {
        std::lock_guard lock(game.world.world_guard);
        state = std::max(0, block_unlocked(game, player.aimed[0], player.aimed[1], player.aimed[2]));
    }
    const uint16_t shape = shapes().state(state).outline;
    const bool shown = state != 0 && !shapes().empty(shape) && outline.has_value();
    // `ItemBlockRenderTypes.getChunkRenderType`, `ChunkSectionLayer.sortOnUpload`:
    // the two layers that sort on upload are the ones whose outline is drawn
    // after the translucent pass instead of before it.
    const mcpp::world::RenderLayer layer = game.world.render.at(state).layer;
    game.renderer->set_outlined_block(player.aimed, shown,
                                      layer == mcpp::world::RenderLayer::Translucent ||
                                          layer == mcpp::world::RenderLayer::Tripwire);
    if (!shown) return;
    const mcpp::gpu::Outline::Range* range = outline->find(shape);
    mcpp::gpu::Outline::Range added;
    if (range == nullptr) {
        std::vector<float> edges;
        mcpp::world::for_all_edges(shapes(), shape, [&](double x0, double y0, double z0, double x1,
                                                        double y1, double z1) {
            for (const double v : {x0, y0, z0, x1, y1, z1}) edges.push_back(static_cast<float>(v));
        });
        added = outline->add(shape, edges);
        range = &added;
    }
    double offset[3];
    shapes().offset(state, player.aimed[0], player.aimed[2], offset);
    double corner[3];
    for (int i = 0; i < 3; ++i) {
        corner[i] = static_cast<double>(player.aimed[i]) + offset[i] -
                    static_cast<double>(game.camera.eye[i]);
    }
    outline->aim(*range, corner, game.camera.eye, game.window->width(), game.window->height());
}

int32_t held_item(const Game& game) {
    const auto& inventory = game.session->inventory();
    return inventory.hotbar(inventory.held).id;
}

// Breaking and building, the game's keybind handling for one tick.
void interact(Game& game) {
    auto& player = game.player;
    auto& session = *game.session;
    const auto& input = game.input;
    const bool free = !game.screens.menu_open && !game.screens.inventory_open &&
                      !game.window->typing();
    const bool attack = free && input.attack;
    const bool use = free && input.use;

    if (player.right_click_delay > 0) --player.right_click_delay;
    if (player.miss_time > 0) --player.miss_time;

    // `KeyMapping.releaseAll`: a screen takes the keyboard, and the presses
    // nobody has taken yet go with it.
    if (!free) player.drop_clicks = player.swap_clicks = player.pick_clicks = 0;

    // `Minecraft.handleKeybinds`: swapping hands, dropping and picking the
    // block being looked at are presses, each taken once, and a spectator
    // does none of them. Control is read now rather than when the key went
    // down, the way `hasControlDown` is read inside the tick.
    if (free && session.vitals().game_mode != 3) {
        while (player.swap_clicks > 0) {
            --player.swap_clicks;
            session.send_player_action(6);
        }
        // `LocalPlayer.drop`: control throws the whole stack, and the arm
        // swings only when there was something to throw.
        while (player.drop_clicks > 0) {
            --player.drop_clicks;
            const auto& inventory = session.inventory();
            const int32_t slot = std::clamp(inventory.held, 0, 8);
            const bool had = inventory.hotbar(static_cast<size_t>(slot)).count > 0;
            session.send_player_action(input.control ? 3 : 4);
            if (had) session.send_swing();
        }
        // `Minecraft.pickBlock`: only a block under the crosshair, and control
        // asks for the block entity's data with it.
        while (player.pick_clicks > 0) {
            --player.pick_clicks;
            if (!player.aiming) continue;
            session.send_pick_item_from_block(player.aimed[0], player.aimed[1], player.aimed[2],
                                              input.control);
        }
    }

    // `Minecraft.handleKeybinds`: a press starts an attack, then using, then
    // holding attack carries a dig on.
    const bool pressed = attack && !player.attack_was;
    player.attack_was = attack;
    const bool broke = pressed && breaking_start_attack(game);

    // Using: a press, then again every four ticks while held.
    const bool use_pressed = use && !player.use_was;
    player.use_was = use;
    if ((use_pressed || (use && player.right_click_delay == 0)) && !breaking_destroying(game)) {
        player.right_click_delay = 4;
        if (player.aiming) {
            const int32_t* p = player.aimed;
            const double* at = player.hit_location;
            session.send_place(p[0], p[1], p[2], player.hit_face,
                               static_cast<float>(at[0] - p[0]), static_cast<float>(at[1] - p[1]),
                               static_cast<float>(at[2] - p[2]));
            session.send_swing();
        }
    }
    breaking_continue_attack(game, attack && !broke);
}

// `GameRenderer.tickFov`, `AbstractClientPlayer.getFieldOfViewModifier`, `Camera.tick`: field of view and eye height ease.
// The camera's own tick: the field of view eases towards what speed and
// flight ask for, and the eye towards the pose's height.
void camera_tick(Game& game) {
    auto& player = game.player;
    const auto& body = player.body;
    const auto& abilities = game.session->abilities();
    float modifier = 1.0f;
    if (body.flying) modifier *= 1.1f;
    const float walking = abilities.walking_speed;
    if (walking != 0.0f) {
        const auto speed = static_cast<float>(mcpp::world::Body::speed(player.stats, body.sprinting));
        modifier *= (speed / walking + 1.0f) / 2.0f;
    }
    modifier = 1.0f + player.fov_effect_scale * (modifier - 1.0f);
    player.fov_modifier_old = player.fov_modifier;
    player.fov_modifier += (modifier - player.fov_modifier) * 0.5f;
    player.fov_modifier = std::clamp(player.fov_modifier, 0.1f, 1.5f);

    player.eye_height_old = player.eye_height;
    player.eye_height += (body.eye_height() - player.eye_height) * 0.5f;
}

} // namespace

void player_bind_motion(Game& game, mcpp::SessionEvents& events);

void player_bind(Game& game, mcpp::SessionEvents& events) {
    player_bind_motion(game, events);
    breaking_bind(game, events);
    events.player.on_teleport = [&game](const mcpp::SessionEvents::Player::Teleport& t) {
        auto& player = game.player;
        apply_teleport(player.body, t);
        const auto& body = player.body;
        player.spawn[0] = body.x;
        player.spawn[1] = body.y;
        player.spawn[2] = body.z;
        player.spawn_yaw = body.yaw;
        player.spawn_pitch = body.pitch;
        if (!player.placed) {
            std::cout << std::format("- the server put us at {:.2f} {:.2f} {:.2f}\n", body.x,
                                     body.y, body.z);
        }
        player.placed = true;
        game.session->accept_teleport(t.id, body.x, body.y, body.z, body.yaw, body.pitch);
    };
}

void player_bind_motion(Game& game, mcpp::SessionEvents& events) {
    events.player.on_motion = [&game](double x, double y, double z) {
        auto& body = game.player.body;
        body.vx = x;
        body.vy = y;
        body.vz = z;
    };
}

void player_start(Game& game) {
    auto& player = game.player;
    auto& body = player.body;
    if (!player.placed) {
        const auto& chunks = game.world.chunks;
        float centre_x = 0.0f;
        float centre_z = 0.0f;
        for (const auto& chunk : chunks) {
            centre_x += (chunk.x + 0.5f) * 16.0f;
            centre_z += (chunk.z + 0.5f) * 16.0f;
        }
        centre_x /= static_cast<float>(chunks.size());
        centre_z /= static_cast<float>(chunks.size());
        body.x = body.xo = centre_x;
        body.y = body.yo = ground_near_centre(chunks, game.table, centre_x, centre_z) + 4.0f;
        body.z = body.zo = centre_z;
        std::cout << "- no teleport from the server; standing at the centre of the chunks\n";
    }
    body.xo = body.x;
    body.yo = body.y;
    body.zo = body.z;
    player.eye_height = player.eye_height_old = body.eye_height();
    player_tags(*game.session, shapes(), player.tags);
    player.tags_seen = game.session->tags().size();
    player.server_flying = game.session->abilities().flying;
    body.flying = player.server_flying;
    player.last_turn = std::chrono::steady_clock::now();
    player.started = true;
    breaking_start(game);
    hand_start(game);
}

// `KeyMapping.click`: a press is counted the moment it happens and taken by a
// later tick, because a frame can fall between two ticks or carry two of them.
void player_input(Game& game) {
    auto& player = game.player;
    const auto& input = game.input;
    if (input.drop) ++player.drop_clicks;
    if (input.swap_offhand) ++player.swap_clicks;
    player.pick_clicks += input.pick_item_clicks;
}

void player_move(Game&) {}

void player_tick(Game& game) {
    auto& player = game.player;
    auto& session = *game.session;
    auto& body = player.body;
    if (!player.started) return;

    // The server's word on flight wins over ours, the way the game takes it.
    if (session.abilities().flying != player.server_flying) {
        player.server_flying = session.abilities().flying;
        body.flying = player.server_flying;
    }
    if (session.tags().size() != player.tags_seen) {
        player_tags(session, shapes(), player.tags);
        player.tags_seen = session.tags().size();
    }

    // The game's clock, advanced for the whole frame in play.cpp: whole ticks
    // run, the rest is how far between two ticks the picture is drawn.
    const int due = game.ticks;
    player.partial_tick = game.clock.partial();
    const mcpp::world::BlockAt at = [&game](int32_t x, int32_t y, int32_t z) {
        return block_unlocked(game, x, y, z);
    };
    for (int i = 0; i < std::min(10, due); ++i) {
        aim(game, 1.0);
        try {
            interact(game);
        } catch (const std::exception& error) {
            std::cerr << std::format("interaction stopped: {}\n", error.what());
            player.connection_lost = true;
            return;
        }
        player.stats = player_stats(session);
        camera_tick(game);
        {
            std::lock_guard lock(game.world.world_guard);
            const mcpp::world::World world{at, shapes(), &player.tags, session.dimension().min_y};
            mcpp::world::tick(body, keys_of(game), player.stats, world);
        }
        try {
            send_tick(session, body);
            ++player.ticks_sent;
            breaking_tick(game);
        hand_tick(game);
        } catch (const std::exception& error) {
            std::cerr << std::format("movement stopped: {}\n", error.what());
            player.connection_lost = true;
            return;
        }
    }
}

void player_draw(Game& game) {
    auto& player = game.player;
    auto& body = player.body;
    if (!player.started) return;

    // The mouse turns the view every frame, as late before drawing as the
    // frame allows: this is what makes aim feel attached to the hand.
    const auto now = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(now - player.last_turn).count();
    player.last_turn = now;
    if (game.window->cursor_captured() && !game.window->typing()) {
        const double scale = mouse_scale(player.sensitivity);
        double dx = game.input.yaw_delta * scale;
        double dy = game.input.pitch_delta * scale;
        if (player.smooth_camera) {
            dx = smooth(player.smooth_x, game.input.yaw_delta * scale, elapsed * scale);
            dy = smooth(player.smooth_y, game.input.pitch_delta * scale, elapsed * scale);
        } else {
            player.smooth_x = {};
            player.smooth_y = {};
        }
        turn(body, dx, dy);
    }

    const auto pt = static_cast<float>(player.partial_tick);
    aim(game, player.partial_tick);

    // Where the camera is: the body between its last two ticks, the eye at
    // its eased height.
    const double eye[3] = {
        body.xo + player.partial_tick * (body.x - body.xo),
        body.yo + player.partial_tick * (body.y - body.yo) +
            static_cast<double>(player.eye_height_old +
                                pt * (player.eye_height - player.eye_height_old)),
        body.zo + player.partial_tick * (body.z - body.zo),
    };

    // `GameRenderer.bobView`: the walking bob.
    // Walking rocks the view: sideways and down with each step, a lean into
    // it and a nod, all in the view's own frame.
    float pitch = body.pitch;
    float roll = 0.0f;
    float shift_right = 0.0f;
    float shift_up = 0.0f;
    if (player.bob_view) {
        const float walked = -(body.walk_dist + (body.walk_dist - body.walk_dist_o) * pt);
        const float bob = body.bob_o + pt * (body.bob - body.bob_o);
        const float phase = walked * std::numbers::pi_v<float>;
        shift_right = mcpp::jmath::sin(phase) * bob * 0.5f;
        shift_up = -std::abs(mcpp::jmath::cos(phase) * bob);
        roll = mcpp::jmath::sin(phase) * bob * 3.0f;
        pitch += std::abs(mcpp::jmath::cos(phase - 0.2f) * bob) * 5.0f;
    }

    // The game's yaw faces south at 0 and turns towards west; the camera's
    // is measured from +x.
    const float yaw_r = (body.yaw + 90.0f) * std::numbers::pi_v<float> / 180.0f;
    const float pitch_r = -pitch * std::numbers::pi_v<float> / 180.0f;
    const float forward[3] = {std::cos(yaw_r) * std::cos(pitch_r), std::sin(pitch_r),
                              std::sin(yaw_r) * std::cos(pitch_r)};
    const float right0[3] = {-std::sin(yaw_r), 0.0f, std::cos(yaw_r)};
    const float up0[3] = {-std::cos(yaw_r) * std::sin(pitch_r), std::cos(pitch_r),
                          -std::sin(yaw_r) * std::sin(pitch_r)};
    const float roll_r = roll * std::numbers::pi_v<float> / 180.0f;
    const float cr = std::cos(roll_r);
    const float sr = std::sin(roll_r);

    auto& camera = game.camera;
    camera = mcpp::gpu::Camera{};
    for (int i = 0; i < 3; ++i) {
        const float right = right0[i] * cr - up0[i] * sr;
        const float up = right0[i] * sr + up0[i] * cr;
        camera.eye[i] = static_cast<float>(eye[i]) - shift_right * right - shift_up * up;
        camera.target[i] = camera.eye[i] + forward[i];
    }
    camera.roll_degrees = -roll;

    // `GameRenderer.getFov`: the field of view, narrower in a fluid.
    float fov = static_cast<float>(player.fov_option) *
                (player.fov_modifier_old + pt * (player.fov_modifier - player.fov_modifier_old));
    {
        std::lock_guard lock(game.world.world_guard);
        const int32_t x = static_cast<int32_t>(std::floor(eye[0]));
        const int32_t y = static_cast<int32_t>(std::floor(eye[1]));
        const int32_t z = static_cast<int32_t>(std::floor(eye[2]));
        const auto& info = shapes().state(std::max(0, block_unlocked(game, x, y, z)));
        if (info.fluid != mcpp::world::Fluid::None) {
            const auto& above = shapes().state(std::max(0, block_unlocked(game, x, y + 1, z)));
            const float h = above.fluid == info.fluid ? 1.0f : info.fluid_height();
            if (static_cast<double>(static_cast<float>(y) + h) > eye[1]) {
                fov *= 1.0f + player.fov_effect_scale * (0.85714287f - 1.0f);
            }
        }
    }
    camera.fov_degrees = fov;

    // The rest of the client reads the camera's angles and position here.
    for (int i = 0; i < 3; ++i) player.eye[i] = static_cast<float>(eye[i]);
    player.yaw = body.yaw + 90.0f;
    player.pitch = -body.pitch;
    player.game_yaw = body.yaw;
    player.fov = fov;
    aim_outline(game);
    breaking_draw(game);
    hand_draw(game);
}

void player_stop(Game& game) {
    breaking_stop(game);
    hand_stop(game);
}

bool player_send(Game& game) { return !game.player.connection_lost; }

} // namespace client
