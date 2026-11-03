#pragma once
#include "mcpp/byte_buffer.hpp"
#include "mcpp/nbt.hpp"

#include <cstdint>
#include <functional>
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
    // By registry name, "minecraft:damage", in the given protocol's numbering.
    const Value* find(std::string_view name, int32_t protocol) const;
    bool removes(int32_t type) const;

    void add(int32_t type, Value value) { added_.push_back({type, std::move(value)}); }
    void remove(int32_t type) { removed_.push_back(type); }

private:
    std::vector<Entry> added_;
    std::vector<int32_t> removed_;
};

// The data component types in a protocol's own numbering, from the reports:
// -1 and an empty name for one the protocol does not have.
int32_t component_type(std::string_view name, int32_t protocol);
std::string_view component_name(int32_t type, int32_t protocol);

// One stack the way every slot on the wire is written: a count, and for a
// count above zero the item, the components it adds and the ones it removes.
// A component without a layout throws, because nothing after it can be found.
ItemStack read_item_stack(ByteBuffer& in, int32_t protocol);
// One component's body, and one body of a named type in the layout table
// ("particle", "resolvable_profile").
Value read_component(ByteBuffer& in, int32_t type, int32_t protocol);
Value read_named(ByteBuffer& in, std::string_view type, int32_t protocol);
// One body of a layout written out, in the table's own notation; `layout`
// must outlive the program, since the decoded names point into it.
Value read_layout(ByteBuffer& in, std::string_view layout, int32_t protocol);
// Whether every layout in the table parses and every @name it uses exists;
// the first problem otherwise.
std::string check_layouts();

// A value on one line, the way inspect prints it. Ids stay numbers unless
// `name_of` knows the registry entry.
using RegistryLookup = std::function<std::string(std::string_view registry, int64_t id)>;
std::string describe(const Value& value, const RegistryLookup& name_of = {},
                     const nbt::TranslateLookup& lookup = {});
std::string describe(const ItemStack& stack, const RegistryLookup& name_of = {},
                     const nbt::TranslateLookup& lookup = {});

} // namespace mcpp::proto
