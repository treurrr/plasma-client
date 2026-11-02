#pragma once
#include "mcpp/byte_buffer.hpp"

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace mcpp::json {

class Value;
using Object = std::map<std::string, Value, std::less<>>;
using Array = std::vector<Value>;

class Value {
public:
    using Storage = std::variant<std::monostate, bool, double, std::string, Array, Object>;

    Value() = default;
    explicit Value(Storage storage) : storage_(std::move(storage)) {}

    bool is_null() const { return std::holds_alternative<std::monostate>(storage_); }

    const Value* find(std::string_view key) const;
    const Value* at(size_t index) const;

    std::optional<bool> as_bool() const;
    std::optional<double> as_number() const;
    const std::string* as_string() const;
    const Array* as_array() const;
    const Object* as_object() const;

    std::string string_or(std::string_view fallback) const;
    double number_or(double fallback) const;

private:
    Storage storage_;
};

// Throws ProtocolError on malformed input.
Value parse(std::string_view text);

// A chat component is a bare string or {"text":..,"extra":[..]}.
std::string flatten_component(const Value& value);

std::string strip_formatting(std::string_view text);

} // namespace mcpp::json
