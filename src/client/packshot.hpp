#pragma once
#include <string>
#include <vector>

namespace client {

// `plasma-client packshot`: a scene rendered with and without a server's
// resource pack on top of the baked tables, and what applying it cost.
int packshot(const std::vector<std::string>& args);
// `plasma-client packserve`: the same, but the pack comes from a server over
// http while the client is running.
int packserve(const std::vector<std::string>& args);

} // namespace client
