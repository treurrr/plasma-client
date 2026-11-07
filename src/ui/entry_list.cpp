#include "mcpp/ui/entry_list.hpp"

#include <algorithm>
#include <cmath>

namespace mcpp::ui {
namespace {

// `KeyEvent.isSelection`: Enter, Space or the keypad's Enter.
bool activates(const KeyEvent& event) {
    return event.key == key::Enter || event.key == key::Space || event.key == key::KeypadEnter;
}

constexpr const char* kDotFrames[] = {"O o o", "o O o", "o o O", "o O o"};

} // namespace

// `LoadingDotsText.get`: one of four frames, 300 ms each.
std::string loading_dots(int64_t millis) {
    const auto frame = static_cast<size_t>(millis / 300LL % 4LL);
    return kDotFrames[frame];
}

EntryList::EntryList(int list_x, int list_y, int list_w, int list_h, int entry_height)
    : default_entry_height(entry_height) {
    x = list_x;
    y = list_y;
    w = list_w;
    h = list_h;
}

// `AbstractSelectionList.contentHeight`: every row's height and four more.
int EntryList::content_total_height() const {
    int total = 0;
    for (const auto& entry : entries_) total += entry->h;
    return total + 4;
}

// `AbstractScrollArea.maxScrollAmount`: what is left over below the view.
int EntryList::max_scroll() const { return std::max(0, content_total_height() - h); }

// `AbstractScrollArea.scrollerHeight`: the thumb, never under 32.
int EntryList::scroller_height() const {
    const int total = std::max(1, content_total_height());
    const auto raw = static_cast<int>(static_cast<float>(h * h) / static_cast<float>(total));
    return std::clamp(raw, 32, std::max(32, h - 8));
}

// `AbstractScrollArea.scrollBarY`: the thumb's top, never above the list.
int EntryList::scroll_bar_y() const {
    const int limit = max_scroll();
    if (limit <= 0) return y;
    return std::max(y, static_cast<int>(scroll_) * (h - scroller_height()) / limit + y);
}

// `AbstractSelectionList.getNextY`, `addEntry`: the row goes under the last one.
size_t EntryList::add_entry(std::unique_ptr<Entry> entry) {
    return add_entry(std::move(entry), default_entry_height);
}

size_t EntryList::add_entry(std::unique_ptr<Entry> entry, int height) {
    int next = first_entry_y() - static_cast<int>(scroll_);
    for (const auto& other : entries_) next += other->h;
    entry->x = row_left();
    entry->w = row_width();
    entry->y = next;
    entry->h = height;
    entries_.push_back(std::move(entry));
    return entries_.size() - 1;
}

void EntryList::clear_entries() {
    entries_.clear();
    selected_.reset();
    hovered_ = nullptr;
    child_focus_ = nullptr;
}

void EntryList::replace_entries(std::vector<std::unique_ptr<Entry>> entries) {
    const int height = default_entry_height;
    clear_entries();
    for (auto& entry : entries) {
        const int own = entry->h > 0 ? entry->h : height;
        add_entry(std::move(entry), own);
    }
}

// `AbstractSelectionList.swap`: two rows trade places and the second is shown.
void EntryList::swap_entries(size_t a, size_t b) {
    if (a >= entries_.size() || b >= entries_.size()) return;
    std::swap(entries_[a], entries_[b]);
    reposition_entries();
    scroll_to_entry(b);
}

int EntryList::index_of(const Entry* entry) const {
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].get() == entry) return static_cast<int>(i);
    }
    return -1;
}

// `AbstractSelectionList.repositionEntries`: the stack rebuilt from the top.
void EntryList::reposition_entries() {
    int next = first_entry_y() - static_cast<int>(scroll_);
    for (auto& entry : entries_) {
        entry->y = next;
        next += entry->h;
        entry->x = row_left();
        entry->w = row_width();
    }
}

// `AbstractSelectionList.setScrollAmount`: clamped, then the rows move.
void EntryList::set_scroll(double amount) {
    scroll_ = std::clamp(amount, 0.0, static_cast<double>(max_scroll()));
    reposition_entries();
}

// `AbstractSelectionList.scrollToEntry`: the least scroll that shows the row.
void EntryList::scroll_to_entry(size_t index) {
    if (index >= entries_.size()) return;
    const auto& entry = *entries_[index];
    const int above = entry.y - y - 2;
    if (above < 0) set_scroll(scroll_ + above);
    const int below = bottom() - entry.y - entry.h - 2;
    if (below < 0) set_scroll(scroll_ - below);
}

