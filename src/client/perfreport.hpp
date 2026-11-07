#pragma once
// P: `plasma-client perfreport <file>`: a recording made while playing, read
// back as the table worldbench prints plus a png of the frame times.
#include <string>
#include <vector>

namespace client {

int perfreport(const std::vector<std::string>& arguments);

} // namespace client
