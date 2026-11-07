#pragma once
#include "mcpp/json.hpp"

#include <string>
#include <string_view>

namespace mcpp::auth::detail {

inline std::string field(const json::Value& root, std::string_view key) {
    const auto* value = root.find(key);
    return value ? value->string_or("") : std::string{};
}

// Dashed form, because that is how the rest of the client writes a uuid.
inline std::string dashed(const std::string& compact) {
    if (compact.size() != 32) return compact;
    return compact.substr(0, 8) + "-" + compact.substr(8, 4) + "-" + compact.substr(12, 4) + "-" +
           compact.substr(16, 4) + "-" + compact.substr(20);
}

} // namespace mcpp::auth::detail
