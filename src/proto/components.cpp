#include "mcpp/proto/components.hpp"
#include "components_gen.hpp"

#include "mcpp/json.hpp"
#include "mcpp/session.hpp"

#include <algorithm>
#include <cctype>
#include <deque>
#include <format>
#include <map>
#include <mutex>

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

const Value* Components::find(std::string_view name, int32_t protocol) const {
    const int32_t type = component_type(name, protocol);
    return type < 0 ? nullptr : find(type);
}

bool Components::removes(int32_t type) const {
    return std::find(removed_.begin(), removed_.end(), type) != removed_.end();
}

namespace {

struct Node {
    enum class Kind : uint8_t {
        Unit, Bool, Byte, UByte, Short, Int, Long, VarInt, OptVarInt, VarLong, Float, Double, String,
        Bytes, Nbt, Text, Uuid, Position, Slot, TypedComponent, Ref, List, Optional, Enum, Id, IdOr, IdSet,
        Record, OneOf, Switch,
    };
    Kind kind = Kind::Unit;
    // The registry of an id, or the name a reference points to.
    std::string_view argument;
    // Fields of a record, cases of a oneof or a switch; the one element of a list or option.
    std::vector<std::pair<std::string_view, const Node*>> parts;
    std::vector<std::pair<int64_t, std::string_view>> labels;
    // Resolved once the table is built, so decoding looks nothing up by name:
    // what a reference names, and per protocol which case each registry entry
    // of a switch picks (-1 for one that carries nothing).
    const Node* target = nullptr;
    struct Cases {
        int32_t protocol = 0;
        const std::vector<std::string_view>* entries = nullptr;
        std::vector<int16_t> part;
    };
    std::vector<Cases> cases;
};

class Parser {
public:
    Parser(std::string_view text, std::deque<Node>& nodes) : text_(text), nodes_(nodes) {}

    const Node* parse() {
        const Node* node = layout();
        skip();
        if (at_ < text_.size()) fail("trailing text");
        return node;
    }

private:
    [[noreturn]] void fail(std::string_view what) const {
        throw ProtocolError(std::format("layout \"{}\": {} at {}", text_, what, at_));
    }

    void skip() {
        while (at_ < text_.size() && text_[at_] == ' ') ++at_;
    }

    bool take(char c) {
        skip();
        if (at_ < text_.size() && text_[at_] == c) {
            ++at_;
            return true;
        }
        return false;
    }

    void expect(char c) {
        if (!take(c)) fail(std::format("expected '{}'", c));
    }

    std::string_view word() {
        skip();
        const size_t start = at_;
        while (at_ < text_.size() &&
               (std::islower(static_cast<unsigned char>(text_[at_])) != 0 ||
                std::isdigit(static_cast<unsigned char>(text_[at_])) != 0 ||
                text_[at_] == '_' || text_[at_] == '/' || text_[at_] == '.')) {
            ++at_;
        }
        if (at_ == start) fail("expected a name");
        return text_.substr(start, at_ - start);
    }

    std::string_view until(std::string_view stops) {
        skip();
        const size_t start = at_;
        while (at_ < text_.size() && stops.find(text_[at_]) == std::string_view::npos) ++at_;
        std::string_view out = text_.substr(start, at_ - start);
        while (!out.empty() && out.back() == ' ') out.remove_suffix(1);
        if (out.empty()) fail("expected a registry");
        return out;
    }

    Node& make(Node::Kind kind) {
        Node& node = nodes_.emplace_back();
        node.kind = kind;
        return node;
    }

    void named_parts(Node& node, char close, bool commas) {
        while (!take(close)) {
            if (commas && !node.parts.empty()) expect(',');
            const std::string_view name = word();
            expect(':');
            node.parts.emplace_back(name, layout());
        }
    }

