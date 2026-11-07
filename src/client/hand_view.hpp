#pragma once
// D: the first person hand in the window. What it draws and where comes from
// client/hand.cpp; this hangs it on the player area's hooks and hands the
// frame to mcpp/gpu/hand.hpp. The player area calls these from its own hooks.
#include "client/hand.hpp"

namespace mcpp::gpu {
class HandRenderer;
}

namespace client {

struct Game;

// Everything one frame draws in front of the camera, queued on the renderer:
// the arm with the player's own skin, or the item with its display transform.
void queue_hand(mcpp::gpu::HandRenderer& renderer, const mcpp::client::HandPlayer& player,
                const mcpp::client::HandBob& bob, bool slim);

void hand_start(Game& game);
// The renderer that owns the item quads, for the entity area: what a mob
// holds is item geometry, not a model layer. Null until the hand has started.
mcpp::gpu::HandRenderer* hand_renderer(Game& game);
// `ItemInHandRenderer.tick`: the dip as the hand changes what it holds.
void hand_tick(Game& game);
void hand_draw(Game& game);
void hand_stop(Game& game);

} // namespace client
