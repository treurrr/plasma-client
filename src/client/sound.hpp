#pragma once
// What the other areas call to be heard. All of it is safe to call before the
// sound engine is open, or when it would not open: it then does nothing.
#include "audio/block_sounds.hpp"
#include "audio/categories.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace client {

struct Game;

// The interface: a button's click is sound_ui(game, "ui.button.click").
void sound_ui(Game& game, std::string_view event, float pitch = 1.0f, float volume = 0.25f);

// The local player's hand on a block, which the server does not echo back:
// Place when a block item is put down, Hit every fourth tick of digging, Break
// when the block goes.
void sound_block(Game& game, int32_t x, int32_t y, int32_t z, int32_t state,
                 mcpp::audio::BlockSoundKind kind);

// J: a sound the world itself makes, already placed: rain on the roof.
void sound_world(Game& game, std::string_view event, mcpp::audio::Category category, double x,
                 double y, double z, float volume, float pitch);

// The options screen's sliders (soundCategory_* in options.txt), heard from
// the next frame on.
mcpp::audio::CategoryVolumes* sound_volumes(Game& game);

// options.txt's soundDevice: empty for the system's default.
std::vector<std::string> sound_devices();
void sound_set_device(Game& game, const std::string& device);

} // namespace client
