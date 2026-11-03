#include "mcpp/proto/components.hpp"
#include "mcpp/proto/metadata.hpp"
#include "mcpp/session.hpp"

#include <bit>
#include <format>
#include <iostream>
#include <sstream>
#include <string>

// The C++ half of CodecOracle. Each line on stdin is the oracle's own: a case
// name, kind:hex of the bytes the game wrote, and what it expects. This decodes
// the bytes and prints every value in wire order, in the oracle's notation.
namespace {

using mcpp::proto::Value;

constexpr int32_t kProtocol = 774;

std::string escape(std::string_view text) {
    std::string out;
    for (const char c : text) {
        if (c == ' ' || c == ',' || c == '%' || c == '\n') {
            out += std::format("%{:02X}", static_cast<unsigned char>(c));
        } else {
            out += c;
        }
    }
    return out;
}

std::string bits(double value) { return std::to_string(std::bit_cast<int64_t>(value)); }

void nbt_text(std::string& out, const mcpp::nbt::Value& value) {
    if (const auto* compound = value.as_compound()) {
        out += '{';
        bool first = true;
        for (const auto& [key, inner] : *compound) {
            if (!first) out += ',';
            first = false;
            out += escape(key) + ':';
            nbt_text(out, inner);
        }
        out += '}';
    } else if (const auto* list = value.as_list()) {
        out += '[';
        for (size_t i = 0; i < list->size(); ++i) {
            if (i != 0) out += ',';
            nbt_text(out, (*list)[i]);
        }
        out += ']';
    } else if (const auto* text = value.as_string()) {
        out += '"' + escape(*text) + '"';
    } else if (const auto number = value.as_int()) {
        out += std::to_string(*number);
    } else if (const auto real = value.as_double()) {
        out += 'f' + bits(*real);
    } else {
        out += "[array]";
    }
}

void leaves(std::string& out, const Value& value);

void stack_leaves(std::string& out, const mcpp::ItemStack& stack) {
    out += '(' + std::to_string(stack.count);
    if (stack.count > 0) {
        out += ',' + std::to_string(stack.id);
        for (const auto& entry : stack.components.added()) {
            out += ',' + std::string(mcpp::proto::component_name(entry.type, kProtocol)) + '=';
            leaves(out, entry.value);
        }
        for (const int32_t type : stack.components.removed()) {
            out += ",!" + std::string(mcpp::proto::component_name(type, kProtocol));
        }
    }
    out += ')';
}

void leaves(std::string& out, const Value& value) {
    const auto& storage = value.storage();
    if (std::holds_alternative<std::monostate>(storage)) {
        out += '-';
    } else if (const auto* b = std::get_if<bool>(&storage)) {
        out += *b ? "true" : "false";
    } else if (const auto* i = std::get_if<int64_t>(&storage)) {
        out += value.label().empty() ? std::to_string(*i) : std::string(value.label());
    } else if (const auto* d = std::get_if<double>(&storage)) {
        out += 'f' + bits(*d);
    } else if (const auto* s = std::get_if<std::string>(&storage)) {
        out += escape(*s);
    } else if (const auto* n = std::get_if<mcpp::nbt::Value>(&storage)) {
        nbt_text(out, *n);
    } else if (const auto* p = std::get_if<mcpp::proto::BlockPos>(&storage)) {
        out += std::format("{},{},{}", p->x, p->y, p->z);
    } else if (const auto* list = std::get_if<Value::List>(&storage)) {
        out += '[';
        for (size_t k = 0; k < list->size(); ++k) {
            if (k != 0) out += ',';
            leaves(out, (*list)[k]);
        }
        out += ']';
    } else if (const auto* record = std::get_if<Value::Record>(&storage)) {
        if (record->size() == 1) {
            out += std::string(record->front().name) + '=';
            leaves(out, record->front().value);
            return;
        }
        out += '{';
        for (size_t k = 0; k < record->size(); ++k) {
            if (k != 0) out += ',';
            leaves(out, (*record)[k].value);
        }
        out += '}';
    } else if (const auto* stack = value.as_stack()) {
        stack_leaves(out, *stack);
    }
}

std::vector<uint8_t> unhex(std::string_view hex) {
    std::vector<uint8_t> out;
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        out.push_back(static_cast<uint8_t>(std::stoi(std::string(hex.substr(i, 2)), nullptr, 16)));
    }
    return out;
}

std::string decode(std::string_view kind, std::string_view hex) {
    mcpp::ByteBuffer in(unhex(hex));
    std::string out;
    if (kind == "stack") {
        stack_leaves(out, mcpp::proto::read_item_stack(in, kProtocol));
    } else if (kind == "meta") {
        const auto metadata = mcpp::proto::read_metadata(in, kProtocol);
        for (const auto& entry : metadata.entries()) {
            if (!out.empty()) out += ',';
            out += std::format("{}:{}=", entry.index, static_cast<int>(entry.type));
            leaves(out, entry.value);
        }
    } else if (kind.starts_with("named.")) {
        leaves(out, mcpp::proto::read_named(in, kind.substr(6), kProtocol));
    } else {
        return "unknown-kind";
    }
    if (in.remaining() != 0) out += std::format("+{}left", in.remaining());
    return out.empty() ? "-" : out;
}

} // namespace

int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream words(line);
        std::string name;
        std::string input;
        words >> name >> input;
        const size_t colon = input.find(':');
        try {
            std::cout << decode(std::string_view(input).substr(0, colon),
                                std::string_view(input).substr(colon + 1))
                      << '\n';
        } catch (const std::exception& error) {
            std::cout << "threw:" << escape(error.what()) << '\n';
        }
    }
}