    const Node* layout() {
        using K = Node::Kind;
        if (take('-')) return &make(K::Unit);
        if (take('@')) {
            Node& node = make(K::Ref);
            node.argument = word();
            return &node;
        }
        if (take('{')) {
            Node& node = make(K::Record);
            named_parts(node, '}', false);
            return &node;
        }
        const std::string_view name = word();
        static const std::map<std::string_view, K> primitives = {
            {"bool", K::Bool}, {"byte", K::Byte}, {"ubyte", K::UByte}, {"short", K::Short},
            {"int", K::Int}, {"long", K::Long}, {"varint", K::VarInt}, {"optvarint", K::OptVarInt}, {"varlong", K::VarLong},
            {"float", K::Float}, {"double", K::Double}, {"string", K::String},
            {"identifier", K::String}, {"bytes", K::Bytes}, {"nbt", K::Nbt}, {"component", K::Text}, {"uuid", K::Uuid},
            {"position", K::Position}, {"slot", K::Slot}, {"typed_component", K::TypedComponent},
        };
        if (const auto found = primitives.find(name); found != primitives.end()) {
            return &make(found->second);
        }
        expect('(');
        if (name == "list" || name == "opt") {
            Node& node = make(name == "list" ? K::List : K::Optional);
            node.parts.emplace_back(std::string_view{}, layout());
            expect(')');
            return &node;
        }
        if (name == "id" || name == "idset") {
            Node& node = make(name == "id" ? K::Id : K::IdSet);
            node.argument = until(")");
            expect(')');
            return &node;
        }
        if (name == "idor") {
            Node& node = make(K::IdOr);
            node.argument = until(",");
            expect(',');
            node.parts.emplace_back(std::string_view{}, layout());
            expect(')');
            return &node;
        }
        if (name == "enum") {
            Node& node = make(K::Enum);
            int64_t next = 0;
            while (!take(')')) {
                if (!node.labels.empty()) expect(',');
                const std::string_view label = word();
                if (take('=')) next = std::stoll(std::string(word()));
                node.labels.emplace_back(next++, label);
            }
            return &node;
        }
        if (name == "oneof") {
            Node& node = make(K::OneOf);
            named_parts(node, ')', true);
            return &node;
        }
        if (name == "switch") {
            Node& node = make(K::Switch);
            node.argument = until(")");
            expect(')');
            expect('{');
            named_parts(node, '}', false);
            return &node;
        }
        fail(std::format("unknown layout {}", name));
    }

