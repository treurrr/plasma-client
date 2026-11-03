#pragma once
#include <functional>
#include <vector>

// The game's layouts: a screen's init() places its widgets by building these
// and arranging them, so every position falls out of the same arithmetic the
// game does - integer division, the remainder handed out one pixel at a time,
// alignment truncated on x and rounded on y.
namespace mcpp::ui {

class Widget;

class LayoutElement {
public:
    virtual ~LayoutElement() = default;
    virtual void set_x(int x) = 0;
    virtual void set_y(int y) = 0;
    virtual int get_x() const = 0;
    virtual int get_y() const = 0;
    virtual int get_width() const = 0;
    virtual int get_height() const = 0;
    virtual void visit_widgets(const std::function<void(Widget&)>& visit) = 0;
    // Layouts arrange their children; widgets have nothing to arrange.
    virtual void arrange_elements() {}
    void set_position(int x, int y) {
        set_x(x);
        set_y(y);
    }
};

struct LayoutSettings {
    int padding_left = 0;
    int padding_top = 0;
    int padding_right = 0;
    int padding_bottom = 0;
    float x_alignment = 0.0f;
    float y_alignment = 0.0f;

    LayoutSettings& padding(int all) { return padding(all, all); }
    LayoutSettings& padding(int horizontal, int vertical) {
        padding_left = padding_right = horizontal;
        padding_top = padding_bottom = vertical;
        return *this;
    }
    LayoutSettings& padding(int left, int top, int right, int bottom) {
        padding_left = left;
        padding_top = top;
        padding_right = right;
        padding_bottom = bottom;
        return *this;
    }
    LayoutSettings& align(float x, float y) {
        x_alignment = x;
        y_alignment = y;
        return *this;
    }
    LayoutSettings& align_horizontally(float x) {
        x_alignment = x;
        return *this;
    }
    LayoutSettings& align_vertically(float y) {
        y_alignment = y;
        return *this;
    }
};

// Splits `total` into `parts` whole numbers, the remainder spread one by one.
class Divisor {
public:
    Divisor(int total, int parts);
    bool has_next() const { return returned_ < parts_; }
    int next();

private:
    int parts_;
    int quotient_;
    int mod_;
    int returned_ = 0;
    int remainder_ = 0;
};

class AbstractLayout : public LayoutElement {
public:
    AbstractLayout(int x, int y, int w, int h) : x_(x), y_(y), width_(w), height_(h) {}
    void set_x(int x) override;
    void set_y(int y) override;
    int get_x() const override { return x_; }
    int get_y() const override { return y_; }
    int get_width() const override { return width_; }
    int get_height() const override { return height_; }
    void visit_widgets(const std::function<void(Widget&)>& visit) override;
    void arrange_elements() override;
    virtual void visit_children(const std::function<void(LayoutElement&)>& visit) = 0;

protected:
    struct Child {
        LayoutElement* element;
        LayoutSettings settings;
        int height() const;
        int width() const;
        void set_x(int x, int cell_width);
        void set_y(int y, int cell_height);
    };

    int x_;
    int y_;
    int width_;
    int height_;
};

class GridLayout : public AbstractLayout {
public:
    GridLayout(int x = 0, int y = 0) : AbstractLayout(x, y, 0, 0) {}
    void arrange_elements() override;
    void visit_children(const std::function<void(LayoutElement&)>& visit) override;

    template <typename T> T& add_child(T& element, int row, int column) {
        return add_child(element, row, column, 1, 1, defaults_);
    }
    template <typename T>
    T& add_child(T& element, int row, int column, int rows, int columns,
                 const LayoutSettings& settings) {
        cells_.push_back({{&element, settings}, row, column, rows, columns});
        return element;
    }
    template <typename T>
    T& add_child(T& element, int row, int column, const LayoutSettings& settings) {
        return add_child(element, row, column, 1, 1, settings);
    }
    GridLayout& column_spacing(int spacing) {
        column_spacing_ = spacing;
        return *this;
    }
    GridLayout& row_spacing(int spacing) {
        row_spacing_ = spacing;
        return *this;
    }
    GridLayout& spacing(int s) { return column_spacing(s).row_spacing(s); }
    LayoutSettings new_cell_settings() const { return defaults_; }
    LayoutSettings& default_cell_setting() { return defaults_; }

