#pragma once
#include "mcpp/ui/screen.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

// The game's widgets, drawn from its own sprites in nine slices.
namespace mcpp::ui {

class Button : public Widget {
public:
    Button(int x_, int y_, int w_, int h_, Component label, std::function<void(Button&)> press);
    void render(GuiGraphics& gfx, double mx, double my) override;
    bool mouse_clicked(double mx, double my, int button) override;
    bool key_pressed(const KeyEvent& event) override;
    std::string narration(const GuiGraphics& gfx) const override;
    virtual void press();

    std::function<void(Button&)> on_press;

protected:
    void render_label(GuiGraphics& gfx);
};

// A button that is only a picture, with a highlighted twin: the server list's
// arrows, the recipe book's green book, the title screen's corner icons.
class SpriteButton : public Button {
public:
    SpriteButton(int x_, int y_, int w_, int h_, std::string sprite, std::string highlighted,
                 Component narration_label, std::function<void(Button&)> press);
    void render(GuiGraphics& gfx, double mx, double my) override;

    std::string sprite;
    std::string sprite_highlighted;
    std::string sprite_disabled;
};

// A button with a picture: in place of its label (the title screen's
// language and accessibility buttons), or beside it.
class SpriteIconButton : public Button {
public:
    SpriteIconButton(int w_, int h_, Component label, std::string sprite, int sprite_w,
                     int sprite_h, bool icon_only, std::function<void(Button&)> press);
    void render(GuiGraphics& gfx, double mx, double my) override;

    std::string sprite;
    int sprite_w;
    int sprite_h;
    bool icon_only;

private:
    void render_sprite(GuiGraphics& gfx, int sx, int sy);
};

// `CommonButtons.language` and `CommonButtons.accessibility`.
std::unique_ptr<SpriteIconButton> language_button(int width, std::function<void(Button&)> press,
                                                  bool icon_only);
std::unique_ptr<SpriteIconButton> accessibility_button(int width,
                                                       std::function<void(Button&)> press,
                                                       bool icon_only);

// Text that is a button: underlined while the pointer is on it.
class PlainTextButton : public Button {
public:
    PlainTextButton(int x_, int y_, int w_, int h_, Component label,
                    std::function<void(Button&)> press);
    void render(GuiGraphics& gfx, double mx, double my) override;
};

// One line of text that is not interactive, cut with an ellipsis when it is
// wider than `max_width`.
class StringWidget : public Widget {
public:
    StringWidget(int x_, int y_, int w_, int h_, Component text);
    void render(GuiGraphics& gfx, double mx, double my) override;
    bool focusable() const override { return false; }

    int max_width = 0;
};

// "Name: Value", stepping through its values on each click, backwards with
// shift held.
class CycleButton : public Button {
public:
    CycleButton(int x_, int y_, int w_, int h_, Component name, std::vector<Component> values,
                size_t index, std::function<void(size_t)> changed);
    void press() override;
    bool mouse_clicked(double mx, double my, int button) override;
    void refresh();

    Component name;
    std::vector<Component> values;
    size_t index;
    // Only the value is shown, as some option buttons do.
    bool value_only = false;
    std::function<void(size_t)> on_change;

private:
    bool backwards_ = false;
};

class Slider : public Widget {
public:
    // `label` turns the value, 0 to 1, into what the slider says.
    Slider(int x_, int y_, int w_, int h_, double value, std::function<Component(double)> label,
           std::function<void(double)> changed);
    void render(GuiGraphics& gfx, double mx, double my) override;
    bool mouse_clicked(double mx, double my, int button) override;
    bool mouse_dragged(double mx, double my, int button, double dx, double dy) override;
    bool mouse_released(double mx, double my, int button) override;
    bool key_pressed(const KeyEvent& event) override;
    void set_focused(bool on) override;
    void set_value(double value);

