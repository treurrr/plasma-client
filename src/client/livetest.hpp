#pragma once
#include <string>
#include <vector>

namespace client {

// M: one scripted session against a real server, run through the client's own
// frame loop with no window. Frames are written to PNG along the way.
int livetest(const std::vector<std::string>& args);

} // namespace client
