#pragma once
// Shared between session.cpp and the session_<area>.cpp files, and nobody else.
#include "mcpp/byte_buffer.hpp"
#include "mcpp/nbt.hpp"

#include <cstddef>
#include <string>

namespace mcpp::detail {

constexpr size_t kUuidBytes = 16;

std::string read_uuid(ByteBuffer& in);
std::string read_component(ByteBuffer& in, const nbt::TranslateLookup& lookup);

} // namespace mcpp::detail
