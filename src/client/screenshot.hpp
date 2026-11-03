#pragma once
#include <string>
#include <vector>

namespace client {

// `plasma-client screenshot <screen|all> [out-dir] [width height]`: each
// screen drawn offscreen at gui scale 2 and 3, written as <screen>-s<scale>.png,
// with what building its interface cost the cpu.
int screenshot(const std::vector<std::string>& args);

// `plasma-client clicktest <host:port> <console fifo> [name]`: inventory clicks
// predicted here and checked against what the server does with them.
int clicktest(const std::vector<std::string>& args);

} // namespace client
