#include "mcpp/proto/commands.hpp"
#include "components_gen.hpp"

#include <format>

namespace mcpp::proto {
namespace {

constexpr uint8_t kTypeMask = 0x03;
constexpr uint8_t kExecutable = 0x04;
constexpr uint8_t kRedirect = 0x08;
constexpr uint8_t kSuggestions = 0x10;
constexpr uint8_t kRestricted = 0x20;

// Minimum and maximum, each there only when its bit in the flags byte is.
template <typename Read>
Value bounds(ByteBuffer& in, Read read) {
    const uint8_t flags = in.read_u8();
    Value::Record record;
    if ((flags & 0x01) != 0) record.push_back({"min", read()});
    if ((flags & 0x02) != 0) record.push_back({"max", read()});
    return Value{std::move(record)};
}

Value read_properties(ByteBuffer& in, std::string_view parser) {
    // `FloatArgumentInfo.deserializeFromNetwork`: optional float bounds.
    if (parser == "brigadier:float") {
        return bounds(in, [&] { return Value{static_cast<double>(in.read_f32())}; });
    }
    // `DoubleArgumentInfo.deserializeFromNetwork`: optional double bounds.
    if (parser == "brigadier:double") return bounds(in, [&] { return Value{in.read_f64()}; });
    // `IntegerArgumentInfo.deserializeFromNetwork`: optional int bounds.
    if (parser == "brigadier:integer") {
        return bounds(in, [&] { return Value{int64_t{in.read_i32()}}; });
    }
    // `LongArgumentInfo.deserializeFromNetwork`: optional long bounds.
    if (parser == "brigadier:long") return bounds(in, [&] { return Value{in.read_i64()}; });
    // `StringArgumentSerializer.deserializeFromNetwork`: the string kind.
    if (parser == "brigadier:string") {
        static constexpr std::string_view kKinds[] = {"single_word", "quotable_phrase",
                                                      "greedy_phrase"};
        const int32_t kind = in.read_varint();
        return Value{int64_t{kind}, Value::Note::Label,
                     kind >= 0 && kind < 3 ? kKinds[kind] : std::string_view{}};
    }
    if (parser == "minecraft:entity" || parser == "minecraft:score_holder") {
        return Value{int64_t{in.read_u8()}};
    }
    if (parser == "minecraft:time") return Value{int64_t{in.read_i32()}};
    if (parser == "minecraft:resource_or_tag" || parser == "minecraft:resource_or_tag_key" ||
        parser == "minecraft:resource" || parser == "minecraft:resource_key" ||
        parser == "minecraft:resource_selector") {
        return Value{in.read_string()};
    }
    return Value{};
}

} // namespace

// `ClientboundCommandsPacket.ClientboundCommandsPacket`: the node list, then the root.
CommandTree read_commands(ByteBuffer& in, int32_t protocol) {
    const auto* parsers = generated::registry_entries("minecraft:command_argument_type", protocol);
    if (parsers == nullptr) {
        throw ProtocolError(std::format("no command parsers for protocol {}", protocol));
    }
    CommandTree tree;
    const int32_t count = in.read_varint();
    if (count < 0 || static_cast<size_t>(count) > in.remaining()) {
        throw ProtocolError(std::format("{} command nodes", count));
    }
    tree.nodes.reserve(static_cast<size_t>(count));
    for (int32_t i = 0; i < count; ++i) {
        // `ClientboundCommandsPacket.readNode`, `ClientboundCommandsPacket.read`: one node.
        CommandNode node;
        const uint8_t flags = in.read_u8();
        node.kind = static_cast<CommandNode::Kind>(flags & kTypeMask);
        if ((flags & kTypeMask) == 3) throw ProtocolError("command node of type 3");
        node.executable = (flags & kExecutable) != 0;
        node.restricted = (flags & kRestricted) != 0;
        const int32_t children = in.read_varint();
        if (children < 0 || static_cast<size_t>(children) > in.remaining()) {
            throw ProtocolError(std::format("a command node with {} children", children));
        }
        node.children.resize(static_cast<size_t>(children));
        for (auto& child : node.children) child = in.read_varint();
        if ((flags & kRedirect) != 0) node.redirect = in.read_varint();
        if (node.kind != CommandNode::Kind::Root) node.name = in.read_string();
        if (node.kind == CommandNode::Kind::Argument) {
            const int32_t parser = in.read_varint();
            if (parser < 0 || static_cast<size_t>(parser) >= parsers->size()) {
                throw ProtocolError(std::format("command parser {}", parser));
            }
            node.parser = std::string((*parsers)[static_cast<size_t>(parser)]);
            node.properties = read_properties(in, node.parser);
        }
        if ((flags & kSuggestions) != 0) node.suggestions = in.read_string();
        tree.nodes.push_back(std::move(node));
    }
    tree.root = in.read_varint();
    return tree;
}

} // namespace mcpp::proto