// `AbstractSelectionList.centerScrollOn`: the row's middle at the view's.
void EntryList::center_scroll_on(size_t index) {
    int before = 0;
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (i == index) {
            before += entries_[i]->h / 2;
            break;
        }
        before += entries_[i]->h;
    }
    set_scroll(static_cast<double>(before) - static_cast<double>(h) / 2.0);
}

// `AbstractSelectionList.setSelected`: scrolled into view when the keyboard
// moved it or it sits outside.
void EntryList::set_selected(std::optional<size_t> index) {
    selected_ = index;
    if (index && *index < entries_.size()) {
        const auto& entry = *entries_[*index];
        const bool above = entry.content_y() < y;
        const bool below = entry.content_bottom() > bottom();
        if (Widget::keyboard_focus || above || below) scroll_to_entry(*index);
    }
    if (on_select) on_select(selected_);
}

// `AbstractSelectionList.updateSizeAndPosition`: size, place, rows, scroll.
void EntryList::update_size(int list_w, int list_h, int top) {
    w = list_w;
    h = list_h;
    x = 0;
    y = top;
    reposition_entries();
    if (selected_) scroll_to_entry(*selected_);
    refresh_scroll();
}

EntryList::Entry* EntryList::entry_at(double mx, double my) const {
    for (const auto& entry : entries_) {
        if (entry->over(mx, my)) return entry.get();
    }
    return nullptr;
}

// `AbstractSelectionList.renderWidget`: background, rows inside the scissor,
// the separator lines, then the scroll bar.
void EntryList::render(GuiGraphics& gfx, double mx, double my) {
    hovered_ = over(mx, my) ? entry_at(mx, my) : nullptr;
    const char* background = in_world ? "gui/inworld_menu_list_background" : "gui/menu_list_background";
    gfx.blit_tiled(background, x, y, w, h, 32.0f, 32.0f, 0.0f, static_cast<float>(scroll_));
    gfx.enable_scissor(x, y, x + w, y + h);
    for (size_t i = 0; i < entries_.size(); ++i) {
        auto& entry = *entries_[i];
        if (entry.y + entry.h < y || entry.y > bottom()) continue;
        if (entries_can_be_selected() && selected_ && *selected_ == i) {
            const uint32_t colour = focused ? 0xFFFFFFFFu : 0xFF808080u;
            gfx.fill(entry.x, entry.y, entry.x + entry.w, entry.y + entry.h, colour);
            gfx.fill(entry.x + 1, entry.y + 1, entry.x + entry.w - 1, entry.y + entry.h - 1,
                     0xFF000000u);
        }
        entry.render_content(gfx, mx, my, hovered_ == &entry);
    }
    gfx.disable_scissor();
    gfx.blit_tiled(in_world ? "gui/inworld_header_separator" : "gui/header_separator", x, y - 2, w,
                   2, 32.0f, 2.0f);
    gfx.blit_tiled(in_world ? "gui/inworld_footer_separator" : "gui/footer_separator", x, bottom(),
                   w, 2, 32.0f, 2.0f);
    if (max_scroll() > 0) {
        const int bar = scroll_bar_x();
        gfx.blit_sprite("widget/scroller_background", bar, y, 6, h);
        gfx.blit_sprite("widget/scroller", bar, scroll_bar_y(), 6, scroller_height());
    }
}

// `AbstractScrollArea.updateScrolling`, `AbstractSelectionList.mouseClicked`.
bool EntryList::mouse_clicked(double mx, double my, int button) {
    if (!visible || !over(mx, my)) return false;
    if (max_scroll() > 0 && mx >= scroll_bar_x() && mx <= scroll_bar_x() + 6) {
        scrolling_ = true;
        return true;
    }
    Entry* hit = entry_at(mx, my);
    if (hit == nullptr) return true;
    child_focus_ = nullptr;
    for (Widget* child : hit->children()) {
        if (child != nullptr && child->mouse_clicked(mx, my, button)) {
            child_focus_ = child;
            child->set_focused(true);
            return true;
        }
    }
    const int index = index_of(hit);
    if (entries_can_be_selected() && button == 0 && index >= 0) {
        set_selected(static_cast<size_t>(index));
    }
    return hit->mouse_clicked(mx, my, button, double_click);
}

