#include "mcpp/proto/components.hpp"

#include <algorithm>

namespace mcpp::proto {

std::optional<int64_t> Value::as_int() const {
    if (const auto* v = std::get_if<int64_t>(&storage_)) return *v;
    if (const auto* v = std::get_if<bool>(&storage_)) return *v ? 1 : 0;
    return std::nullopt;
}

std::optional<double> Value::as_double() const {
    if (const auto* v = std::get_if<double>(&storage_)) return *v;
    if (const auto* v = std::get_if<int64_t>(&storage_)) return static_cast<double>(*v);
    return std::nullopt;
}

std::optional<bool> Value::as_bool() const {
    if (const auto* v = std::get_if<bool>(&storage_)) return *v;
    return std::nullopt;
}

const std::string* Value::as_string() const { return std::get_if<std::string>(&storage_); }
const nbt::Value* Value::as_nbt() const { return std::get_if<nbt::Value>(&storage_); }
const BlockPos* Value::as_pos() const { return std::get_if<BlockPos>(&storage_); }
const Value::List* Value::as_list() const { return std::get_if<List>(&storage_); }
const Value::Record* Value::as_record() const { return std::get_if<Record>(&storage_); }

const ItemStack* Value::as_stack() const {
    const auto* stack = std::get_if<std::shared_ptr<const ItemStack>>(&storage_);
    return stack != nullptr ? stack->get() : nullptr;
}

const Value* Value::field(std::string_view name) const {
    const Record* record = as_record();
    if (record == nullptr) return nullptr;
    for (const auto& f : *record) {
        if (f.name == name) return &f.value;
    }
    return nullptr;
}

const Value* Components::find(int32_t type) const {
    for (const auto& entry : added_) {
        if (entry.type == type) return &entry.value;
    }
    return nullptr;
}

bool Components::removes(int32_t type) const {
    return std::find(removed_.begin(), removed_.end(), type) != removed_.end();
}

} // namespace mcpp::proto