    // Fills the grid row by row, `columns` across; a child wider than what is
    // left of a row starts the next one.
    class RowHelper {
    public:
        RowHelper(GridLayout& grid, int columns) : grid_(grid), columns_(columns) {}
        template <typename T> T& add_child(T& element, int span = 1) {
            return add_child(element, span, grid_.defaults_);
        }
        template <typename T> T& add_child(T& element, int span, const LayoutSettings& settings) {
            int row = index_ / columns_;
            int column = index_ % columns_;
            if (column + span > columns_) {
                ++row;
                column = 0;
                index_ = (index_ + columns_ - 1) / columns_ * columns_;
            }
            index_ += span;
            return grid_.add_child(element, row, column, 1, span, settings);
        }

    private:
        GridLayout& grid_;
        int columns_;
        int index_ = 0;
    };
    RowHelper create_row_helper(int columns) { return RowHelper(*this, columns); }

private:
    struct Cell {
        Child child;
        int row;
        int column;
        int rows;
        int columns;
        int last_row() const { return row + rows - 1; }
        int last_column() const { return column + columns - 1; }
    };
    std::vector<Cell> cells_;
    LayoutSettings defaults_;
    int row_spacing_ = 0;
    int column_spacing_ = 0;
};

class LinearLayout : public LayoutElement {
public:
    enum class Orientation { Horizontal, Vertical };
    LinearLayout(int x, int y, Orientation orientation) : grid_(x, y), orientation_(orientation) {}
    static LinearLayout vertical() { return LinearLayout(0, 0, Orientation::Vertical); }
    static LinearLayout horizontal() { return LinearLayout(0, 0, Orientation::Horizontal); }

    LinearLayout& spacing(int s) {
        if (orientation_ == Orientation::Horizontal) {
            grid_.column_spacing(s);
        } else {
            grid_.row_spacing(s);
        }
        return *this;
    }
    LayoutSettings new_cell_settings() const { return grid_.new_cell_settings(); }
    LayoutSettings& default_cell_setting() { return grid_.default_cell_setting(); }
    template <typename T> T& add_child(T& element) { return add_child(element, new_cell_settings()); }
    template <typename T> T& add_child(T& element, const LayoutSettings& settings) {
        const int index = next_++;
        return orientation_ == Orientation::Horizontal
                   ? grid_.add_child(element, 0, index, settings)
                   : grid_.add_child(element, index, 0, settings);
    }

    void set_x(int x) override { grid_.set_x(x); }
    void set_y(int y) override { grid_.set_y(y); }
    int get_x() const override { return grid_.get_x(); }
    int get_y() const override { return grid_.get_y(); }
    int get_width() const override { return grid_.get_width(); }
    int get_height() const override { return grid_.get_height(); }
    void visit_widgets(const std::function<void(Widget&)>& visit) override {
        grid_.visit_widgets(visit);
    }
    void arrange_elements() override { grid_.arrange_elements(); }

private:
    GridLayout grid_;
    Orientation orientation_;
    int next_ = 0;
};

class FrameLayout : public AbstractLayout {
public:
    FrameLayout(int x = 0, int y = 0, int w = 0, int h = 0);
    FrameLayout& set_min_dimensions(int w, int h) {
        min_width_ = w;
        min_height_ = h;
        return *this;
    }
    FrameLayout& set_min_width(int w) {
        min_width_ = w;
        return *this;
    }
    FrameLayout& set_min_height(int h) {
        min_height_ = h;
        return *this;
    }
    LayoutSettings new_child_layout_settings() const { return defaults_; }
    LayoutSettings& default_child_layout_setting() { return defaults_; }
    template <typename T> T& add_child(T& element) { return add_child(element, defaults_); }
    template <typename T> T& add_child(T& element, const LayoutSettings& settings) {
        children_.push_back({&element, settings});
        return element;
    }
    void arrange_elements() override;
    void visit_children(const std::function<void(LayoutElement&)>& visit) override;

