#pragma once
// The tables tools/gen_components.py writes into components_gen.cpp.
#include <cstdint>
#include <string_view>
#include <vector>

namespace mcpp::proto::generated {

struct NamedLayout {
    std::string_view name;
    std::string_view layout;
};

// The named types the layouts refer to as @name.
const std::vector<NamedLayout>& component_types();
// One per data component of every generated protocol, by registry name.
const std::vector<NamedLayout>& component_layouts();
// A registry's entries in a protocol's numbering: the data component types,
// and every registry a layout switches on. nullptr for any other.
const std::vector<std::string_view>* registry_entries(std::string_view registry,
                                                      int32_t protocol);

} // namespace mcpp::proto::generated
