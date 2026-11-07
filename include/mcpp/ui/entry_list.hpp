#pragma once
#include "mcpp/ui/screen.hpp"
#include "mcpp/ui/widgets.hpp"

#include <memory>
#include <optional>
#include <vector>

// The game's AbstractSelectionList: rows of their own heights stacked from the
// top, a scroll bar, a selection, and the header and footer lines over the
// tiled background. The server list, the options rows, the key bindings and
// the language list are all this list with different rows.
namespace mcpp::ui {

class EntryList : public Widget {
public:
    // The game's AbstractSelectionList.Entry: a row that knows its own box.
    class Entry {
    public:
        virtual ~Entry() = default;

        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;

        int content_x() const { return x + 2; }
        int content_y() const { return y + 2; }
        int content_height() const { return h - 4; }
        int content_y_middle() const { return content_y() + content_height() / 2; }
        int content_bottom() const { return content_y() + content_height(); }
        int content_width() const { return w - 4; }
        int content_x_middle() const { return content_x() + content_width() / 2; }
        int content_right() const { return content_x() + content_width(); }
        bool over(double mx, double my) const {
            return mx >= x && mx < x + w && my >= y && my < y + h;
        }

        virtual void render_content(GuiGraphics& gfx, double mx, double my, bool hovered) = 0;
        virtual bool mouse_clicked(double mx, double my, int button, bool double_click) {
            (void)mx;
            (void)my;
            (void)button;
            (void)double_click;
            return true;
        }
        virtual bool key_pressed(const KeyEvent&) { return false; }
        virtual void refresh() {}
        virtual Component narration() const { return literal(""); }
        // The widgets a row of a ContainerObjectSelectionList holds.
        virtual std::vector<Widget*> children() { return {}; }
    };

    EntryList(int list_x, int list_y, int list_w, int list_h, int default_entry_height);

    void render(GuiGraphics& gfx, double mx, double my) override;
    bool mouse_clicked(double mx, double my, int button) override;
    bool mouse_dragged(double mx, double my, int button, double dx, double dy) override;
    bool mouse_released(double mx, double my, int button) override;
    bool mouse_scrolled(double mx, double my, double dx, double dy) override;
    bool key_pressed(const KeyEvent& event) override;
    bool char_typed(uint32_t code, int mods) override;
    void set_focused(bool on) override;

    // `AbstractSelectionList.getRowWidth`: 220 unless a list says otherwise.
    virtual int row_width() const { return 220; }
    // `ContainerObjectSelectionList.entriesCanBeSelected`: false for the rows
    // that are only holders of widgets.
    virtual bool entries_can_be_selected() const { return true; }

    size_t add_entry(std::unique_ptr<Entry> entry);
    size_t add_entry(std::unique_ptr<Entry> entry, int height);
    void clear_entries();
    void replace_entries(std::vector<std::unique_ptr<Entry>> entries);
    void swap_entries(size_t a, size_t b);

    const std::vector<std::unique_ptr<Entry>>& children() const { return entries_; }
    size_t size() const { return entries_.size(); }
    Entry* entry(size_t index) const {
        return index < entries_.size() ? entries_[index].get() : nullptr;
    }
    Entry* selected() const {
        return selected_ && *selected_ < entries_.size() ? entries_[*selected_].get() : nullptr;
    }
    std::optional<size_t> selected_index() const { return selected_; }
    void set_selected(std::optional<size_t> index);
    int index_of(const Entry* entry) const;

    // `AbstractSelectionList.updateSize`: the width, the height between header
    // and footer, and the header's height as the top.
    void update_size(int list_w, int list_h, int top);
    void set_scroll(double amount);
    double scroll() const { return scroll_; }
    void refresh_scroll() { set_scroll(scroll_); }
    void center_scroll_on(size_t index);

    int first_entry_y() const { return y + 2; }
    int content_total_height() const;
    int max_scroll() const;
    int scroller_height() const;
    int scroll_bar_x() const { return row_right() + 6 + 2; }
    int scroll_bar_y() const;
    int row_left() const { return x + w / 2 - row_width() / 2; }
    int row_right() const { return row_left() + row_width(); }
    int bottom() const { return y + h; }
    Entry* entry_at(double mx, double my) const;
    Entry* hovered_entry() const { return hovered_; }

    std::function<void(std::optional<size_t>)> on_select;
    // What the game reads off Minecraft: the in-world textures and whether
    // this click is the second of a pair.
    bool in_world = false;
    bool double_click = false;

protected:
    void reposition_entries();
    void scroll_to_entry(size_t index);
    // `AbstractSelectionList.nextEntry`: the next row in that direction.
    std::optional<size_t> next_entry(int step) const;

    int default_entry_height;
    std::vector<std::unique_ptr<Entry>> entries_;
    std::optional<size_t> selected_;
    Entry* hovered_ = nullptr;
    double scroll_ = 0.0;
    bool scrolling_ = false;
    Widget* child_focus_ = nullptr;
};

// `LoadingDotsWidget`: a label with the four turning dots under it.
class LoadingDotsWidget : public Widget {
public:
    LoadingDotsWidget(int label_width, Component label);
    void render(GuiGraphics& gfx, double mx, double my) override;
    bool focusable() const override { return false; }
};

// `MultiLineTextWidget`: text wrapped to `max_width`, optionally centred.
class MultiLineTextWidget : public Widget {
public:
    MultiLineTextWidget(Component text, int max_width, bool centred);
    void render(GuiGraphics& gfx, double mx, double my) override;
    bool focusable() const override { return false; }
    // Measured from the font the first time the widget is laid out.
    void measure(const Font& font);

    int max_width;
    bool centred;

private:
    std::vector<std::vector<Run>> lines_;
};

// `LoadingDotsText.get`: "O o o", one frame every 300 ms.
std::string loading_dots(int64_t millis);

} // namespace mcpp::ui