    static void center_in_rectangle(LayoutElement& element, int x, int y, int w, int h);
    static void align_in_rectangle(LayoutElement& element, int x, int y, int w, int h,
                                   float align_x, float align_y);

private:
    std::vector<Child> children_;
    LayoutSettings defaults_;
    int min_width_ = 0;
    int min_height_ = 0;
};

class EqualSpacingLayout : public AbstractLayout {
public:
    enum class Orientation { Horizontal, Vertical };
    EqualSpacingLayout(int w, int h, Orientation orientation)
        : AbstractLayout(0, 0, w, h), orientation_(orientation) {}
    template <typename T> T& add_child(T& element) { return add_child(element, LayoutSettings{}); }
    template <typename T> T& add_child(T& element, const LayoutSettings& settings) {
        children_.push_back({&element, settings});
        return element;
    }
    void arrange_elements() override;
    void visit_children(const std::function<void(LayoutElement&)>& visit) override;

private:
    std::vector<Child> children_;
    Orientation orientation_;
};

class SpacerElement : public LayoutElement {
public:
    SpacerElement(int w, int h) : width_(w), height_(h) {}
    static SpacerElement width(int w) { return SpacerElement(w, 0); }
    static SpacerElement height(int h) { return SpacerElement(0, h); }
    void set_x(int x) override { x_ = x; }
    void set_y(int y) override { y_ = y; }
    int get_x() const override { return x_; }
    int get_y() const override { return y_; }
    int get_width() const override { return width_; }
    int get_height() const override { return height_; }
    void visit_widgets(const std::function<void(Widget&)>&) override {}

private:
    int x_ = 0;
    int y_ = 0;
    int width_;
    int height_;
};

// A header, a footer, both 33 high by default, and the contents between them,
// 30 below the header or as far up as they must be to clear the footer.
class HeaderAndFooterLayout : public LayoutElement {
public:
    static constexpr int kDefaultHeight = 33;
    static constexpr int kMagicPadding = 13;
    HeaderAndFooterLayout(const int& screen_width, const int& screen_height,
                          int header = kDefaultHeight, int footer = kDefaultHeight);

    void set_x(int) override {}
    void set_y(int) override {}
    int get_x() const override { return 0; }
    int get_y() const override { return 0; }
    int get_width() const override { return screen_width_; }
    int get_height() const override { return screen_height_; }
    void visit_widgets(const std::function<void(Widget&)>& visit) override;
    void arrange_elements() override;

    int header_height() const { return header_height_; }
    int footer_height() const { return footer_height_; }
    void set_header_height(int h) { header_height_ = h; }
    void set_footer_height(int h) { footer_height_ = h; }
    int content_height() const { return screen_height_ - header_height_ - footer_height_; }

    template <typename T> T& add_to_header(T& element) { return header_.add_child(element); }
    template <typename T> T& add_to_header(T& element, const LayoutSettings& settings) {
        return header_.add_child(element, settings);
    }
    template <typename T> T& add_to_footer(T& element) { return footer_.add_child(element); }
    template <typename T> T& add_to_footer(T& element, const LayoutSettings& settings) {
        return footer_.add_child(element, settings);
    }
    template <typename T> T& add_to_contents(T& element) { return contents_.add_child(element); }
    template <typename T> T& add_to_contents(T& element, const LayoutSettings& settings) {
        return contents_.add_child(element, settings);
    }

private:
    const int& screen_width_;
    const int& screen_height_;
    int header_height_;
    int footer_height_;
    FrameLayout header_;
    FrameLayout footer_;
    FrameLayout contents_;
};

} // namespace mcpp::ui
