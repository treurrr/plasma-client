#pragma once
// B: breaking blocks in the window: the dig itself (game_mode.hpp), the
// cracks of everyone breaking something, and the particles. The player area
// calls these from its own hooks.
#include <cstdint>

namespace mcpp {
struct SessionEvents;
}

namespace client {

struct Game;

void breaking_bind(Game& game, mcpp::SessionEvents& events);
void breaking_start(Game& game);
// `Minecraft.startAttack`: a press of attack; true when it broke a block outright.
bool breaking_start_attack(Game& game);
// `Minecraft.continueAttack`: attack held, or let go.
void breaking_continue_attack(Game& game, bool down);
bool breaking_destroying(const Game& game);
// One game tick of the particles and the cracks.
void breaking_tick(Game& game);
// This frame's particles and cracks, handed to the gpu.
void breaking_draw(Game& game);
void breaking_stop(Game& game);
// J: a drop the weather let fall, or the smoke it turns into over something
// hot. Does nothing before the particle engine is open.
void breaking_spawn_rain(Game& game, bool smoke, double x, double y, double z);
// A level event from the server; 2001 is a block someone else broke.
void breaking_level_event(Game& game, int32_t type, int32_t x, int32_t y, int32_t z, int32_t data);

} // namespace client