    std::string_view text_;
    std::deque<Node>& nodes_;
    size_t at_ = 0;
};

struct Table {
    std::deque<Node> nodes;
    std::map<std::string_view, const Node*, std::less<>> types;
    std::map<std::string_view, const Node*, std::less<>> components;
    std::string problem;
};

constexpr int32_t kProtocols[] = {774, 776};

// Fills in what a node refers to; the first problem, or an empty string.
std::string link(Node& node, const std::map<std::string_view, const Node*, std::less<>>& types) {
    if (node.kind == Node::Kind::Ref) {
        const auto found = types.find(node.argument);
        if (found == types.end()) return std::format("no type @{}", node.argument);
        node.target = found->second;
    }
    if (node.kind == Node::Kind::Switch) {
        for (const int32_t protocol : kProtocols) {
            const auto* entries = generated::registry_entries(node.argument, protocol);
            if (entries == nullptr) continue;
            Node::Cases cases{protocol, entries, std::vector<int16_t>(entries->size(), -1)};
            for (size_t e = 0; e < entries->size(); ++e) {
                std::string_view bare = (*entries)[e];
                if (bare.starts_with("minecraft:")) bare.remove_prefix(10);
                for (size_t k = 0; k < node.parts.size(); ++k) {
                    if (node.parts[k].first == bare) cases.part[e] = static_cast<int16_t>(k);
                }
            }
            node.cases.push_back(std::move(cases));
        }
        if (node.cases.empty()) return std::format("no registry {}", node.argument);
    }
    return {};
}

const Table& table() {
    static const Table built = [] {
        Table t;
        try {
            for (const auto& [name, layout] : generated::component_types()) {
                t.types.emplace(name, Parser(layout, t.nodes).parse());
            }
            for (const auto& [name, layout] : generated::component_layouts()) {
                t.components.emplace(name, Parser(layout, t.nodes).parse());
            }
            for (Node& node : t.nodes) {
                std::string problem = link(node, t.types);
                if (!problem.empty()) t.problem = std::move(problem);
            }
        } catch (const ProtocolError& error) {
            t.problem = error.what();
        }
        return t;
    }();
    return built;
}

std::string uuid_text(ByteBuffer& in) {
    std::string out;
    for (int half = 0; half < 2; ++half) {
        const auto bits = static_cast<uint64_t>(in.read_i64());
        out += std::format("{:016x}", bits);
    }
    return std::format("{}-{}-{}-{}-{}", out.substr(0, 8), out.substr(8, 4), out.substr(12, 4),
                       out.substr(16, 4), out.substr(20));
}

BlockPos unpack_position(int64_t packed) {
    return {static_cast<int32_t>(packed >> 38), static_cast<int32_t>(packed << 52 >> 52),
            static_cast<int32_t>(packed << 26 >> 38)};
}

int32_t read_count(ByteBuffer& in) {
    const int32_t count = in.read_varint();
    if (count < 0 || static_cast<size_t>(count) > in.remaining()) {
        throw ProtocolError(std::format("a list of {} with {} bytes left", count, in.remaining()));
    }
    return count;
}

constexpr int kMaxDepth = 64;

Value decode(const Node& node, ByteBuffer& in, int32_t protocol, int depth);

Value decode_part(const Node& node, ByteBuffer& in, int32_t protocol, int depth) {
    return decode(*node.parts.front().second, in, protocol, depth + 1);
}

Value decode(const Node& node, ByteBuffer& in, int32_t protocol, int depth) {
    using K = Node::Kind;
    if (depth > kMaxDepth) throw ProtocolError("component nested too deep");
    switch (node.kind) {
    case K::Unit: return Value{};
    case K::Bool: return Value{in.read_bool()};
    case K::Byte: return Value{int64_t{static_cast<int8_t>(in.read_u8())}};
    case K::UByte: return Value{int64_t{in.read_u8()}};
    case K::Short: return Value{int64_t{static_cast<int16_t>(in.read_u16())}};
    case K::Int: return Value{int64_t{in.read_i32()}};
    case K::Long: return Value{in.read_i64()};
    case K::VarInt: return Value{int64_t{in.read_varint()}};
    case K::OptVarInt: {
        const int32_t raw = in.read_varint();
        return raw == 0 ? Value{} : Value{int64_t{raw} - 1};
    }
    case K::VarLong: return Value{in.read_varlong()};
    case K::Float: return Value{static_cast<double>(in.read_f32())};
    case K::Double: return Value{in.read_f64()};
    case K::String: return Value{in.read_string()};
    case K::Bytes: {
        const auto bytes = in.read_bytes(static_cast<size_t>(read_count(in)));
        return Value{std::string(bytes.begin(), bytes.end())};
    }
    case K::Nbt: return Value{nbt::read_network_tag(in)};
    case K::Text: return Value{nbt::read_network_tag(in), Value::Note::Text, {}};
    case K::Uuid: return Value{uuid_text(in)};
    case K::Position: return Value{unpack_position(in.read_i64())};
    case K::Slot:
        return Value{std::shared_ptr<const ItemStack>(
            std::make_shared<ItemStack>(read_item_stack(in, protocol)))};
    case K::TypedComponent: {
        const int32_t type = in.read_varint();
        Value::Record record;
        record.push_back({"type", Value{int64_t{type}, Value::Note::Label, component_name(type, protocol)}});
        record.push_back({"value", read_component(in, type, protocol)});
        return Value{std::move(record)};
    }
    case K::Ref: {
        if (node.target == nullptr) throw ProtocolError(std::format("no type @{}", node.argument));
        return decode(*node.target, in, protocol, depth + 1);
    }
    case K::List: {
        const int32_t count = read_count(in);
        Value::List items;
        items.reserve(static_cast<size_t>(count));
        for (int32_t i = 0; i < count; ++i) items.push_back(decode_part(node, in, protocol, depth));
        return Value{std::move(items)};
    }
    case K::Optional:
        return in.read_bool() ? decode_part(node, in, protocol, depth) : Value{};
    case K::Enum: {
        const int64_t value = in.read_varint();
        for (const auto& [number, label] : node.labels) {
            if (number == value) return Value{value, Value::Note::Label, label};
        }
        return Value{value};
    }
    case K::Id: return Value{int64_t{in.read_varint()}, Value::Note::Registry, node.argument};
    case K::IdOr: {
        const int32_t id = in.read_varint();
        if (id == 0) return decode_part(node, in, protocol, depth);
        return Value{int64_t{id - 1}, Value::Note::Registry, node.argument};
    }
    case K::IdSet: {
        const int32_t kind = in.read_varint();
        if (kind == 0) return Value{"#" + in.read_string(), Value::Note::Registry, node.argument};
        if (kind < 0 || static_cast<size_t>(kind - 1) > in.remaining()) {
            throw ProtocolError(std::format("an id set of {}", kind - 1));
        }
        Value::List ids;
        for (int32_t i = 1; i < kind; ++i) {
            ids.push_back(Value{int64_t{in.read_varint()}, Value::Note::Registry, node.argument});
        }
        return Value{std::move(ids), Value::Note::Registry, node.argument};
    }
    case K::Record: {
        Value::Record record;
        record.reserve(node.parts.size());
        for (const auto& [name, part] : node.parts) {
            record.push_back({name, decode(*part, in, protocol, depth + 1)});
        }
        return Value{std::move(record)};
    }
    case K::OneOf: {
        const int32_t which = in.read_varint();
        if (which < 0 || static_cast<size_t>(which) >= node.parts.size()) {
            throw ProtocolError(std::format("case {} of {}", which, node.parts.size()));
        }
        const auto& [name, part] = node.parts[static_cast<size_t>(which)];
        Value::Record record;
        record.push_back({name, decode(*part, in, protocol, depth + 1)});
        return Value{std::move(record)};
    }
    case K::Switch: {
        const int32_t id = in.read_varint();
        const Node::Cases* cases = nullptr;
        for (const auto& c : node.cases) {
            if (c.protocol == protocol) cases = &c;
        }
        if (cases == nullptr || id < 0 || static_cast<size_t>(id) >= cases->entries->size()) {
            throw ProtocolError(std::format("{} has no entry {} in protocol {}", node.argument, id,
                                            protocol));
        }
        Value::Record record;
        record.push_back(
            {"type", Value{int64_t{id}, Value::Note::Label, (*cases->entries)[static_cast<size_t>(id)]}});
        const int16_t which = cases->part[static_cast<size_t>(id)];
        if (which < 0) return Value{std::move(record)};
        const Node* part = node.parts[static_cast<size_t>(which)].second;
        if (part->kind == K::Record) {
            record.reserve(1 + part->parts.size());
            for (const auto& [field, inner] : part->parts) {
                record.push_back({field, decode(*inner, in, protocol, depth + 1)});
            }
        } else {
            record.push_back({"value", decode(*part, in, protocol, depth + 1)});
        }
        return Value{std::move(record)};
    }
    }
    throw ProtocolError("unknown layout node");
}

} // namespace

