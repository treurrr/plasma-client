#pragma once
#include "mcpp/byte_buffer.hpp"
#include "mcpp/nbt.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace mcpp {
struct ItemStack;
}

namespace mcpp::proto {

struct BlockPos {
    int32_t x = 0;
    int32_t y = 0;
    int32_t z = 0;
    bool operator==(const BlockPos&) const = default;
};

// A decoded wire body whose shape is only known from a layout table: a data
// component, an entity data value, a particle's options. Numbers are widened
// to int64 and double; records keep their fields in wire order.
class Value {
public:
    struct Field;
    using List = std::vector<Value>;
    using Record = std::vector<Field>;
    using Storage = std::variant<std::monostate, bool, int64_t, double, std::string, nbt::Value,
                                 BlockPos, List, Record, std::shared_ptr<const ItemStack>>;

    Value() = default;
    explicit Value(Storage storage, std::string_view note = {})
        : storage_(std::move(storage)), note_(note) {}

    bool empty() const { return std::holds_alternative<std::monostate>(storage_); }
    const Storage& storage() const { return storage_; }
    // What an integer means: the name of an enum value, or the registry an
    // id points into ("minecraft:enchantment").
    std::string_view note() const { return note_; }

    std::optional<int64_t> as_int() const;
    std::optional<double> as_double() const;   // ints too
    std::optional<bool> as_bool() const;
    const std::string* as_string() const;
    const nbt::Value* as_nbt() const;
    const BlockPos* as_pos() const;
    const List* as_list() const;
    const Record* as_record() const;
    const ItemStack* as_stack() const;
    // A record's field by name; nullptr when this is no record or has no such field.
    const Value* field(std::string_view name) const;

private:
    Storage storage_;
    std::string_view note_;
};

struct Value::Field {
    std::string_view name;
    Value value;
};

// The data components a stack carries on top of its item's defaults, and the
// defaults it takes away. Types are ids in minecraft:data_component_type.
class Components {
public:
    struct Entry {
        int32_t type = 0;
        Value value;
    };

    bool empty() const { return added_.empty() && removed_.empty(); }
    std::span<const Entry> added() const { return added_; }
    std::span<const int32_t> removed() const { return removed_; }

    const Value* find(int32_t type) const;
    bool removes(int32_t type) const;

    void add(int32_t type, Value value) { added_.push_back({type, std::move(value)}); }
    void remove(int32_t type) { removed_.push_back(type); }

private:
    std::vector<Entry> added_;
    std::vector<int32_t> removed_;
};

} // namespace mcpp::proto