    double value;
    std::function<Component(double)> label;
    std::function<void(double)> on_change;

private:
    void set_from_mouse(double mx);
    bool can_change_ = false;
};

class EditBox : public Widget {
public:
    EditBox(int x_, int y_, int w_, int h_, Component narration_label);
    void render(GuiGraphics& gfx, double mx, double my) override;
    void tick() override;
    bool mouse_clicked(double mx, double my, int button) override;
    bool key_pressed(const KeyEvent& event) override;
    bool char_typed(uint32_t code, int mods) override;
    void set_focused(bool on) override;

    const std::string& value() const { return text_; }
    void set_value(std::string text);
    void insert(std::string_view text);
    void erase(int count);

    size_t max_length = 32;
    bool bordered = true;
    bool editable = true;
    uint32_t text_colour = 0xFFE0E0E0u;
    uint32_t text_colour_uneditable = 0xFF707070u;
    Component hint;
    // Grey ghost text after the caret: the completion a command field offers.
    std::string suggestion;
    std::function<bool(const std::string&)> filter;
    std::function<void(const std::string&)> on_change;
    Platform* platform = nullptr;

private:
    void move_cursor(int to, bool select);
    int word_from(int from, int direction) const;
    int inner_width() const { return bordered ? w - 8 : w; }

    std::string text_;
    int cursor_ = 0;
    int selection_ = 0;
    int display_ = 0;
    int frame_ = 0;
    GuiGraphics* last_gfx_ = nullptr;
    double focused_at_ = 0.0;
    bool was_focused_ = false;
};

class Checkbox : public Widget {
public:
    Checkbox(int x_, int y_, Component label, bool selected, std::function<void(bool)> changed);
    void render(GuiGraphics& gfx, double mx, double my) override;
    bool mouse_clicked(double mx, double my, int button) override;
    bool key_pressed(const KeyEvent& event) override;
    void toggle();

    bool selected;
    std::function<void(bool)> on_change;
    static constexpr int kBox = 17;
};

// A line of text that is not interactive.
class Label : public Widget {
public:
    Label(int x_, int y_, int w_, Component text, uint32_t colour = 0xFFFFFFFFu,
          bool centred_ = true);
    void render(GuiGraphics& gfx, double mx, double my) override;
    bool focusable() const override { return false; }

    uint32_t colour;
    bool centred;
    bool shadow = true;
};

// The list behind the server list, the world list, the options rows and the
// key bindings: rows of one height, a scroll bar, a selection, and the
// header and footer lines.
class SelectionList : public Widget {
public:
    SelectionList(int x_, int y_, int w_, int h_, int row_height);
    void render(GuiGraphics& gfx, double mx, double my) override;
    bool mouse_clicked(double mx, double my, int button) override;
    bool mouse_dragged(double mx, double my, int button, double dx, double dy) override;
    bool mouse_released(double mx, double my, int button) override;
    bool mouse_scrolled(double mx, double my, double dx, double dy) override;
    bool key_pressed(const KeyEvent& event) override;

    virtual size_t rows() const = 0;
    virtual void render_row(GuiGraphics& gfx, size_t index, int left, int top, int width, int height,
                            double mx, double my, bool hovered) = 0;
    virtual bool row_clicked(size_t, double, double, int, int, int) { return false; }
    virtual void row_activated(size_t) {}
    virtual int row_width() const { return 220; }

    int row_left() const { return x + w / 2 - row_width() / 2 + 2; }
    int row_top(size_t index) const;
    int max_scroll() const;
    void scroll_to(double amount);
    void ensure_visible(size_t index);
    int scrollbar_x() const;
    std::optional<size_t> row_at(double mx, double my) const;

    std::optional<size_t> selected;
    double scroll = 0.0;
    int row_height;
    int header_height = 0;
    bool selectable = true;
    std::function<void(std::optional<size_t>)> on_select;

private:
    bool scrolling_ = false;
};

} // namespace mcpp::ui
