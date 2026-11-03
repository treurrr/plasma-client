#pragma once
#include <string>
#include <vector>

namespace client {

// `plasma-client hudshot-scenes <scene|all> [out-dir] [width height]`: the
// hud in fixed situations - hurt, poisoned, hardcore, absorbing, chat, titles,
// boss bars, the sidebar, the player list - drawn offscreen at gui scale 2
// and 3, with what building it cost the cpu.
int hud_shot(const std::vector<std::string>& args);

} // namespace client
