#pragma once
#include <string>
#include <vector>

namespace client {

// `plasma-client outlineshot <out-dir> [block[:property=value,...] ...]`: the
// block outline around blocks that are not full cubes, from three sides.
int outlineshot(const std::vector<std::string>& args);

} // namespace client
