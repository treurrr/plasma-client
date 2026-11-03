#pragma once
#include "mcpp/json.hpp"
#include "mcpp/nbt.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mcpp::gpu {
class UiRenderer;
}

// The game's text components: what every chat line, title, tooltip, MOTD and
// disconnect reason is made of. A component is a piece of text, a translation
// with arguments, or a key binding's name, with a style and children that
// inherit it. They arrive as JSON (status pings, older packets) or NBT (play
// packets since 1.20.3) and are drawn the same way.
namespace mcpp::ui {

struct Component;

struct Style {
    std::optional<uint32_t> colour;   // 0xRRGGBB
    std::optional<bool> bold;
    std::optional<bool> italic;
    std::optional<bool> underlined;
    std::optional<bool> strikethrough;
    std::optional<bool> obfuscated;
    std::optional<uint32_t> shadow_colour;   // 0xAARRGGBB
    std::string font;
    std::string insertion;
    std::string click_action;
    std::string click_value;
    std::string hover_action;
    std::shared_ptr<const Component> hover_text;

    // Every field this style leaves unset, taken from the parent.
    Style under(const Style& parent) const;
    bool operator==(const Style& other) const;
};

struct Component {
    enum class Kind : uint8_t { Text, Translate, Keybind, Score, Selector, Nbt };
    Kind kind = Kind::Text;
    // The literal text, the translation key, the binding's name, or what a
    // score, selector or nbt component would have looked up.
    std::string text;
    std::optional<std::string> fallback;
    std::vector<Component> args;
    Style style;
    std::vector<Component> extra;
};

Component literal(std::string text, Style style = {});
Component translatable(std::string key, std::vector<Component> args = {}, Style style = {});

Component from_json(const json::Value& value);
Component from_json_text(std::string_view text);
Component from_nbt(const nbt::Value& value);

// "red", "#ff5555" or nothing.
std::optional<uint32_t> parse_colour(std::string_view name);
// The sixteen colours the § codes and the colour names share, 0 to 15.
uint32_t legacy_colour(int index);

// A run of text with one style, every inherited field resolved and every §
// code applied.
struct Run {
    std::string text;
    Style style;
};

struct Resolve {
    std::function<const std::string*(std::string_view)> translate;
    // What a keybind component shows: "key.jump" -> "Space".
    std::function<std::string(std::string_view)> key_name;
};

std::vector<Run> flatten(const Component& component, const Resolve& resolve = {});
std::vector<Run> flatten_legacy(std::string_view text, const Style& base = {});
std::string plain(const std::vector<Run>& runs);
std::string plain(const Component& component, const Resolve& resolve = {});

// Width in font pixels with the game's own measure: each glyph's advance,
// one more for bold.
using Advance = std::function<float(uint32_t)>;
float width(const std::vector<Run>& runs, const Advance& advance);
// Broken where the game breaks: at the last space that fits, or inside a word
// that is wider than the line on its own, and always at a newline.
std::vector<std::vector<Run>> wrap(const std::vector<Run>& runs, float max_width,
                                   const Advance& advance);

uint32_t next_codepoint(std::string_view text, size_t& at);

// Draws the runs at (x, y) in screen pixels, `scale` screen pixels per font
// pixel. `rgba` is the colour of text whose style names none; its alpha is
// the alpha of everything. Returns the width drawn, in screen pixels.
float draw(gpu::UiRenderer& quads, const std::vector<Run>& runs, float x, float y, float scale,
           uint32_t rgba, bool shadow, uint32_t seed = 0);

} // namespace mcpp::ui