int32_t component_type(std::string_view name, int32_t protocol) {
    const auto* names = generated::registry_entries("minecraft:data_component_type", protocol);
    if (names == nullptr) return -1;
    const auto found = std::find(names->begin(), names->end(), name);
    return found == names->end() ? -1 : static_cast<int32_t>(found - names->begin());
}

std::string_view component_name(int32_t type, int32_t protocol) {
    const auto* names = generated::registry_entries("minecraft:data_component_type", protocol);
    if (names == nullptr || type < 0 || static_cast<size_t>(type) >= names->size()) return {};
    return (*names)[static_cast<size_t>(type)];
}

Value read_component(ByteBuffer& in, int32_t type, int32_t protocol) {
    const std::string_view name = component_name(type, protocol);
    const auto& components = table().components;
    const auto found = components.find(name);
    if (name.empty() || found == components.end()) {
        throw ProtocolError(std::format("no layout for data component {} in protocol {}", type,
                                        protocol));
    }
    return decode(*found->second, in, protocol, 0);
}

Value read_named(ByteBuffer& in, std::string_view type, int32_t protocol) {
    const auto& types = table().types;
    const auto found = types.find(type);
    if (found == types.end()) throw ProtocolError(std::format("no type @{}", type));
    return decode(*found->second, in, protocol, 0);
}