bool EntryList::mouse_dragged(double mx, double my, int button, double dx, double dy) {
    if (!scrolling_) {
        if (child_focus_ != nullptr) return child_focus_->mouse_dragged(mx, my, button, dx, dy);
        return false;
    }
    if (my < y) {
        set_scroll(0.0);
    } else if (my > bottom()) {
        set_scroll(static_cast<double>(max_scroll()));
    } else {
        const double limit = std::max(1, max_scroll());
        const double rate = std::max(1.0, limit / static_cast<double>(h - scroller_height()));
        set_scroll(scroll_ + dy * rate);
    }
    return true;
}

bool EntryList::mouse_released(double mx, double my, int button) {
    scrolling_ = false;
    if (child_focus_ != nullptr) return child_focus_->mouse_released(mx, my, button);
    return false;
}

// `AbstractScrollArea.mouseScrolled`, `AbstractSelectionList.scrollRate`: half
// a default row per notch.
bool EntryList::mouse_scrolled(double, double, double, double dy) {
    if (!visible) return false;
    set_scroll(scroll_ - dy * (static_cast<double>(default_entry_height) / 2.0));
    return true;
}

std::optional<size_t> EntryList::next_entry(int step) const {
    if (entries_.empty() || step == 0) return std::nullopt;
    int at = 0;
    if (!selected_) {
        at = step > 0 ? 0 : static_cast<int>(entries_.size()) - 1;
    } else {
        at = static_cast<int>(*selected_) + step;
    }
    if (at < 0 || at >= static_cast<int>(entries_.size())) return std::nullopt;
    return static_cast<size_t>(at);
}

bool EntryList::key_pressed(const KeyEvent& event) {
    if (child_focus_ != nullptr && child_focus_->key_pressed(event)) return true;
    if (entries_.empty()) return false;
    if (event.key == key::Up || event.key == key::Down) {
        const auto next = next_entry(event.key == key::Up ? -1 : 1);
        if (!next) return false;
        set_selected(*next);
        return true;
    }
    Entry* current = selected();
    if (current != nullptr && current->key_pressed(event)) return true;
    if (current != nullptr && activates(event)) return true;
    return false;
}

bool EntryList::char_typed(uint32_t code, int mods) {
    return child_focus_ != nullptr && child_focus_->char_typed(code, mods);
}

void EntryList::set_focused(bool on) {
    Widget::set_focused(on);
    if (!on && child_focus_ != nullptr) {
        child_focus_->set_focused(false);
        child_focus_ = nullptr;
    }
}

LoadingDotsWidget::LoadingDotsWidget(int label_width, Component label) {
    w = label_width;
    h = Font::kLineHeight * 3;
    message = std::move(label);
    active = false;
}

// `LoadingDotsWidget.renderWidget`: the label a line up, the dots a line down.
void LoadingDotsWidget::render(GuiGraphics& gfx, double, double) {
    const int cx = x + w / 2;
    const int cy = y + h / 2;
    const auto runs = gfx.runs(message);
    gfx.draw_string(runs, cx - gfx.width_of(runs) / 2, cy - Font::kLineHeight, 0xFFFFFFFFu);
    const std::string dots = loading_dots(gfx.millis);
    gfx.draw_string(dots, cx - gfx.width_of(dots) / 2, cy + Font::kLineHeight, 0xFF808080u);
}

MultiLineTextWidget::MultiLineTextWidget(Component text, int width, bool centre)
    : max_width(width), centred(centre) {
    message = std::move(text);
    active = false;
    w = width;
    h = Font::kLineHeight;
}

// `MultiLineTextWidget.getWidth`, `getHeight`: the widest line and nine a line.
void MultiLineTextWidget::measure(const Font& font) {
    lines_ = font.split(message, max_width);
    if (lines_.empty()) lines_.emplace_back();
    int widest = 0;
    for (const auto& line : lines_) widest = std::max(widest, font.width(line));
    w = widest;
    h = static_cast<int>(lines_.size()) * Font::kLineHeight;
}

void MultiLineTextWidget::render(GuiGraphics& gfx, double, double) {
    if (lines_.empty()) measure(gfx.font);
    int line_y = y;
    for (const auto& line : lines_) {
        if (centred) {
            gfx.draw_string(line, x + (w - gfx.width_of(line)) / 2, line_y, 0xFFFFFFFFu);
        } else {
            gfx.draw_string(line, x, line_y, 0xFFFFFFFFu);
        }
        line_y += Font::kLineHeight;
    }
}

} // namespace mcpp::ui
