#include "mcpp/json.hpp"

#include <format>

namespace mcpp::json {

const Value* Value::find(std::string_view key) const {
    const auto* object = as_object();
    if (object == nullptr) return nullptr;
    auto it = object->find(key);
    return it == object->end() ? nullptr : &it->second;
}

const Value* Value::at(size_t index) const {
    const auto* array = as_array();
    if (array == nullptr || index >= array->size()) return nullptr;
    return &(*array)[index];
}

std::optional<bool> Value::as_bool() const {
    if (const auto* v = std::get_if<bool>(&storage_)) return *v;
    return std::nullopt;
}

std::optional<double> Value::as_number() const {
    if (const auto* v = std::get_if<double>(&storage_)) return *v;
    return std::nullopt;
}

const std::string* Value::as_string() const { return std::get_if<std::string>(&storage_); }
const Array* Value::as_array() const { return std::get_if<Array>(&storage_); }
const Object* Value::as_object() const { return std::get_if<Object>(&storage_); }

std::string Value::string_or(std::string_view fallback) const {
    const auto* s = as_string();
    return s != nullptr ? *s : std::string(fallback);
}

double Value::number_or(double fallback) const { return as_number().value_or(fallback); }

namespace {

class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {}

    Value parse_document() {
        skip_whitespace();
        Value value = parse_value(0);
        skip_whitespace();
        if (pos_ != text_.size()) {
            throw ProtocolError(std::format("trailing JSON at offset {}", pos_));
        }
        return value;
    }

private:
    static constexpr int kMaxDepth = 64;

    [[noreturn]] void fail(std::string_view what) const {
        throw ProtocolError(std::format("invalid JSON at offset {}: {}", pos_, what));
    }

    char peek() const {
        if (pos_ >= text_.size()) fail("unexpected end of input");
        return text_[pos_];
    }

    void skip_whitespace() {
        while (pos_ < text_.size()) {
            char c = text_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++pos_;
            } else {
                break;
            }
        }
    }

    void expect(char c) {
        if (peek() != c) fail(std::format("expected '{}'", c));
        ++pos_;
    }

    bool consume_literal(std::string_view literal) {
        if (text_.compare(pos_, literal.size(), literal) != 0) return false;
        pos_ += literal.size();
        return true;
    }

    void append_utf8(std::string& out, uint32_t code_point) {
        if (code_point < 0x80) {
            out.push_back(static_cast<char>(code_point));
        } else if (code_point < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (code_point >> 6)));
            out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
        } else if (code_point < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (code_point >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (code_point >> 18)));
            out.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
        }
    }

    uint32_t parse_hex4() {
        if (pos_ + 4 > text_.size()) fail("truncated \\u escape");
        uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            char c = text_[pos_++];
            value <<= 4;
            if (c >= '0' && c <= '9') {
                value |= static_cast<uint32_t>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                value |= static_cast<uint32_t>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                value |= static_cast<uint32_t>(c - 'A' + 10);
            } else {
                fail("bad hex digit in \\u escape");
            }
        }
        return value;
    }

    std::string parse_string() {
        expect('"');
        std::string out;
        while (true) {
            char c = peek();
            ++pos_;
            if (c == '"') return out;
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            char esc = peek();
            ++pos_;
            switch (esc) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    uint32_t cp = parse_hex4();
                    // Surrogate pair (emoji in MOTDs).
                    if (cp >= 0xD800 && cp <= 0xDBFF && pos_ + 1 < text_.size() &&
                        text_[pos_] == '\\' && text_[pos_ + 1] == 'u') {
                        size_t save = pos_;
                        pos_ += 2;
                        uint32_t low = parse_hex4();
                        if (low >= 0xDC00 && low <= 0xDFFF) {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        } else {
                            pos_ = save;
                        }
                    }
                    append_utf8(out, cp);
                    break;
                }
                default: fail("unknown escape");
            }
        }
    }

    double parse_number() {
        size_t start = pos_;
        if (pos_ < text_.size() && (text_[pos_] == '-' || text_[pos_] == '+')) ++pos_;
        while (pos_ < text_.size()) {
            char c = text_[pos_];
            bool numeric = (c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' ||
                           c == '+' || c == '-';
            if (!numeric) break;
            ++pos_;
        }
        if (start == pos_) fail("expected a number");
        try {
            return std::stod(std::string(text_.substr(start, pos_ - start)));
        } catch (const std::exception&) {
            fail("unparseable number");
        }
    }

    Value parse_value(int depth) {
        if (depth > kMaxDepth) fail("nesting too deep");
        skip_whitespace();
        switch (peek()) {
            case '{': {
                ++pos_;
                Object object;
                skip_whitespace();
                if (peek() == '}') {
                    ++pos_;
                    return Value(std::move(object));
                }
                while (true) {
                    skip_whitespace();
                    std::string key = parse_string();
                    skip_whitespace();
                    expect(':');
                    object.emplace(std::move(key), parse_value(depth + 1));
                    skip_whitespace();
                    char c = peek();
                    ++pos_;
                    if (c == '}') return Value(std::move(object));
                    if (c != ',') fail("expected ',' or '}'");
                }
            }
            case '[': {
                ++pos_;
                Array array;
                skip_whitespace();
                if (peek() == ']') {
                    ++pos_;
                    return Value(std::move(array));
                }
                while (true) {
                    array.push_back(parse_value(depth + 1));
                    skip_whitespace();
                    char c = peek();
                    ++pos_;
                    if (c == ']') return Value(std::move(array));
                    if (c != ',') fail("expected ',' or ']'");
                }
            }
            case '"': return Value(parse_string());
            case 't':
                if (consume_literal("true")) return Value(true);
                fail("expected 'true'");
            case 'f':
                if (consume_literal("false")) return Value(false);
                fail("expected 'false'");
            case 'n':
                if (consume_literal("null")) return Value();
                fail("expected 'null'");
            default: return Value(parse_number());
        }
    }

    std::string_view text_;
    size_t pos_ = 0;
};

} // namespace

Value parse(std::string_view text) { return Parser(text).parse_document(); }

std::string flatten_component(const Value& value) {
    if (const auto* text = value.as_string()) return *text;

    if (const auto* array = value.as_array()) {
        std::string out;
        for (const auto& item : *array) out += flatten_component(item);
        return out;
    }

    std::string out;
    if (const Value* text = value.find("text")) out += text->string_or("");
    if (const Value* translate = value.find("translate"); out.empty() && translate != nullptr) {
        out += translate->string_or("");
    }
    if (const Value* extra = value.find("extra")) out += flatten_component(*extra);
    return out;
}

std::string strip_formatting(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        // The section sign is U+00A7, two bytes in UTF-8.
        if (static_cast<uint8_t>(text[i]) == 0xC2 && i + 2 < text.size() &&
            static_cast<uint8_t>(text[i + 1]) == 0xA7) {
            i += 3;
            continue;
        }
        out.push_back(text[i]);
        ++i;
    }
    return out;
}

} // namespace mcpp::json
