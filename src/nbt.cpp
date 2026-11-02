#include "mcpp/nbt.hpp"

#include <format>

namespace mcpp::nbt {
namespace {

enum class Tag : uint8_t {
    End = 0, Byte = 1, Short = 2, Int = 3, Long = 4, Float = 5, Double = 6,
    ByteArray = 7, String = 8, List = 9, Compound = 10, IntArray = 11, LongArray = 12,
};

constexpr int kMaxDepth = 64;

std::string read_name(ByteBuffer& buffer) {
    const uint16_t length = buffer.read_u16();
    auto bytes = buffer.read_bytes(length);
    return std::string(bytes.begin(), bytes.end());
}

Value read_payload(ByteBuffer& buffer, Tag tag, int depth) {
    if (depth > kMaxDepth) throw ProtocolError("nbt te diep genest");

    switch (tag) {
    case Tag::End:
        return Value{};
    case Tag::Byte:
        return Value{static_cast<int8_t>(buffer.read_u8())};
    case Tag::Short:
        return Value{static_cast<int16_t>(buffer.read_u16())};
    case Tag::Int:
        return Value{buffer.read_i32()};
    case Tag::Long:
        return Value{buffer.read_i64()};
    case Tag::Float:
        return Value{buffer.read_f32()};
    case Tag::Double:
        return Value{buffer.read_f64()};
    case Tag::String:
        return Value{read_name(buffer)};
    case Tag::ByteArray: {
        const int32_t count = buffer.read_i32();
        if (count < 0) throw ProtocolError("nbt byte-array met negatieve lengte");
        return Value{buffer.read_bytes(static_cast<size_t>(count))};
    }
    case Tag::IntArray: {
        const int32_t count = buffer.read_i32();
        if (count < 0) throw ProtocolError("nbt int-array met negatieve lengte");
        std::vector<int32_t> values(static_cast<size_t>(count));
        for (auto& value : values) value = buffer.read_i32();
        return Value{std::move(values)};
    }
    case Tag::LongArray: {
        const int32_t count = buffer.read_i32();
        if (count < 0) throw ProtocolError("nbt long-array met negatieve lengte");
        std::vector<int64_t> values(static_cast<size_t>(count));
        for (auto& value : values) value = buffer.read_i64();
        return Value{std::move(values)};
    }
    case Tag::List: {
        const auto element = static_cast<Tag>(buffer.read_u8());
        const int32_t count = buffer.read_i32();
        if (count < 0) throw ProtocolError("nbt lijst met negatieve lengte");
        List items;
        items.reserve(static_cast<size_t>(count));
        for (int32_t i = 0; i < count; ++i) items.push_back(read_payload(buffer, element, depth + 1));
        return Value{std::move(items)};
    }
    case Tag::Compound: {
        Compound fields;
        for (;;) {
            const auto entry = static_cast<Tag>(buffer.read_u8());
            if (entry == Tag::End) break;
            auto name = read_name(buffer);
            fields.emplace(std::move(name), read_payload(buffer, entry, depth + 1));
        }
        return Value{std::move(fields)};
    }
    }
    throw ProtocolError(std::format("onbekende nbt-tag {}", static_cast<int>(tag)));
}

// Minecraft patterns use %s in order and %N$s by position.
std::string apply_pattern(std::string_view pattern, const std::vector<std::string>& args) {
    std::string out;
    size_t next = 0;
    for (size_t i = 0; i < pattern.size(); ++i) {
        if (pattern[i] != '%') {
            out += pattern[i];
            continue;
        }
        if (i + 1 >= pattern.size()) break;
        if (pattern[i + 1] == '%') {
            out += '%';
            ++i;
            continue;
        }

        size_t cursor = i + 1;
        size_t index = 0;
        bool positional = false;
        while (cursor < pattern.size() && pattern[cursor] >= '0' && pattern[cursor] <= '9') {
            index = index * 10 + static_cast<size_t>(pattern[cursor] - '0');
            ++cursor;
            positional = true;
        }
        if (positional && cursor < pattern.size() && pattern[cursor] == '$') ++cursor;

        if (cursor >= pattern.size() || pattern[cursor] != 's') {
            out += '%';
            continue;
        }
        const size_t slot = positional ? index - 1 : next++;
        if (slot < args.size()) out += args[slot];
        i = cursor;
    }
    return out;
}

} // namespace

const Value* Value::find(std::string_view key) const {
    const auto* fields = std::get_if<Compound>(&storage_);
    if (!fields) return nullptr;
    auto it = fields->find(key);
    return it == fields->end() ? nullptr : &it->second;
}

const std::string* Value::as_string() const { return std::get_if<std::string>(&storage_); }
const List* Value::as_list() const { return std::get_if<List>(&storage_); }
const Compound* Value::as_compound() const { return std::get_if<Compound>(&storage_); }

std::optional<int64_t> Value::as_int() const {
    if (const auto* v = std::get_if<int8_t>(&storage_)) return *v;
    if (const auto* v = std::get_if<int16_t>(&storage_)) return *v;
    if (const auto* v = std::get_if<int32_t>(&storage_)) return *v;
    if (const auto* v = std::get_if<int64_t>(&storage_)) return *v;
    return std::nullopt;
}

std::optional<double> Value::as_double() const {
    if (const auto* v = std::get_if<float>(&storage_)) return *v;
    if (const auto* v = std::get_if<double>(&storage_)) return *v;
    return std::nullopt;
}

std::string Value::string_or(std::string_view fallback) const {
    const auto* text = as_string();
    return text ? *text : std::string(fallback);
}

Value read_network_tag(ByteBuffer& buffer) {
    const auto tag = static_cast<Tag>(buffer.read_u8());
    if (tag == Tag::End) return Value{};
    return read_payload(buffer, tag, 0);
}

std::string component_to_text(const Value& value, const TranslateLookup& lookup) {
    if (const auto* text = value.as_string()) return *text;
    if (const auto number = value.as_int()) return std::to_string(*number);
    if (const auto number = value.as_double()) return std::format("{}", *number);

    if (const auto* items = value.as_list()) {
        std::string out;
        for (const auto& item : *items) out += component_to_text(item, lookup);
        return out;
    }

    // Mojang's codec wraps a bare number in a compound under the empty key.
    if (!value.find("text") && !value.find("translate")) {
        if (const auto* wrapped = value.find("")) return component_to_text(*wrapped, lookup);
    }

    std::string out;
    if (const auto* text = value.find("text")) out += text->string_or("");

    if (const auto* key = value.find("translate"); key && out.empty()) {
        std::vector<std::string> args;
        if (const auto* with = value.find("with")) {
            if (const auto* items = with->as_list()) {
                for (const auto& item : *items) args.push_back(component_to_text(item, lookup));
            }
        }

        const std::string* pattern = lookup ? lookup(key->string_or("")) : nullptr;
        if (pattern) {
            out += apply_pattern(*pattern, args);
        } else if (const auto* fallback = value.find("fallback")) {
            out += apply_pattern(fallback->string_or(""), args);
        } else {
            out += key->string_or("");
        }
    }

    if (const auto* extra = value.find("extra")) out += component_to_text(*extra, lookup);
    return out;
}

} // namespace mcpp::nbt
