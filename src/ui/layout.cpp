#include "mcpp/ui/layout.hpp"

#include "mcpp/ui/screen.hpp"

#include <algorithm>
#include <cmath>

namespace mcpp::ui {
namespace {

float lerp(float t, float from, float to) { return from + t * (to - from); }

} // namespace


Divisor::Divisor(int total, int parts) : parts_(parts) {
    quotient_ = parts > 0 ? total / parts : 0;
    mod_ = parts > 0 ? total % parts : 0;
}

int Divisor::next() {
    int out = quotient_;
    remainder_ += mod_;
    if (remainder_ >= parts_) {
        remainder_ -= parts_;
        ++out;
    }
    ++returned_;
    return out;
}

void AbstractLayout::set_x(int x) {
    visit_children([&](LayoutElement& child) { child.set_x(child.get_x() + (x - x_)); });
    x_ = x;
}

void AbstractLayout::set_y(int y) {
    visit_children([&](LayoutElement& child) { child.set_y(child.get_y() + (y - y_)); });
    y_ = y;
}

void AbstractLayout::visit_widgets(const std::function<void(Widget&)>& visit) {
    visit_children([&](LayoutElement& child) { child.visit_widgets(visit); });
}

void AbstractLayout::arrange_elements() {
    visit_children([](LayoutElement& child) { child.arrange_elements(); });
}

int AbstractLayout::Child::height() const {
    return element->get_height() + settings.padding_top + settings.padding_bottom;
}

int AbstractLayout::Child::width() const {
    return element->get_width() + settings.padding_left + settings.padding_right;
}

void AbstractLayout::Child::set_x(int x, int cell_width) {
    const auto from = static_cast<float>(settings.padding_left);
    const auto to =
        static_cast<float>(cell_width - element->get_width() - settings.padding_right);
    element->set_x(static_cast<int>(lerp(settings.x_alignment, from, to)) + x);
}

void AbstractLayout::Child::set_y(int y, int cell_height) {
    const auto from = static_cast<float>(settings.padding_top);
    const auto to =
        static_cast<float>(cell_height - element->get_height() - settings.padding_bottom);
    element->set_y(static_cast<int>(std::floor(lerp(settings.y_alignment, from, to) + 0.5f)) + y);
}

void GridLayout::visit_children(const std::function<void(LayoutElement&)>& visit) {
    for (auto& cell : cells_) visit(*cell.child.element);
}

void GridLayout::arrange_elements() {
    AbstractLayout::arrange_elements();
    int last_row = 0;
    int last_column = 0;
    for (const auto& cell : cells_) {
        last_row = std::max(cell.last_row(), last_row);
        last_column = std::max(cell.last_column(), last_column);
    }
    std::vector<int> widths(static_cast<size_t>(last_column + 1), 0);
    std::vector<int> heights(static_cast<size_t>(last_row + 1), 0);
    for (const auto& cell : cells_) {
        Divisor rows(cell.child.height() - (cell.rows - 1) * row_spacing_, cell.rows);
        for (int r = cell.row; r <= cell.last_row(); ++r) {
            heights[static_cast<size_t>(r)] = std::max(heights[static_cast<size_t>(r)], rows.next());
        }
        Divisor columns(cell.child.width() - (cell.columns - 1) * column_spacing_, cell.columns);
        for (int c = cell.column; c <= cell.last_column(); ++c) {
            widths[static_cast<size_t>(c)] = std::max(widths[static_cast<size_t>(c)], columns.next());
        }
    }
    std::vector<int> column_x(widths.size(), 0);
    std::vector<int> row_y(heights.size(), 0);
    for (size_t c = 1; c < widths.size(); ++c) {
        column_x[c] = column_x[c - 1] + widths[c - 1] + column_spacing_;
    }
    for (size_t r = 1; r < heights.size(); ++r) {
        row_y[r] = row_y[r - 1] + heights[r - 1] + row_spacing_;
    }
    for (auto& cell : cells_) {
        int w = 0;
        for (int c = cell.column; c <= cell.last_column(); ++c) w += widths[static_cast<size_t>(c)];
        w += column_spacing_ * (cell.columns - 1);
        cell.child.set_x(get_x() + column_x[static_cast<size_t>(cell.column)], w);
        int h = 0;
        for (int r = cell.row; r <= cell.last_row(); ++r) h += heights[static_cast<size_t>(r)];
        h += row_spacing_ * (cell.rows - 1);
        cell.child.set_y(get_y() + row_y[static_cast<size_t>(cell.row)], h);
    }
    width_ = column_x.back() + widths.back();
    height_ = row_y.back() + heights.back();
}

FrameLayout::FrameLayout(int x, int y, int w, int h) : AbstractLayout(x, y, w, h) {
    set_min_dimensions(w, h);
    defaults_.align(0.5f, 0.5f);
}

void FrameLayout::visit_children(const std::function<void(LayoutElement&)>& visit) {
    for (auto& child : children_) visit(*child.element);
}

void FrameLayout::arrange_elements() {
    AbstractLayout::arrange_elements();
    int w = min_width_;
    int h = min_height_;
    for (const auto& child : children_) {
        w = std::max(w, child.width());
        h = std::max(h, child.height());
    }
    for (auto& child : children_) {
        child.set_x(get_x(), w);
        child.set_y(get_y(), h);
    }
    width_ = w;
    height_ = h;
}

void FrameLayout::center_in_rectangle(LayoutElement& element, int x, int y, int w, int h) {
    align_in_rectangle(element, x, y, w, h, 0.5f, 0.5f);
}

void FrameLayout::align_in_rectangle(LayoutElement& element, int x, int y, int w, int h,
                                     float align_x, float align_y) {
    element.set_x(x + static_cast<int>(
                          lerp(align_x, 0.0f, static_cast<float>(w - element.get_width()))));
    element.set_y(y + static_cast<int>(
                          lerp(align_y, 0.0f, static_cast<float>(h - element.get_height()))));
}

void EqualSpacingLayout::visit_children(const std::function<void(LayoutElement&)>& visit) {
    for (auto& child : children_) visit(*child.element);
}

void EqualSpacingLayout::arrange_elements() {
    AbstractLayout::arrange_elements();
    if (children_.empty()) return;
    const bool horizontal = orientation_ == Orientation::Horizontal;
    const auto primary = [&](const Child& c) { return horizontal ? c.width() : c.height(); };
    const auto secondary = [&](const Child& c) { return horizontal ? c.height() : c.width(); };
    int used = 0;
    int across = horizontal ? height_ : width_;
    for (const auto& child : children_) {
        used += primary(child);
        across = std::max(across, secondary(child));
    }
    const int left_over = (horizontal ? width_ : height_) - used;
    int at = horizontal ? get_x() : get_y();
    const auto place = [&](Child& child, int position) {
        if (horizontal) {
            child.set_x(position, child.width());
        } else {
            child.set_y(position, child.height());
        }
    };
    place(children_[0], at);
    at += primary(children_[0]);
    if (children_.size() >= 2) {
        Divisor gaps(left_over, static_cast<int>(children_.size()) - 1);
        for (size_t i = 1; i < children_.size(); ++i) {
            at += gaps.next();
            place(children_[i], at);
            at += primary(children_[i]);
        }
    }
    for (auto& child : children_) {
        if (horizontal) {
            child.set_y(get_y(), across);
        } else {
            child.set_x(get_x(), across);
        }
    }
    if (horizontal) {
        height_ = across;
    } else {
        width_ = across;
    }
}

HeaderAndFooterLayout::HeaderAndFooterLayout(const int& screen_width, const int& screen_height,
                                             int header, int footer)
    : screen_width_(screen_width), screen_height_(screen_height), header_height_(header),
      footer_height_(footer) {}

void HeaderAndFooterLayout::visit_widgets(const std::function<void(Widget&)>& visit) {
    header_.visit_widgets(visit);
    contents_.visit_widgets(visit);
    footer_.visit_widgets(visit);
}

void HeaderAndFooterLayout::arrange_elements() {
    header_.set_min_width(screen_width_);
    header_.set_min_height(header_height_);
    header_.set_position(0, 0);
    header_.arrange_elements();
    footer_.set_min_width(screen_width_);
    footer_.set_min_height(footer_height_);
    footer_.arrange_elements();
    footer_.set_y(screen_height_ - footer_height_);
    contents_.set_min_width(screen_width_);
    contents_.arrange_elements();
    const int below_header = header_height_ + 30;
    const int above_footer = screen_height_ - footer_height_ - contents_.get_height();
    contents_.set_position(0, std::min(below_header, above_footer));
}

} // namespace mcpp::ui
