#pragma once
#include <string>
#include <vector>

namespace client {

// `plasma-client chattest <host:port> [name]`: joins the server, types a line
// into the chat screen the way a player does, and reads back what the server
// sent everyone.
int chattest(const std::vector<std::string>& args);

} // namespace client
