#pragma once
#include <string>
#include <vector>

namespace client {

// `plasma-client itemshot`: every item the game knows drawn as the interface
// draws it, counted, and every creative tab beside it.
int itemshot(const std::vector<std::string>& args);

} // namespace client
