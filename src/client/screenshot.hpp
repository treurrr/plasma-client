#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace mcpp::gpu {
class UiRenderer;
class WorldRenderer;
} // namespace mcpp::gpu

namespace client {

// `plasma-client screenshot <screen|all> [out-dir] [width height]`: each
// screen drawn offscreen at gui scale 2 and 3, written as <screen>-s<scale>.png,
// with what building its interface cost the cpu.
int screenshot(const std::vector<std::string>& args);

// `plasma-client clicktest <host:port> <console fifo> [name]`: inventory clicks
// predicted here and checked against what the server does with them.
int clicktest(const std::vector<std::string>& args);

// The game menu over a frame the renderer is about to draw: everything it
// draws goes into `quads`, and the frame is blurred under it.
void draw_pause_over(mcpp::gpu::UiRenderer& quads, mcpp::gpu::WorldRenderer& renderer,
                     uint32_t width, uint32_t height);

} // namespace client
