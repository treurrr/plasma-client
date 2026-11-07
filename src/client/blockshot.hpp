#pragma once
#include <string>
#include <vector>

namespace client {

// `plasma-client blockshot`: the blocks the game draws with a model of their
// own, each on a floor of its own, from three sides.
int blockshot(const std::vector<std::string>& args);
// `plasma-client blockserver`: the same models, but every block and every
// block entity comes from a server.
int blockserver(const std::vector<std::string>& args);
// `plasma-client blockbench`: what a chunk full of them costs in a frame.
int blockbench(const std::vector<std::string>& args);

} // namespace client
