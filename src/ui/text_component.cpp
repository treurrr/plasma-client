#include "mcpp/ui/text_component.hpp"

#include "mcpp/gpu/ui.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace mcpp::ui {
namespace {

constexpr uint32_t kLegacy[16] = {0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA,
                                  0xFFAA00, 0xAAAAAA, 0x555555, 0x5555FF, 0x55FF55, 0x55FFFF,
                                  0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF};
constexpr std::string_view kNames[16] = {
    "black", "dark_blue", "dark_green", "dark_aqua", "dark_red", "dark_purple",
    "gold",  "gray",      "dark_gray",  "blue",      "green",    "aqua",
    "red",   "light_purple", "yellow",  "white"};

// One reader over both forms, so the two cannot drift apart: JSON and NBT
// carry the same fields, NBT with bytes where JSON has booleans.
struct JsonNode {
    const json::Value* v;
    const std::string* string() const { return v->as_string(); }
    std::optional<double> number() const { return v->as_number(); }
    std::optional<bool> boolean() const {
        if (auto b = v->as_bool()) return b;
        if (auto n = v->as_number()) return *n != 0.0;
        if (const auto* s = v->as_string()) return *s == "true";
        return std::nullopt;
    }
    bool is_list() const { return v->as_array() != nullptr; }
    bool is_compound() const { return v->as_object() != nullptr; }
    template <typename F> void each(F&& f) const {
        if (const auto* a = v->as_array()) {
            for (const auto& item : *a) f(JsonNode{&item});
        }
    }
    std::optional<JsonNode> find(std::string_view key) const {
        if (const auto* found = v->find(key)) return JsonNode{found};
        return std::nullopt;
    }
};

struct NbtNode {
    const nbt::Value* v;
    const std::string* string() const { return v->as_string(); }
    std::optional<double> number() const {
        if (auto i = v->as_int()) return static_cast<double>(*i);
        return v->as_double();
    }
    std::optional<bool> boolean() const {
        if (auto i = v->as_int()) return *i != 0;
        if (const auto* s = v->as_string()) return *s == "true";
        return std::nullopt;
    }
    bool is_list() const { return v->as_list() != nullptr; }
    bool is_compound() const { return v->as_compound() != nullptr; }
    template <typename F> void each(F&& f) const {
        if (const auto* l = v->as_list()) {
            for (const auto& item : *l) f(NbtNode{&item});
        }
    }
    std::optional<NbtNode> find(std::string_view key) const {
        if (const auto* found = v->find(key)) return NbtNode{found};
        return std::nullopt;
    }
};

std::string number_text(double value) {
    if (value == std::floor(value) && std::abs(value) < 1e15) {
        return std::to_string(static_cast<int64_t>(value));
    }
    return std::format("{}", value);
}

template <typename Node> Component parse(const Node& node, int depth);

template <typename Node> void parse_style(const Node& node, Style& style, int depth) {
    if (auto c = node.find("color")) {
        if (const auto* s = c->string()) style.colour = parse_colour(*s);
    }
    const auto flag = [&](std::string_view key, std::optional<bool>& out) {
        if (auto f = node.find(key)) out = f->boolean();
    };
    flag("bold", style.bold);
    flag("italic", style.italic);
    flag("underlined", style.underlined);
    flag("strikethrough", style.strikethrough);
    flag("obfuscated", style.obfuscated);
    if (auto s = node.find("shadow_color")) {
        if (auto n = s->number()) {
            style.shadow_colour = static_cast<uint32_t>(static_cast<int64_t>(*n));
        } else if (s->is_list()) {
            float parts[4] = {0, 0, 0, 0};
            int i = 0;
            s->each([&](const Node& part) {
                if (i < 4) parts[i++] = static_cast<float>(part.number().value_or(0.0));
            });
            const auto byte = [](float f) {
                return static_cast<uint32_t>(std::clamp(f, 0.0f, 1.0f) * 255.0f + 0.5f);
            };
            style.shadow_colour = (byte(parts[3]) << 24) | (byte(parts[0]) << 16) |
                                  (byte(parts[1]) << 8) | byte(parts[2]);
        }
    }
    if (auto f = node.find("font")) {
        if (const auto* s = f->string()) style.font = *s;
    }
    if (auto f = node.find("insertion")) {
        if (const auto* s = f->string()) style.insertion = *s;
    }
    // 1.21.5 renamed the events and gave each action its own field; servers
    // behind a proxy still send the old shape, so both are read.
    std::optional<Node> click = node.find("click_event");
    if (!click) click = node.find("clickEvent");
    if (click && click->is_compound()) {
        if (auto a = click->find("action"); a && a->string()) style.click_action = *a->string();
        for (const char* key : {"url", "command", "path", "page", "value"}) {
            if (auto v = click->find(key)) {
                if (const auto* s = v->string()) {
                    style.click_value = *s;
                } else if (auto n = v->number()) {
                    style.click_value = number_text(*n);
                }
                break;
            }
        }
    }
    std::optional<Node> hover = node.find("hover_event");
    if (!hover) hover = node.find("hoverEvent");
    if (hover && hover->is_compound()) {
        if (auto a = hover->find("action"); a && a->string()) style.hover_action = *a->string();
        if (style.hover_action == "show_text") {
            std::optional<Node> body = hover->find("value");
            if (!body) body = hover->find("contents");
            if (body && depth < 16) {
                style.hover_text = std::make_shared<const Component>(parse(*body, depth + 1));
            }
        }
    }
}

template <typename Node> Component parse(const Node& node, int depth) {
    Component out;
    if (depth > 64) return out;
    if (const auto* s = node.string()) {
        out.text = *s;
        return out;
    }
    if (auto b = node.boolean(); b && !node.is_compound() && !node.is_list() && !node.number()) {
        out.text = *b ? "true" : "false";
        return out;
    }
    if (auto n = node.number(); n && !node.is_compound() && !node.is_list()) {
        out.text = number_text(*n);
        return out;
    }
    if (node.is_list()) {
        bool first = true;
        node.each([&](const Node& item) {
            if (first) {
                out = parse(item, depth + 1);
                first = false;
            } else {
                out.extra.push_back(parse(item, depth + 1));
            }
        });
        return out;
    }
    if (!node.is_compound()) return out;

    if (auto t = node.find("text")) {
        out.kind = Component::Kind::Text;
        if (const auto* s = t->string()) {
            out.text = *s;
        } else if (auto n = t->number()) {
            out.text = number_text(*n);
        }
    } else if (auto key = node.find("translate")) {
        out.kind = Component::Kind::Translate;
        out.text = key->string() ? *key->string() : std::string{};
        if (auto f = node.find("fallback"); f && f->string()) out.fallback = *f->string();
        if (auto with = node.find("with")) {
            with->each([&](const Node& item) { out.args.push_back(parse(item, depth + 1)); });
        }
    } else if (auto bind = node.find("keybind")) {
        out.kind = Component::Kind::Keybind;
        out.text = bind->string() ? *bind->string() : std::string{};
    } else if (auto score = node.find("score")) {
        out.kind = Component::Kind::Score;
        // The server resolves scores before it sends them; what is left is
        // the value it filled in, when it filled one in.
        if (auto v = score->find("value"); v && v->string()) out.text = *v->string();
    } else if (auto selector = node.find("selector")) {
        out.kind = Component::Kind::Selector;
        out.text = selector->string() ? *selector->string() : std::string{};
    } else if (auto path = node.find("nbt")) {
        out.kind = Component::Kind::Nbt;
        out.text = path->string() ? *path->string() : std::string{};
    } else if (auto wrapped = node.find("")) {
        // The codec's way of writing a bare value inside a compound.
        return parse(*wrapped, depth + 1);
    }
    parse_style(node, out.style, depth);
    if (auto extra = node.find("extra")) {
        extra->each([&](const Node& item) { out.extra.push_back(parse(item, depth + 1)); });
    }
    return out;
}

void push(std::vector<Run>& runs, std::string text, const Style& style) {
    if (text.empty()) return;
    if (!runs.empty() && runs.back().style == style) {
        runs.back().text += text;
        return;
    }
    runs.push_back({std::move(text), style});
}

// § codes inside a literal, the way the game applies them when it draws: a
// colour code also clears bold and the rest, and §r goes back to the style the
// literal itself had.
void legacy_into(std::vector<Run>& runs, std::string_view text, const Style& base) {
    Style style = base;
    std::string piece;
    for (size_t i = 0; i < text.size();) {
        if (text.compare(i, 2, "\xC2\xA7") == 0 && i + 2 < text.size()) {
            push(runs, std::move(piece), style);
            piece.clear();
            const char code = static_cast<char>(std::tolower(static_cast<unsigned char>(text[i + 2])));
            const auto digit = std::string_view("0123456789abcdef").find(code);
            if (digit != std::string_view::npos) {
                style = base;
                style.bold = style.italic = style.underlined = style.strikethrough =
                    style.obfuscated = false;
                style.colour = kLegacy[digit];
            } else if (code == 'k') {
                style.obfuscated = true;
            } else if (code == 'l') {
                style.bold = true;
            } else if (code == 'm') {
                style.strikethrough = true;
            } else if (code == 'n') {
                style.underlined = true;
            } else if (code == 'o') {
                style.italic = true;
            } else if (code == 'r') {
                style = base;
            }
            i += 3;
            continue;
        }
        piece += text[i];
        ++i;
    }
    push(runs, std::move(piece), style);
}

void flatten_into(std::vector<Run>& runs, const Component& component, const Style& parent,
                  const Resolve& resolve, int depth);

// %s, %2$s and %%, with each argument drawn in its own style under the
// translation's.
void translate_into(std::vector<Run>& runs, std::string_view pattern, const Component& component,
                    const Style& style, const Resolve& resolve, int depth) {
    std::string literal_part;
    size_t next = 0;
    for (size_t i = 0; i < pattern.size(); ++i) {
        if (pattern[i] != '%' || i + 1 >= pattern.size()) {
            literal_part += pattern[i];
            continue;
        }
        if (pattern[i + 1] == '%') {
            literal_part += '%';
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
        if (cursor >= pattern.size() || (pattern[cursor] != 's' && pattern[cursor] != 'd')) {
            literal_part += pattern[i];
            continue;
        }
        legacy_into(runs, literal_part, style);
        literal_part.clear();
        const size_t slot = positional ? index - 1 : next++;
        if (slot < component.args.size()) {
            flatten_into(runs, component.args[slot], style, resolve, depth + 1);
        }
        i = cursor;
    }
    legacy_into(runs, literal_part, style);
}

void flatten_into(std::vector<Run>& runs, const Component& component, const Style& parent,
                  const Resolve& resolve, int depth) {
    if (depth > 64) return;
    const Style style = component.style.under(parent);
    switch (component.kind) {
    case Component::Kind::Text:
    case Component::Kind::Score:
    case Component::Kind::Selector:
    case Component::Kind::Nbt:
        legacy_into(runs, component.text, style);
        break;
    case Component::Kind::Keybind:
        if (resolve.key_name) {
            legacy_into(runs, resolve.key_name(component.text), style);
        } else {
            legacy_into(runs, component.text, style);
        }
        break;
    case Component::Kind::Translate: {
        const std::string* pattern = resolve.translate ? resolve.translate(component.text)
                                                       : nullptr;
        if (pattern != nullptr) {
            translate_into(runs, *pattern, component, style, resolve, depth);
        } else if (component.fallback) {
            translate_into(runs, *component.fallback, component, style, resolve, depth);
        } else {
            // An unknown key is shown as the key, and its arguments are not.
            legacy_into(runs, component.text, style);
        }
        break;
    }
    }
    for (const auto& child : component.extra) flatten_into(runs, child, style, resolve, depth + 1);
}

float glyph_advance(const Advance& advance, uint32_t code, const Style& style) {
    return advance(code) + (style.bold.value_or(false) ? 1.0f : 0.0f);
}

} // namespace

Style Style::under(const Style& parent) const {
    Style out = *this;
    if (!out.colour) out.colour = parent.colour;
    if (!out.bold) out.bold = parent.bold;
    if (!out.italic) out.italic = parent.italic;
    if (!out.underlined) out.underlined = parent.underlined;
    if (!out.strikethrough) out.strikethrough = parent.strikethrough;
    if (!out.obfuscated) out.obfuscated = parent.obfuscated;
    if (!out.shadow_colour) out.shadow_colour = parent.shadow_colour;
    if (out.font.empty()) out.font = parent.font;
    if (out.insertion.empty()) out.insertion = parent.insertion;
    if (out.click_action.empty()) {
        out.click_action = parent.click_action;
        out.click_value = parent.click_value;
    }
    if (out.hover_action.empty()) {
        out.hover_action = parent.hover_action;
        out.hover_text = parent.hover_text;
    }
    return out;
}

bool Style::operator==(const Style& other) const {
    return colour == other.colour && bold.value_or(false) == other.bold.value_or(false) &&
           italic.value_or(false) == other.italic.value_or(false) &&
           underlined.value_or(false) == other.underlined.value_or(false) &&
           strikethrough.value_or(false) == other.strikethrough.value_or(false) &&
           obfuscated.value_or(false) == other.obfuscated.value_or(false) &&
           shadow_colour == other.shadow_colour && font == other.font &&
           insertion == other.insertion && click_action == other.click_action &&
           click_value == other.click_value && hover_action == other.hover_action &&
           hover_text == other.hover_text;
}

Component literal(std::string text, Style style) {
    Component out;
    out.text = std::move(text);
    out.style = std::move(style);
    return out;
}

Component translatable(std::string key, std::vector<Component> args, Style style) {
    Component out;
    out.kind = Component::Kind::Translate;
    out.text = std::move(key);
    out.args = std::move(args);
    out.style = std::move(style);
    return out;
}

Component from_json(const json::Value& value) { return parse(JsonNode{&value}, 0); }

Component from_json_text(std::string_view text) {
    try {
        return from_json(json::parse(text));
    } catch (const std::exception&) {
        return literal(std::string(text));
    }
}

Component from_nbt(const nbt::Value& value) { return parse(NbtNode{&value}, 0); }

std::optional<uint32_t> parse_colour(std::string_view name) {
    if (name.size() == 7 && name[0] == '#') {
        uint32_t rgb = 0;
        for (size_t i = 1; i < 7; ++i) {
            const char c = name[i];
            uint32_t digit = 0;
            if (c >= '0' && c <= '9') {
                digit = static_cast<uint32_t>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                digit = static_cast<uint32_t>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                digit = static_cast<uint32_t>(c - 'A' + 10);
            } else {
                return std::nullopt;
            }
            rgb = rgb * 16 + digit;
        }
        return rgb;
    }
    for (size_t i = 0; i < 16; ++i) {
        if (kNames[i] == name) return kLegacy[i];
    }
    return std::nullopt;
}

uint32_t legacy_colour(int index) { return kLegacy[std::clamp(index, 0, 15)]; }

std::vector<Run> flatten(const Component& component, const Resolve& resolve) {
    std::vector<Run> runs;
    flatten_into(runs, component, Style{}, resolve, 0);
    return runs;
}

std::vector<Run> flatten_legacy(std::string_view text, const Style& base) {
    std::vector<Run> runs;
    legacy_into(runs, text, base);
    return runs;
}

std::string plain(const std::vector<Run>& runs) {
    std::string out;
    for (const auto& run : runs) out += run.text;
    return out;
}

std::string plain(const Component& component, const Resolve& resolve) {
    return plain(flatten(component, resolve));
}

uint32_t next_codepoint(std::string_view text, size_t& at) {
    const auto lead = static_cast<unsigned char>(text[at]);
    size_t length = 1;
    uint32_t code = lead;
    if ((lead & 0xE0u) == 0xC0u) {
        length = 2;
        code = lead & 0x1Fu;
    } else if ((lead & 0xF0u) == 0xE0u) {
        length = 3;
        code = lead & 0x0Fu;
    } else if ((lead & 0xF8u) == 0xF0u) {
        length = 4;
        code = lead & 0x07u;
    }
    if (at + length > text.size()) length = 1;
    for (size_t i = 1; i < length; ++i) {
        const auto byte = static_cast<unsigned char>(text[at + i]);
        if ((byte & 0xC0u) != 0x80u) {
            length = 1;
            code = lead;
            break;
        }
        code = (code << 6) | (byte & 0x3Fu);
    }
    at += length;
    return code;
}

float width(const std::vector<Run>& runs, const Advance& advance) {
    float total = 0.0f;
    for (const auto& run : runs) {
        for (size_t at = 0; at < run.text.size();) {
            const uint32_t code = next_codepoint(run.text, at);
            if (code == '\n') continue;
            total += glyph_advance(advance, code, run.style);
        }
    }
    return total;
}

std::vector<std::vector<Run>> wrap(const std::vector<Run>& runs, float max_width,
                                   const Advance& advance) {
    // Lay the runs out as one stream of (codepoint, style) so a break can fall
    // anywhere, then cut the stream into lines.
    struct Cell {
        uint32_t code;
        size_t run;
        std::string bytes;
    };
    std::vector<Cell> cells;
    for (size_t r = 0; r < runs.size(); ++r) {
        for (size_t at = 0; at < runs[r].text.size();) {
            const size_t start = at;
            const uint32_t code = next_codepoint(runs[r].text, at);
            cells.push_back({code, r, runs[r].text.substr(start, at - start)});
        }
    }
    std::vector<std::vector<Run>> lines;
    const auto emit = [&](size_t from, size_t to) {
        std::vector<Run> line;
        for (size_t i = from; i < to; ++i) push(line, cells[i].bytes, runs[cells[i].run].style);
        lines.push_back(std::move(line));
    };
    size_t start = 0;
    while (start <= cells.size()) {
        float used = 0.0f;
        size_t last_space = std::string::npos;
        size_t i = start;
        bool broke = false;
        for (; i < cells.size(); ++i) {
            if (cells[i].code == '\n') {
                emit(start, i);
                start = i + 1;
                broke = true;
                break;
            }
            const float w = glyph_advance(advance, cells[i].code, runs[cells[i].run].style);
            if (cells[i].code == ' ') last_space = i;
            if (used + w > max_width && i > start) {
                if (last_space != std::string::npos && last_space > start) {
                    emit(start, last_space);
                    start = last_space + 1;
                } else {
                    emit(start, i);
                    start = i;
                }
                broke = true;
                break;
            }
            used += w;
        }
        if (!broke) {
            emit(start, cells.size());
            break;
        }
    }
    return lines;
}

float draw(gpu::UiRenderer& quads, const std::vector<Run>& runs, float x, float y, float scale,
           uint32_t rgba, bool shadow, uint32_t seed) {
    const uint32_t alpha = rgba & 0xFFu;
    const auto colour_of = [&](const Style& style) {
        const uint32_t rgb = style.colour ? *style.colour : (rgba >> 8);
        return (rgb << 8) | alpha;
    };
    // The game's shadow is the colour at a quarter brightness, or the colour
    // the style asks for.
    const auto shadow_of = [&](const Style& style, uint32_t colour) -> uint32_t {
        if (style.shadow_colour) {
            const uint32_t argb = *style.shadow_colour;
            const uint32_t a = ((argb >> 24) & 0xFFu) * alpha / 255u;
            return ((argb & 0xFFFFFFu) << 8) | a;
        }
        return ((((colour >> 24) & 0xFFu) / 4u) << 24) | ((((colour >> 16) & 0xFFu) / 4u) << 16) |
               ((((colour >> 8) & 0xFFu) / 4u) << 8) | (colour & 0xFFu);
    };
    uint32_t random = seed * 2654435761u + 1u;

    const auto pass = [&](bool is_shadow) {
        float pen = x;
        for (const auto& run : runs) {
            const uint32_t colour = colour_of(run.style);
            const uint32_t ink = is_shadow ? shadow_of(run.style, colour) : colour;
            if (is_shadow && (ink & 0xFFu) == 0) {
                for (size_t at = 0; at < run.text.size();) {
                    pen += (quads.advance(next_codepoint(run.text, at)) +
                            (run.style.bold.value_or(false) ? 1.0f : 0.0f)) *
                           scale;
                }
                continue;
            }
            const float offset = is_shadow ? scale : 0.0f;
            const bool bold = run.style.bold.value_or(false);
            const bool italic = run.style.italic.value_or(false);
            for (size_t at = 0; at < run.text.size();) {
                uint32_t code = next_codepoint(run.text, at);
                const float step = quads.advance(code) + (bold ? 1.0f : 0.0f);
                if (run.style.obfuscated.value_or(false) && code != ' ') {
                    random = random * 1664525u + 1013904223u;
                    code = quads.glyph_like(code, random);
                }
                const float gx = pen + offset;
                const float gy = y + offset;
                if (italic) quads.set_transform(1.0f, -0.25f, 0.0f, 1.0f, 0.0f, gy + 4.0f * scale);
                quads.glyph(code, gx, gy, scale, ink);
                if (bold) quads.glyph(code, gx + scale, gy, scale, ink);
                if (italic) quads.reset_transform();
                if (run.style.strikethrough.value_or(false)) {
                    quads.fill(gx - scale, gy + 3.5f * scale, step * scale + scale, scale, ink);
                }
                if (run.style.underlined.value_or(false)) {
                    quads.fill(gx - scale, gy + 8.0f * scale, step * scale + scale, scale, ink);
                }
                pen += step * scale;
            }
        }
        return pen - x;
    };
    if (shadow) pass(true);
    return pass(false);
}

} // namespace mcpp::ui