Value read_layout(ByteBuffer& in, std::string_view layout, int32_t protocol) {
    static std::mutex guard;
    static std::deque<Node> nodes;
    static std::map<const char*, const Node*> parsed;
    const Node* node = nullptr;
    {
        std::lock_guard lock(guard);
        auto& slot = parsed[layout.data()];
        if (slot == nullptr) {
            const size_t first = nodes.size();
            slot = Parser(layout, nodes).parse();
            for (size_t k = first; k < nodes.size(); ++k) {
                const std::string problem = link(nodes[k], table().types);
                if (!problem.empty()) throw ProtocolError(problem);
            }
        }
        node = slot;
    }
    return decode(*node, in, protocol, 0);
}

ItemStack read_item_stack(ByteBuffer& in, int32_t protocol) {
    ItemStack stack;
    const int32_t count = in.read_varint();
    if (count <= 0) return stack;
    stack.count = count;
    stack.id = in.read_varint();
    const int32_t added = read_count(in);
    const int32_t removed = read_count(in);
    for (int32_t i = 0; i < added; ++i) {
        const int32_t type = in.read_varint();
        stack.components.add(type, read_component(in, type, protocol));
    }
    for (int32_t i = 0; i < removed; ++i) stack.components.remove(in.read_varint());
    return stack;
}

std::string check_layouts() { return table().problem; }

