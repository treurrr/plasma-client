#pragma once
#include <string>
#include <vector>

namespace client {

// `plasma-client crafttest <host:port> <server dir> [name] [x y z]`: crafting
// in the inventory and at a table, with the vanilla server as the referee.
int crafttest(const std::vector<std::string>& args);
// `plasma-client booktest <host:port> <server dir> [name] [x y z]`: the recipe book
// filled by the server, clicked, and the grid the server fills from it.
int booktest(const std::vector<std::string>& args);

} // namespace client
