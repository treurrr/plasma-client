#pragma once
#include "mcpp/proto/components.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace mcpp::proto {

// The server's command graph: every node with its children by index, the
// way the commands packet lists them. Chat completion walks it from the root.
struct CommandNode {
    enum class Kind : uint8_t { Root, Literal, Argument };
    Kind kind = Kind::Root;
    bool executable = false;
    bool restricted = false;   // needs a permission level above 0
    std::vector<int32_t> children;
    int32_t redirect = -1;
    std::string name;          // a literal's word, an argument's label
    std::string parser;        // "brigadier:integer", for an argument
    // A parser's own settings: an integer's bounds, a string's kind, the
    // registry a resource argument names. Empty for parsers without any.
    Value properties;
    std::string suggestions;   // "minecraft:ask_server", or empty
};

struct CommandTree {
    std::vector<CommandNode> nodes;
    int32_t root = -1;
    const CommandNode* root_node() const {
        return root >= 0 && static_cast<size_t>(root) < nodes.size() ? &nodes[root] : nullptr;
    }
};

// Throws on a parser the protocol does not have: the nodes after it cannot
// be found.
CommandTree read_commands(ByteBuffer& in, int32_t protocol);

} // namespace mcpp::proto
