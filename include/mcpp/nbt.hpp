#pragma once
#include "mcpp/byte_buffer.hpp"

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace mcpp::nbt {

class Value;
using Compound = std::map<std::string, Value, std::less<>>;
using List = std::vector<Value>;

class Value {
public:
    using Storage = std::variant<std::monostate, int8_t, int16_t, int32_t, int64_t, float, double,
                                 std::string, List, Compound, std::vector<uint8_t>,
                                 std::vector<int32_t>, std::vector<int64_t>>;

    Value() = default;
    explicit Value(Storage storage) : storage_(std::move(storage)) {}

    bool is_null() const { return std::holds_alternative<std::monostate>(storage_); }

    const Value* find(std::string_view key) const;
    const std::string* as_string() const;
    const List* as_list() const;
    const Compound* as_compound() const;
    std::optional<int64_t> as_int() const;
    std::optional<double> as_double() const;

    std::string string_or(std::string_view fallback) const;
    // F: the tag as it was read, so a file can be written back whole.
    const Storage& storage() const { return storage_; }

private:
    Storage storage_;
};

// Network form since 1.20.2: the root tag carries its type but no name.
Value read_network_tag(ByteBuffer& buffer);

using TranslateLookup = std::function<const std::string*(std::string_view)>;

// A text component is a bare string, a list, or a compound with text/extra/translate.
std::string component_to_text(const Value& value, const TranslateLookup& lookup = {});
// A language pattern filled in: %s takes the arguments in order, %N$s by place.
std::string apply_pattern(std::string_view pattern, const std::vector<std::string>& args);

} // namespace mcpp::nbt
