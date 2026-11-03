#pragma once
// The table tools/gen_metadata.py writes into metadata_gen.cpp.
#include "mcpp/proto/metadata.hpp"

#include <map>
#include <string_view>
#include <vector>

namespace mcpp::proto::generated {

// Every entity type's fields by index, inherited ones included.
const std::map<std::string_view, std::vector<MetaField>, std::less<>>& metadata_fields();

} // namespace mcpp::proto::generated
