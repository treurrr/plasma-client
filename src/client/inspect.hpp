#pragma once
#include <string>

namespace client {

// Joins, or plays back replay:<file>, and prints what the server sent: the
// registries, every slot with its components, every entity with its data, and
// the data packets. A mistake in a layout is visible here rather than
// plausible on screen. With a file, it also records what arrived.
int inspect(const std::string& target, const std::string& username, double seconds,
            const std::string& record);

} // namespace client
