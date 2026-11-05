#include "mcpp/ui/widgets.hpp"

#include <algorithm>
#include <cmath>

namespace mcpp::ui {
namespace {

constexpr std::string_view kClick = "minecraft:ui.button.click";

// `ARGB.color`, `ARGB.as8BitChannel`: the alpha floored to eight bits.
uint32_t with_alpha(uint32_t argb, float alpha) {
    const auto a = static_cast<uint32_t>(std::floor(std::clamp(alpha, 0.0f, 1.0f) * 255.0f));
    return (argb & 0x00FFFFFFu) | (a << 24);
}

bool activates(const KeyEvent& event) {
    return event.key == key::Enter || event.key == key::Space || event.key == key::KeypadEnter;
}

size_t utf8_back(const std::string& text, size_t at) {
    if (at == 0) return 0;
    --at;
    while (at > 0 && (static_cast<unsigned char>(text[at]) & 0xC0u) == 0x80u) --at;
    return at;
}

size_t utf8_forward(const std::string& text, size_t at) {
    if (at >= text.size()) return text.size();
    ++at;
    while (at < text.size() && (static_cast<unsigned char>(text[at]) & 0xC0u) == 0x80u) ++at;
    return at;
}

std::string encode(uint32_t code) {
    std::string out;
    if (code < 0x80) {
        out += static_cast<char>(code);
    } else if (code < 0x800) {
        out += static_cast<char>(0xC0 | (code >> 6));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else if (code < 0x10000) {
        out += static_cast<char>(0xE0 | (code >> 12));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (code >> 18));
        out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    }
    return out;
}

} // namespace

Button::Button(int x_, int y_, int w_, int h_, Component label, std::function<void(Button&)> press)
    : on_press(std::move(press)) {
    x = x_;
    y = y_;
    w = w_;
    h = h_;
    message = std::move(label);
}

// `AbstractButton.renderDefaultLabel`, `AbstractWidget.renderScrollingStringOverContents`: the label, scrolled inside a 2-pixel margin.
void Button::render_label(GuiGraphics& gfx) {
    const uint32_t colour = with_alpha(active ? 0xFFFFFFu : 0xA0A0A0u, alpha);
    gfx.draw_scrolling_string(gfx.runs(message), x + 2, y, x + w - 2, y + h, colour);
}

// `Button.Plain.renderContents`, `AbstractButton.renderDefaultSprite`: the button sprite for its state, then the label.
void Button::render(GuiGraphics& gfx, double, double) {
    const char* sprite = !active ? "widget/button_disabled"
                         : highlighted() ? "widget/button_highlighted"
                                         : "widget/button";
    gfx.blit_sprite(sprite, x, y, w, h, with_alpha(0xFFFFFFu, alpha));
    render_label(gfx);
}

// `AbstractWidget.mouseClicked`: a left click inside an active widget clicks it with a sound.
bool Button::mouse_clicked(double mx, double my, int button) {
    if (!active || !visible || button != 0 || !over(mx, my)) return false;
    play_sound(kClick);
    press();
    return true;
}

// `AbstractButton.keyPressed`: enter, space or keypad enter press the button with a sound.
bool Button::key_pressed(const KeyEvent& event) {
    if (!active || !visible || !activates(event)) return false;
    play_sound(kClick);
    press();
    return true;
}

void Button::press() {
    if (on_press) on_press(*this);
}

std::string Button::narration(const GuiGraphics& gfx) const {
    // narration.button: "Button: %s"
    return plain(translatable("narration.button", {message}), gfx.resolver());
}

SpriteButton::SpriteButton(int x_, int y_, int w_, int h_, std::string sprite_,
                           std::string highlighted_, Component narration_label,
                           std::function<void(Button&)> press)
    : Button(x_, y_, w_, h_, std::move(narration_label), std::move(press)),
      sprite(std::move(sprite_)), sprite_highlighted(std::move(highlighted_)) {}

void SpriteButton::render(GuiGraphics& gfx, double, double) {
    const std::string& name = !active && !sprite_disabled.empty() ? sprite_disabled
                              : highlighted() && !sprite_highlighted.empty() ? sprite_highlighted
                                                                              : sprite;
    gfx.blit_sprite(name, x, y, w, h, with_alpha(0xFFFFFFu, alpha));
}

SpriteIconButton::SpriteIconButton(int w_, int h_, Component label, std::string sprite_,
                                   int sprite_w_, int sprite_h_, bool icon_only_,
                                   std::function<void(Button&)> press)
    : Button(0, 0, w_, h_, std::move(label), std::move(press)), sprite(std::move(sprite_)),
      sprite_w(sprite_w_), sprite_h(sprite_h_), icon_only(icon_only_) {}

// `SpriteIconButton.renderSprite`: the icon at its own size, faded with the button.
void SpriteIconButton::render_sprite(GuiGraphics& gfx, int sx, int sy) {
    gfx.blit_sprite(sprite, sx, sy, sprite_w, sprite_h, with_alpha(0xFFFFFFu, alpha));
}

// `SpriteIconButton.CenteredIcon.renderContents`, `SpriteIconButton.TextAndIcon.renderContents`: the button, then the icon centred or the label with the icon at the right.
void SpriteIconButton::render(GuiGraphics& gfx, double, double) {
    const char* frame = !active ? "widget/button_disabled"
                        : highlighted() ? "widget/button_highlighted"
                                        : "widget/button";
    gfx.blit_sprite(frame, x, y, w, h, with_alpha(0xFFFFFFu, alpha));
    if (icon_only) {
        render_sprite(gfx, x + w / 2 - sprite_w / 2, y + h / 2 - sprite_h / 2);
        return;
    }
    const uint32_t colour = with_alpha(active ? 0xFFFFFFu : 0xA0A0A0u, alpha);
    gfx.draw_scrolling_string(gfx.runs(message), x + w / 2, x + 2, y, x + w - sprite_w - 4, y + h,
                              colour);
    render_sprite(gfx, x + w - sprite_w - 2, y + h / 2 - sprite_h / 2);
}

// `CommonButtons.language`: the globe, "Language...".
std::unique_ptr<SpriteIconButton> language_button(int width, std::function<void(Button&)> press,
                                                  bool icon_only) {
    return std::make_unique<SpriteIconButton>(width, 20, translatable("options.language"),
                                              "icon/language", 15, 15, icon_only,
                                              std::move(press));
}

// `CommonButtons.accessibility`: the figure, "Accessibility Settings...".
std::unique_ptr<SpriteIconButton> accessibility_button(int width,
                                                       std::function<void(Button&)> press,
                                                       bool icon_only) {
    return std::make_unique<SpriteIconButton>(
        width, 20,
        translatable(icon_only ? "options.accessibility"
                               : "accessibility.onboarding.accessibility.button"),
        "icon/accessibility", 15, 15, icon_only, std::move(press));
}

PlainTextButton::PlainTextButton(int x_, int y_, int w_, int h_, Component label,
                                 std::function<void(Button&)> press)
    : Button(x_, y_, w_, h_, std::move(label), std::move(press)) {}

// `PlainTextButton.renderContents`: the text, underlined while hovered or focused.
void PlainTextButton::render(GuiGraphics& gfx, double, double) {
    Component shown = message;
    if (highlighted()) shown.style.underlined = true;
    const auto a = static_cast<uint32_t>(std::ceil(alpha * 255.0f));
    gfx.draw_string(shown, x, y, 0xFFFFFFu | (a << 24));
}

StringWidget::StringWidget(int x_, int y_, int w_, int h_, Component text) {
    x = x_;
    y = y_;
    w = w_;
    h = h_;
    message = std::move(text);
    active = false;
}

// `StringWidget.visitLines`, `StringWidget.clipText`: the line centred on the height, cut with an ellipsis past the maximum width.
void StringWidget::render(GuiGraphics& gfx, double, double) {
    const int limit = max_width > 0 ? max_width : w;
    const int ty = y + (h - Font::kLineHeight) / 2;
    const uint32_t colour = with_alpha(0xFFFFFFu, alpha);
    auto runs = gfx.runs(message);
    if (gfx.width_of(runs) > limit) {
        const std::string head =
            gfx.font.plain_substr_by_width(plain(runs), limit - gfx.width_of("..."));
        gfx.draw_string(head + "...", x, ty, colour);
        return;
    }
    gfx.draw_string(runs, x, ty, colour);
}

CycleButton::CycleButton(int x_, int y_, int w_, int h_, Component name_,
                         std::vector<Component> values_, size_t index_,
                         std::function<void(size_t)> changed)
    : Button(x_, y_, w_, h_, {}, {}), name(std::move(name_)), values(std::move(values_)),
      index(index_), on_change(std::move(changed)) {
    refresh();
}

void CycleButton::refresh() {
    if (values.empty()) return;
    index = std::min(index, values.size() - 1);
    message = value_only ? values[index] : translatable("options.generic_value", {name, values[index]});
}

bool CycleButton::mouse_clicked(double mx, double my, int button) {
    backwards_ = false;
    return Button::mouse_clicked(mx, my, button);
}

// `CycleButton.onPress`, `CycleButton.cycleValue`: steps to the next or previous value and reports it.
void CycleButton::press() {
    if (values.empty()) return;
    index = backwards_ ? (index + values.size() - 1) % values.size() : (index + 1) % values.size();
    refresh();
    if (on_change) on_change(index);
}

Slider::Slider(int x_, int y_, int w_, int h_, double value_,
               std::function<Component(double)> label_, std::function<void(double)> changed)
    : value(std::clamp(value_, 0.0, 1.0)), label(std::move(label_)), on_change(std::move(changed)) {
    x = x_;
    y = y_;
    w = w_;
    h = h_;
    if (label) message = label(value);
}

// `AbstractSliderButton.renderWidget`: track, handle at the value, then the label.
void Slider::render(GuiGraphics& gfx, double, double) {
    const uint32_t tint = with_alpha(0xFFFFFFu, alpha);
    gfx.blit_sprite(focused && keyboard_focus && !can_change_ ? "widget/slider_highlighted"
                                                       : "widget/slider",
             x, y, w, h, tint);
    gfx.blit_sprite(hovered || can_change_ ? "widget/slider_handle_highlighted" : "widget/slider_handle",
             x + static_cast<int>(value * static_cast<double>(w - 8)), y, 8, h, tint);
    gfx.draw_scrolling_string(gfx.runs(message), x + 2, y, x + w - 2, y + h,
                  with_alpha(active ? 0xFFFFFFu : 0xA0A0A0u, alpha));
}

// `AbstractSliderButton.setValue`: clamps the value and applies it when it changed.
void Slider::set_value(double v) {
    const double clamped = std::clamp(v, 0.0, 1.0);
    if (clamped == value) return;
    value = clamped;
    if (on_change) on_change(value);
    if (label) message = label(value);
}

// `AbstractSliderButton.setValueFromMouse`: the value under the mouse.
void Slider::set_from_mouse(double mx) {
    set_value((mx - (x + 4)) / static_cast<double>(w - 8));
}

bool Slider::mouse_clicked(double mx, double my, int button) {
    if (!active || button != 0 || !over(mx, my)) return false;
    set_from_mouse(mx);
    return true;
}

// `AbstractSliderButton.onDrag`: dragging follows the mouse.
bool Slider::mouse_dragged(double mx, double, int, double, double) {
    set_from_mouse(mx);
    return true;
}

// `AbstractSliderButton.onRelease`: the click sound plays on release.
bool Slider::mouse_released(double, double, int) {
    play_sound(kClick);
    return true;
}

// `AbstractSliderButton.keyPressed`: selection toggles editing, arrows step one pixel.
bool Slider::key_pressed(const KeyEvent& event) {
    if (activates(event)) {
        can_change_ = !can_change_;
        return true;
    }
    if (can_change_ && (event.key == key::Left || event.key == key::Right)) {
        const double step = 1.0 / static_cast<double>(w - 8);
        set_value(value + (event.key == key::Left ? -step : step));
        return true;
    }
    return false;
}

void Slider::set_focused(bool on) {
    focused = on;
    if (!on) can_change_ = false;
}

EditBox::EditBox(int x_, int y_, int w_, int h_, Component narration_label) {
    x = x_;
    y = y_;
    w = w_;
    h = h_;
    message = std::move(narration_label);
}

// `EditBox.setValue`: filtered, cut to the max length, caret to the end.
void EditBox::set_value(std::string text) {
    if (filter && !filter(text)) return;
    if (text.size() > max_length) text.resize(max_length);
    text_ = std::move(text);
    cursor_ = selection_ = static_cast<int>(text_.size());
    if (on_change) on_change(text_);
}

// `EditBox.insertText`: replaces the selection with filtered text that still fits.
void EditBox::insert(std::string_view piece) {
    const int from = std::min(cursor_, selection_);
    const int to = std::max(cursor_, selection_);
    std::string filtered;
    for (const char c : piece) {
        // The game refuses § and control characters in a typed line.
        if (static_cast<unsigned char>(c) < 32 || c == 127) continue;
        filtered += c;
    }
    if (filtered.find("\xC2\xA7") != std::string::npos) {
        std::string clean;
        for (size_t i = 0; i < filtered.size(); ++i) {
            if (filtered.compare(i, 2, "\xC2\xA7") == 0) {
                ++i;
                continue;
            }
            clean += filtered[i];
        }
        filtered = clean;
    }
    std::string next = text_.substr(0, static_cast<size_t>(from));
    const size_t room = max_length > text_.size() - static_cast<size_t>(to - from)
                            ? max_length - (text_.size() - static_cast<size_t>(to - from))
                            : 0;
    if (filtered.size() > room) {
        size_t cut = room;
        while (cut > 0 && (static_cast<unsigned char>(filtered[cut]) & 0xC0u) == 0x80u) --cut;
        filtered.resize(cut);
    }
    next += filtered;
    next += text_.substr(static_cast<size_t>(to));
    if (filter && !filter(next)) return;
    text_ = std::move(next);
    cursor_ = selection_ = from + static_cast<int>(filtered.size());
    if (on_change) on_change(text_);
}

void EditBox::erase(int count) {
    if (cursor_ != selection_) {
        insert("");
        return;
    }
    int from = cursor_;
    int to = cursor_;
    for (int i = 0; i < std::abs(count); ++i) {
        if (count < 0) {
            from = static_cast<int>(utf8_back(text_, static_cast<size_t>(from)));
        } else {
            to = static_cast<int>(utf8_forward(text_, static_cast<size_t>(to)));
        }
    }
    if (from == to) return;
    std::string next = text_.substr(0, static_cast<size_t>(from)) + text_.substr(static_cast<size_t>(to));
    if (filter && !filter(next)) return;
    text_ = std::move(next);
    cursor_ = selection_ = from;
    if (on_change) on_change(text_);
}

int EditBox::word_from(int from, int direction) const {
    int at = from;
    const int size = static_cast<int>(text_.size());
    if (direction < 0) {
        while (at > 0 && text_[static_cast<size_t>(at - 1)] == ' ') --at;
        while (at > 0 && text_[static_cast<size_t>(at - 1)] != ' ') --at;
    } else {
        while (at < size && text_[static_cast<size_t>(at)] != ' ') ++at;
        while (at < size && text_[static_cast<size_t>(at)] == ' ') ++at;
    }
    return at;
}

void EditBox::move_cursor(int to, bool select) {
    cursor_ = std::clamp(to, 0, static_cast<int>(text_.size()));
    if (!select) selection_ = cursor_;
}

bool EditBox::key_pressed(const KeyEvent& event) {
    if (!focused || !visible) return false;
    const bool select = event.shift();
    const bool word = event.control();
    if (event.control() && !event.shift()) {
        if (event.key == key::A) {
            cursor_ = static_cast<int>(text_.size());
            selection_ = 0;
            return true;
        }
        if (event.key == key::C || event.key == key::X) {
            const int from = std::min(cursor_, selection_);
            const int to = std::max(cursor_, selection_);
            if (platform != nullptr && platform->set_clipboard && to > from) {
                platform->set_clipboard(text_.substr(static_cast<size_t>(from),
                                                     static_cast<size_t>(to - from)));
            }
            if (event.key == key::X && editable) insert("");
            return true;
        }
        if (event.key == key::V) {
            if (editable && platform != nullptr && platform->get_clipboard) {
                insert(platform->get_clipboard());
            }
            return true;
        }
    }
    switch (event.key) {
    case key::Backspace:
        if (editable) {
            if (word && cursor_ == selection_) {
                erase(0);
                const int to = cursor_;
                const int from = word_from(cursor_, -1);
                selection_ = from;
                cursor_ = to;
                insert("");
            } else {
                erase(-1);
            }
        }
        return true;
    case key::Delete:
        if (editable) {
            if (word && cursor_ == selection_) {
                selection_ = word_from(cursor_, 1);
                insert("");
            } else {
                erase(1);
            }
        }
        return true;
    case key::Left:
        if (!select && cursor_ != selection_ && !word) {
            move_cursor(std::min(cursor_, selection_), false);
        } else {
            move_cursor(word ? word_from(cursor_, -1)
                             : static_cast<int>(utf8_back(text_, static_cast<size_t>(cursor_))),
                        select);
        }
        return true;
    case key::Right:
        if (!select && cursor_ != selection_ && !word) {
            move_cursor(std::max(cursor_, selection_), false);
        } else {
            move_cursor(word ? word_from(cursor_, 1)
                             : static_cast<int>(utf8_forward(text_, static_cast<size_t>(cursor_))),
                        select);
        }
        return true;
    case key::Home:
        move_cursor(0, select);
        return true;
    case key::End:
        move_cursor(static_cast<int>(text_.size()), select);
        return true;
    default:
        return false;
    }
}

// `EditBox.charTyped`: inserts an allowed typed character.
bool EditBox::char_typed(uint32_t code, int) {
    if (!focused || !visible || !editable) return false;
    if (code < 32 || code == 127 || code == 0xA7) return false;
    insert(encode(code));
    return true;
}

bool EditBox::mouse_clicked(double mx, double my, int button) {
    if (!visible || button != 0 || !over(mx, my)) return false;
    if (last_gfx_ != nullptr) {
        // Where in the visible text the click landed.
        const int text_x = bordered ? x + 4 : x;
        const int offset = static_cast<int>(mx) - text_x;
        const std::string shown = text_.substr(static_cast<size_t>(display_));
        int at = 0;
        size_t i = 0;
        while (i < shown.size()) {
            const size_t next = utf8_forward(shown, i);
            const int wide = last_gfx_->width_of(shown.substr(0, next));
            if (wide > offset) break;
            at = static_cast<int>(next);
            i = next;
        }
        move_cursor(display_ + at, false);
    }
    return true;
}

void EditBox::set_focused(bool on) {
    focused = on;
    frame_ = 0;
}

void EditBox::tick() { ++frame_; }

// `EditBox.renderWidget`: text split at the caret, hint, suggestion, selection and blinking caret.
void EditBox::render(GuiGraphics& gfx, double, double) {
    last_gfx_ = &gfx;
    if (focused && !was_focused_) focused_at_ = gfx.seconds;
    was_focused_ = focused;
    if (bordered) {
        gfx.blit_sprite(focused ? "widget/text_field_highlighted" : "widget/text_field", x, y, w, h);
    }
    const uint32_t colour = editable ? text_colour : text_colour_uneditable;
    const int text_x = bordered ? x + 4 : x;
    const int text_y = bordered ? y + (h - 8) / 2 : y;

    // Keep the caret inside the box, scrolling the text under it.
    display_ = std::min(display_, static_cast<int>(text_.size()));
    if (cursor_ < display_) display_ = cursor_;
    while (display_ < cursor_ &&
           gfx.width_of(std::string_view(text_).substr(static_cast<size_t>(display_),
                                                       static_cast<size_t>(cursor_ - display_))) >
               inner_width()) {
        display_ = static_cast<int>(utf8_forward(text_, static_cast<size_t>(display_)));
    }
    std::string shown = text_.substr(static_cast<size_t>(display_));
    while (!shown.empty() && gfx.width_of(shown) > inner_width()) shown.pop_back();
    while (!shown.empty() && (static_cast<unsigned char>(shown.back()) & 0xC0u) == 0x80u) {
        shown.pop_back();
    }
    const auto literal_runs = [](std::string_view s) {
        std::vector<Run> runs;
        if (!s.empty()) runs.push_back({std::string(s), {}});
        return runs;
    };

    const int caret = cursor_ - display_;
    const bool caret_shown = caret >= 0 && caret <= static_cast<int>(shown.size());
    const bool blink_on =
        focused && caret_shown && static_cast<int64_t>((gfx.seconds - focused_at_) / 0.3) % 2 == 0;
    int pen = text_x;
    const int highlight =
        std::clamp(selection_ - display_, 0, static_cast<int>(shown.size()));
    if (!shown.empty()) {
        const std::string head =
            caret_shown ? shown.substr(0, static_cast<size_t>(caret)) : shown;
        pen = gfx.draw_string(literal_runs(head), pen, text_y, colour, true) - 1;
        pen += 1;
    }
    const bool caret_bar =
        cursor_ < static_cast<int>(text_.size()) || text_.size() >= max_length;
    int caret_x = pen;
    if (!caret_shown) {
        caret_x = caret > 0 ? text_x + w : text_x;
    } else if (caret_bar) {
        --caret_x;
        --pen;
    }
    if (!shown.empty() && caret_shown && caret < static_cast<int>(shown.size())) {
        gfx.draw_string(literal_runs(shown.substr(static_cast<size_t>(caret))), pen, text_y,
                        colour, true);
    }
    if (!plain(hint).empty() && shown.empty() && !focused) {
        gfx.draw_string(hint, pen, text_y, colour, true);
    }
    if (!caret_bar && !suggestion.empty()) {
        gfx.draw_string(literal_runs(suggestion), caret_x - 1, text_y, 0xFF808080u, true);
    }
    if (highlight != caret) {
        const int other = text_x + gfx.width_of(literal_runs(shown.substr(0, static_cast<size_t>(highlight))));
        int x0 = std::min(caret_x, x + w);
        int x1 = std::min(other - 1, x + w);
        if (x0 > x1) std::swap(x0, x1);
        gfx.fill(x0, text_y - 1, x1, text_y + 1 + 9, 0x800000FFu);
    }
    if (blink_on) {
        if (caret_bar) {
            gfx.fill(caret_x, text_y - 1, caret_x + 1, text_y + 1 + 9, colour);
        } else {
            gfx.draw_string(literal_runs("_"), caret_x, text_y, colour, true);
        }
    }
}

Checkbox::Checkbox(int x_, int y_, Component label, bool selected_,
                   std::function<void(bool)> changed)
    : selected(selected_), on_change(std::move(changed)) {
    x = x_;
    y = y_;
    w = kBox;
    h = kBox;
    message = std::move(label);
}

// `Checkbox.renderContents`: the box sprite for its state, then the label beside it.
void Checkbox::render(GuiGraphics& gfx, double, double) {
    const bool lit = highlighted() || focused;
    const char* sprite = selected ? (lit ? "widget/checkbox_selected_highlighted"
                                         : "widget/checkbox_selected")
                                  : (lit ? "widget/checkbox_highlighted" : "widget/checkbox");
    gfx.blit_sprite(sprite, x, y, kBox, kBox, with_alpha(0xFFFFFFu, alpha));
    gfx.draw_string(message, x + kBox + 4, y + kBox / 2 - 9 / 2, with_alpha(0xE0E0E0u, alpha), true);
    w = kBox + 4 + gfx.width_of(message);
}

// `Checkbox.onPress`: flips the box and reports it.
void Checkbox::toggle() {
    selected = !selected;
    if (on_change) on_change(selected);
}

bool Checkbox::mouse_clicked(double mx, double my, int button) {
    if (!active || button != 0 || !over(mx, my)) return false;
    play_sound(kClick);
    toggle();
    return true;
}

bool Checkbox::key_pressed(const KeyEvent& event) {
    if (!active || !activates(event)) return false;
    play_sound(kClick);
    toggle();
    return true;
}

Label::Label(int x_, int y_, int w_, Component text, uint32_t colour_, bool centred_)
    : colour(colour_), centred(centred_) {
    x = x_;
    y = y_;
    w = w_;
    h = 9;
    message = std::move(text);
}

void Label::render(GuiGraphics& gfx, double, double) {
    if (centred) {
        gfx.draw_string(message, x + w / 2 - gfx.width_of(message) / 2, y, colour, shadow);
    } else {
        gfx.draw_string(message, x, y, colour, shadow);
    }
}

SelectionList::SelectionList(int x_, int y_, int w_, int h_, int row_height_)
    : row_height(row_height_) {
    x = x_;
    y = y_;
    w = w_;
    h = h_;
}

int SelectionList::row_top(size_t index) const {
    return y + 4 - static_cast<int>(scroll) + static_cast<int>(index) * row_height + header_height;
}

int SelectionList::max_scroll() const {
    const int content = static_cast<int>(rows()) * row_height + header_height + 4;
    return std::max(0, content - (h - 4));
}

void SelectionList::scroll_to(double amount) {
    scroll = std::clamp(amount, 0.0, static_cast<double>(max_scroll()));
}

void SelectionList::ensure_visible(size_t index) {
    const int top = row_top(index) - y - 4;
    if (top < 0) scroll_to(scroll + top);
    const int bottom = top + row_height + 4 + 4;
    if (bottom > h) scroll_to(scroll + (bottom - h));
}

int SelectionList::scrollbar_x() const { return x + w / 2 + row_width() / 2 + 14; }

std::optional<size_t> SelectionList::row_at(double mx, double my) const {
    const int left = x + w / 2 - row_width() / 2;
    const int right = x + w / 2 + row_width() / 2;
    if (mx < left || mx >= right || my < y || my >= y + h) return std::nullopt;
    const int offset = static_cast<int>(std::floor(my - y)) - header_height + static_cast<int>(scroll) - 4;
    if (offset < 0) return std::nullopt;
    const auto index = static_cast<size_t>(offset / row_height);
    if (index >= rows()) return std::nullopt;
    return index;
}

void SelectionList::render(GuiGraphics& gfx, double mx, double my) {
    const bool world = false;
    (void)world;
    gfx.blit_tiled("gui/menu_list_background", x, y, w, h, 32.0f, 32.0f, 0.0f, static_cast<float>(scroll));
    gfx.enable_scissor(x, y, x + w, y + h);
    const auto hovered_row = over(mx, my) ? row_at(mx, my) : std::nullopt;
    for (size_t i = 0; i < rows(); ++i) {
        const int top = row_top(i);
        const int bottom = top + row_height;
        if (bottom < y || top > y + h) continue;
        if (selectable && selected && *selected == i) {
            const int left = x + (w - row_width()) / 2;
            const int right = x + (w + row_width()) / 2;
            const int inner = row_height - 4;
            gfx.fill(left, top - 2, right, top + inner + 2, focused ? 0xFFFFFFFFu : 0xFF808080u);
            gfx.fill(left + 1, top - 1, right - 1, top + inner + 1, 0xFF000000u);
        }
        render_row(gfx, i, row_left(), top, row_width(), row_height - 4, mx, my,
                   hovered_row && *hovered_row == i);
    }
    gfx.disable_scissor();
    gfx.blit_tiled("gui/header_separator", x, y - 2, w, 2, 32.0f, 2.0f);
    gfx.blit_tiled("gui/footer_separator", x, y + h, w, 2, 32.0f, 2.0f);
    if (max_scroll() > 0) {
        const int bar_x = scrollbar_x();
        const int content = static_cast<int>(rows()) * row_height + header_height + 4;
        const int thumb = std::clamp(h * h / std::max(content, 1), 32, h - 8);
        const int thumb_y = static_cast<int>(scroll) * (h - thumb) / max_scroll() + y;
        gfx.blit_sprite("widget/scroller_background", bar_x, y, 6, h);
        gfx.blit_sprite("widget/scroller", bar_x, thumb_y, 6, thumb);
    }
}

bool SelectionList::mouse_clicked(double mx, double my, int button) {
    if (!visible || !over(mx, my)) {
        return false;
    }
    if (button == 0 && max_scroll() > 0 && mx >= scrollbar_x() && mx < scrollbar_x() + 6) {
        scrolling_ = true;
        return true;
    }
    const auto row = row_at(mx, my);
    if (!row) return true;
    if (row_clicked(*row, mx, my, button, row_left(), row_top(*row))) return true;
    if (selectable && button == 0) {
        selected = *row;
        if (on_select) on_select(selected);
    }
    return true;
}

bool SelectionList::mouse_dragged(double, double, int button, double, double dy) {
    if (!scrolling_ || button != 0) return false;
    const int content = static_cast<int>(rows()) * row_height + header_height + 4;
    const int thumb = std::clamp(h * h / std::max(content, 1), 32, h - 8);
    const double ratio = static_cast<double>(max_scroll()) / std::max(1, h - thumb);
    scroll_to(scroll + dy * ratio);
    return true;
}

bool SelectionList::mouse_released(double, double, int) {
    scrolling_ = false;
    return false;
}

// `AbstractScrollArea.mouseScrolled`, `AbstractSelectionList.scrollRate`: scrolls half a row per notch.
bool SelectionList::mouse_scrolled(double, double, double, double dy) {
    scroll_to(scroll - dy * static_cast<double>(row_height) / 2.0);
    return true;
}

bool SelectionList::key_pressed(const KeyEvent& event) {
    if (rows() == 0) return false;
    if (event.key == key::Up || event.key == key::Down) {
        size_t next = 0;
        if (selected) {
            next = event.key == key::Up ? (*selected == 0 ? 0 : *selected - 1)
                                        : std::min(rows() - 1, *selected + 1);
        }
        selected = next;
        ensure_visible(next);
        if (on_select) on_select(selected);
        return true;
    }
    if (selected && activates(event)) {
        row_activated(*selected);
        return true;
    }
    return false;
}

} // namespace mcpp::ui