namespace {

void describe_into(std::string& out, const Value& value, const RegistryLookup& name_of,
                   const nbt::TranslateLookup& lookup, int depth);

void describe_nbt(std::string& out, const nbt::Value& value, int depth) {
    if (depth > kMaxDepth) {
        out += "...";
    } else if (const auto* compound = value.as_compound()) {
        out += '{';
        bool first = true;
        for (const auto& [key, inner] : *compound) {
            if (!first) out += ',';
            first = false;
            out += key + ':';
            describe_nbt(out, inner, depth + 1);
        }
        out += '}';
    } else if (const auto* list = value.as_list()) {
        out += '[';
        for (size_t k = 0; k < list->size(); ++k) {
            if (k != 0) out += ',';
            describe_nbt(out, (*list)[k], depth + 1);
        }
        out += ']';
    } else if (const auto* text = value.as_string()) {
        out += std::format("\"{}\"", *text);
    } else if (const auto number = value.as_int()) {
        out += std::to_string(*number);
    } else if (const auto real = value.as_double()) {
        out += std::format("{}", *real);
    } else {
        out += "[array]";
    }
}

void describe_stack(std::string& out, const ItemStack& stack, const RegistryLookup& name_of,
                    const nbt::TranslateLookup& lookup, int depth) {
    if (stack.count <= 0) {
        out += "empty";
        return;
    }
    const std::string item = name_of ? name_of("minecraft:item", stack.id) : std::string{};
    out += std::format("{} x{}", item.empty() ? std::to_string(stack.id) : item, stack.count);
    if (stack.components.empty()) return;
    out += " [";
    bool first = true;
    for (const auto& entry : stack.components.added()) {
        if (!first) out += ", ";
        first = false;
        const std::string name = name_of ? name_of("minecraft:data_component_type", entry.type)
                                         : std::string{};
        out += name.empty() ? std::to_string(entry.type) : name;
        if (!entry.value.empty()) {
            out += '=';
            describe_into(out, entry.value, name_of, lookup, depth + 1);
        }
    }
    for (const int32_t type : stack.components.removed()) {
        if (!first) out += ", ";
        first = false;
        const std::string name = name_of ? name_of("minecraft:data_component_type", type)
                                         : std::string{};
        out += '!' + (name.empty() ? std::to_string(type) : name);
    }
    out += ']';
}

void describe_into(std::string& out, const Value& value, const RegistryLookup& name_of,
                   const nbt::TranslateLookup& lookup, int depth) {
    if (depth > kMaxDepth) {
        out += "...";
        return;
    }
    const auto& storage = value.storage();
    if (std::holds_alternative<std::monostate>(storage)) {
        out += "-";
    } else if (const auto* b = std::get_if<bool>(&storage)) {
        out += *b ? "true" : "false";
    } else if (const auto* i = std::get_if<int64_t>(&storage)) {
        if (!value.registry().empty()) {
            const std::string name = name_of ? name_of(value.registry(), *i) : std::string{};
            out += name.empty() ? std::format("{}#{}", value.registry(), *i) : name;
        } else {
            out += value.label().empty() ? std::to_string(*i) : std::string(value.label());
        }
    } else if (const auto* d = std::get_if<double>(&storage)) {
        out += std::format("{}", *d);
    } else if (const auto* s = std::get_if<std::string>(&storage)) {
        if (s->size() > 80) {
            out += std::format("\"{}...\" ({} bytes)", s->substr(0, 40), s->size());
        } else {
            out += std::format("\"{}\"", *s);
        }
    } else if (const auto* n = std::get_if<nbt::Value>(&storage)) {
        if (value.note_kind() == Value::Note::Text) {
            out += std::format("'{}'", nbt::component_to_text(*n, lookup));
        } else {
            describe_nbt(out, *n, depth);
        }
    } else if (const auto* p = std::get_if<BlockPos>(&storage)) {
        out += std::format("({}, {}, {})", p->x, p->y, p->z);
    } else if (const auto* list = std::get_if<Value::List>(&storage)) {
        out += '[';
        for (size_t k = 0; k < list->size(); ++k) {
            if (k != 0) out += ", ";
            describe_into(out, (*list)[k], name_of, lookup, depth + 1);
        }
        out += ']';
    } else if (const auto* record = std::get_if<Value::Record>(&storage)) {
        out += '{';
        for (size_t k = 0; k < record->size(); ++k) {
            if (k != 0) out += ' ';
            out += (*record)[k].name;
            out += ':';
            describe_into(out, (*record)[k].value, name_of, lookup, depth + 1);
        }
        out += '}';
    } else if (const ItemStack* stack = value.as_stack()) {
        out += '(';
        describe_stack(out, *stack, name_of, lookup, depth + 1);
        out += ')';
    }
}

} // namespace

std::string describe(const Value& value, const RegistryLookup& name_of,
                     const nbt::TranslateLookup& lookup) {
    std::string out;
    describe_into(out, value, name_of, lookup, 0);
    return out;
}

std::string describe(const ItemStack& stack, const RegistryLookup& name_of,
                     const nbt::TranslateLookup& lookup) {
    std::string out;
    describe_stack(out, stack, name_of, lookup, 0);
    return out;
}

} // namespace mcpp::proto
