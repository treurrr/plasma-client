#include "mcpp/ui/screen.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace mcpp::ui {
namespace {

std::function<void(std::string_view)>& sound_hook() {
    static std::function<void(std::string_view)> hook;
    return hook;
}

} // namespace

void set_sound_hook(std::function<void(std::string_view)> hook) { sound_hook() = std::move(hook); }

void play_sound(std::string_view event) {
    if (sound_hook()) sound_hook()(event);
}

bool Widget::keyboard_focus = false;

bool Widget::highlighted() const { return hovered || (focused && keyboard_focus); }

std::string Widget::narration(const Gfx& gfx) const { return plain(message, gfx.resolver()); }

Gfx::Gfx(gpu::UiRenderer& renderer, int gui_scale_, int w, int h, const Translations* language)
    : quads(renderer), scale(gui_scale_), width(w), height(h), lang(language) {}

void Gfx::fill(int x0, int y0, int x1, int y1, uint32_t argb) {
    if (x1 < x0) std::swap(x0, x1);
    if (y1 < y0) std::swap(y0, y1);
    if ((argb >> 24) == 0) return;
    quads.fill(static_cast<float>(x0 * scale), static_cast<float>(y0 * scale),
               static_cast<float>((x1 - x0) * scale), static_cast<float>((y1 - y0) * scale),
               rgba_of(argb));
}

void Gfx::fill_gradient(int x0, int y0, int x1, int y1, uint32_t top, uint32_t bottom) {
    gpu::Quad quad;
    quad.x = static_cast<float>(x0 * scale);
    quad.y = static_cast<float>(y0 * scale);
    quad.width = static_cast<float>((x1 - x0) * scale);
    quad.height = static_cast<float>((y1 - y0) * scale);
    quad.fill_top = rgba_of(top);
    quad.fill_bottom = rgba_of(bottom);
    quads.add(quad);
}

void Gfx::outline(int x, int y, int w, int h, uint32_t argb) {
    fill(x, y, x + w, y + 1, argb);
    fill(x, y + h - 1, x + w, y + h, argb);
    fill(x, y + 1, x + 1, y + h - 1, argb);
    fill(x + w - 1, y + 1, x + w, y + h - 1, argb);
}

void Gfx::sprite(std::string_view name, int x, int y, int w, int h, uint32_t argb) {
    quads.sprite(name, static_cast<float>(x * scale), static_cast<float>(y * scale),
                 static_cast<float>(w * scale), static_cast<float>(h * scale), rgba_of(argb));
}

void Gfx::nine(std::string_view name, int x, int y, int w, int h, uint32_t argb) {
    quads.nine_slice(name, static_cast<float>(x * scale), static_cast<float>(y * scale),
                     static_cast<float>(w * scale), static_cast<float>(h * scale),
                     static_cast<float>(scale), rgba_of(argb));
}

void Gfx::blit(std::string_view sheet, int x, int y, float u, float v, int w, int h,
               uint32_t argb) {
    quads.sprite_region(sheet, u, v, static_cast<float>(w), static_cast<float>(h),
                        static_cast<float>(x * scale), static_cast<float>(y * scale),
                        static_cast<float>(w * scale), static_cast<float>(h * scale),
                        rgba_of(argb));
}

void Gfx::tiled(std::string_view name, int x, int y, int w, int h, float tile, float u, float v,
                uint32_t argb) {
    const auto [tw, th] = quads.sprite_size(name);
    if (tw <= 0.0f || th <= 0.0f || w <= 0 || h <= 0) return;
    // `tile` gui units cover the whole texture, and (u, v) is where in gui
    // units the pattern starts, so a scrolled list drags its background along.
    const float texel_x = tw / tile;
    const float texel_y = th / tile;
    float phase_y = std::fmod(v, tile);
    if (phase_y < 0.0f) phase_y += tile;
    for (float gy = 0.0f; gy < static_cast<float>(h);) {
        const float row_start = std::fmod(phase_y + gy, tile);
        const float row_h = std::min(tile - row_start, static_cast<float>(h) - gy);
        float phase_x = std::fmod(u, tile);
        if (phase_x < 0.0f) phase_x += tile;
        for (float gx = 0.0f; gx < static_cast<float>(w);) {
            const float col_start = std::fmod(phase_x + gx, tile);
            const float col_w = std::min(tile - col_start, static_cast<float>(w) - gx);
            quads.sprite_region(name, col_start * texel_x, row_start * texel_y, col_w * texel_x,
                                row_h * texel_y, (static_cast<float>(x) + gx) * scale,
                                (static_cast<float>(y) + gy) * scale, col_w * scale,
                                row_h * scale, rgba_of(argb));
            gx += col_w;
        }
        gy += row_h;
    }
}

void Gfx::item(int32_t id, int x, int y) {
    if (id <= 0) return;
    quads.icon(id, static_cast<float>(x * scale), static_cast<float>(y * scale),
               static_cast<float>(16 * scale));
}

void Gfx::item_count(int x, int y, int32_t count) {
    if (count <= 1) return;
    const std::string label = std::to_string(count);
    text(label, x + 19 - 2 - width_of(label), y + 6 + 3, 0xFFFFFFFFu, true);
}

int Gfx::text(std::string_view legacy, int x, int y, uint32_t argb, bool shadow) {
    return text(flatten_legacy(legacy), x, y, argb, shadow);
}

int Gfx::text(const std::vector<Run>& runs, int x, int y, uint32_t argb, bool shadow) {
    if ((argb >> 24) == 0) return 0;
    const float drawn = draw(quads, runs, static_cast<float>(x * scale),
                             static_cast<float>(y * scale), static_cast<float>(scale),
                             rgba_of(argb), shadow, static_cast<uint32_t>(seconds * 20.0));
    return static_cast<int>(drawn / static_cast<float>(scale));
}

int Gfx::text(const Component& component, int x, int y, uint32_t argb, bool shadow) {
    return text(runs(component), x, y, argb, shadow);
}

void Gfx::centred(std::string_view legacy, int cx, int y, uint32_t argb, bool shadow) {
    text(legacy, cx - width_of(legacy) / 2, y, argb, shadow);
}

void Gfx::centred(const Component& component, int cx, int y, uint32_t argb, bool shadow) {
    const auto r = runs(component);
    text(r, cx - width_of(r) / 2, y, argb, shadow);
}

void Gfx::scrolling(const std::vector<Run>& r, int x0, int y0, int x1, int y1, uint32_t argb) {
    const int w = width_of(r);
    const int y = (y0 + y1 - 9) / 2 + 1;
    const int box = x1 - x0;
    if (w <= box) {
        text(r, (x0 + x1) / 2 - w / 2, y, argb);
        return;
    }
    // Back and forth over the overflow, a second per few pixels, pausing at
    // each end: the game's rhythm for a label that does not fit.
    const int overflow = w - box;
    const double period = std::max(static_cast<double>(overflow) * 0.5, 3.0);
    const double phase = std::sin(1.5707963267948966 * std::cos(6.283185307179586 * seconds /
                                                                period)) /
                             2.0 +
                         0.5;
    const int offset = static_cast<int>(std::lerp(0.0, static_cast<double>(overflow), phase));
    scissor(x0, y0, x1, y1);
    text(r, x0 - offset, y, argb);
    end_scissor();
}

int Gfx::width_of(std::string_view legacy) const { return width_of(flatten_legacy(legacy)); }

int Gfx::width_of(const std::vector<Run>& r) const {
    return static_cast<int>(
        ::mcpp::ui::width(r, [this](uint32_t code) { return quads.advance(code); }));
}

int Gfx::width_of(const Component& component) const { return width_of(runs(component)); }

std::vector<std::vector<Run>> Gfx::wrap(const Component& component, int max_width) const {
    return ::mcpp::ui::wrap(runs(component), static_cast<float>(max_width),
                            [this](uint32_t code) { return quads.advance(code); });
}

std::vector<Run> Gfx::runs(const Component& component) const {
    return flatten(component, resolver());
}

std::string Gfx::tr(std::string_view key) const {
    if (lang != nullptr) {
        if (const auto* found = lang->find(key)) return *found;
    }
    return std::string(key);
}

Resolve Gfx::resolver() const {
    Resolve resolve;
    if (lang != nullptr) {
        const Translations* language = lang;
        resolve.translate = [language](std::string_view k) { return language->find(k); };
    }
    resolve.key_name = key_name;
    return resolve;
}

void Gfx::scissor(int x0, int y0, int x1, int y1) {
    quads.push_scissor(static_cast<float>(x0 * scale), static_cast<float>(y0 * scale),
                       static_cast<float>((x1 - x0) * scale), static_cast<float>((y1 - y0) * scale));
}

void Gfx::end_scissor() { quads.pop_scissor(); }

void Gfx::tooltip(const std::vector<std::vector<Run>>& lines, int mouse_x, int mouse_y) {
    if (lines.empty()) return;
    int w = 0;
    for (const auto& line : lines) w = std::max(w, width_of(line));
    const int h = lines.size() == 1 ? 8 : static_cast<int>(lines.size()) * 10;
    int x = mouse_x + 12;
    int y = mouse_y - 12;
    if (x + w > width) x = std::max(mouse_x - 24 - w, 4);
    if (y + h + 3 > height) y = height - h - 3;
    y = std::max(y, 4);
    nine("tooltip/background", x - 12, y - 12, w + 24, h + 24);
    nine("tooltip/frame", x - 12, y - 12, w + 24, h + 24);
    int line_y = y;
    for (size_t i = 0; i < lines.size(); ++i) {
        text(lines[i], x, line_y, 0xFFFFFFFFu, true);
        line_y += 10 + (i == 0 ? 2 : 0);
    }
}

void Screen::open(ScreenManager& owner, int w, int h) {
    manager = &owner;
    width = w;
    height = h;
    widgets.clear();
    focused_ = nullptr;
    dragging = nullptr;
    init();
}

void Screen::tick() {
    for (auto& widget : widgets) widget->tick();
}

void Screen::render_background(Gfx& gfx) {
    if (manager != nullptr && manager->in_world) {
        gfx.tiled("gui/inworld_menu_background", 0, 0, width, height, 32.0f);
    } else {
        if (manager != nullptr && manager->panorama) manager->panorama(gfx, true);
        gfx.tiled("gui/menu_background", 0, 0, width, height, 32.0f);
    }
}

void Screen::render(Gfx& gfx, double mx, double my) {
    render_background(gfx);
    for (auto& widget : widgets) {
        widget->hovered = widget->over(mx, my);
        if (widget->visible) widget->render(gfx, mx, my);
    }
    for (auto& widget : widgets) {
        if (widget->hovered && !widget->tooltip.empty()) {
            std::vector<std::vector<Run>> lines;
            for (const auto& line : widget->tooltip) {
                for (auto& wrapped : gfx.wrap(line, 170)) lines.push_back(std::move(wrapped));
            }
            gfx.tooltip(lines, static_cast<int>(mx), static_cast<int>(my));
            break;
        }
    }
}

bool Screen::mouse_clicked(double mx, double my, int button) {
    for (auto& widget : widgets) {
        if (!widget->visible) continue;
        if (widget->mouse_clicked(mx, my, button)) {
            set_focus(widget.get(), false);
            if (button == 0) dragging = widget.get();
            return true;
        }
    }
    set_focus(nullptr, false);
    return false;
}

bool Screen::mouse_released(double mx, double my, int button) {
    Widget* was = dragging;
    if (button == 0) dragging = nullptr;
    if (was != nullptr) return was->mouse_released(mx, my, button);
    return false;
}

bool Screen::mouse_dragged(double mx, double my, int button, double dx, double dy) {
    if (dragging != nullptr && button == 0) return dragging->mouse_dragged(mx, my, button, dx, dy);
    return false;
}

bool Screen::mouse_scrolled(double mx, double my, double dx, double dy) {
    for (auto& widget : widgets) {
        if (widget->visible && widget->over(mx, my) && widget->mouse_scrolled(mx, my, dx, dy)) {
            return true;
        }
    }
    return false;
}

bool Screen::key_pressed(const KeyEvent& event) {
    if (event.key == key::Escape && should_close_on_esc()) {
        on_close();
        return true;
    }
    if (focused_ != nullptr && focused_->key_pressed(event)) return true;
    if (event.key == key::Tab) return move_focus(!event.shift());
    if (event.key == key::Up) return move_focus_spatial(0, -1);
    if (event.key == key::Down) return move_focus_spatial(0, 1);
    if (event.key == key::Left) return move_focus_spatial(-1, 0);
    if (event.key == key::Right) return move_focus_spatial(1, 0);
    return false;
}

bool Screen::char_typed(uint32_t code, int mods) {
    return focused_ != nullptr && focused_->char_typed(code, mods);
}

void Screen::on_close() {
    if (manager != nullptr) manager->set_screen(parent);
}

void Screen::set_focus(Widget* widget, bool from_keyboard) {
    if (focused_ == widget) {
        if (widget != nullptr) Widget::keyboard_focus = from_keyboard;
        return;
    }
    if (focused_ != nullptr) focused_->set_focused(false);
    focused_ = widget;
    Widget::keyboard_focus = from_keyboard;
    if (focused_ != nullptr) focused_->set_focused(true);
}

bool Screen::move_focus(bool forward) {
    std::vector<Widget*> order;
    for (auto& widget : widgets) {
        if (widget->focusable()) order.push_back(widget.get());
    }
    if (order.empty()) return false;
    auto at = std::find(order.begin(), order.end(), focused_);
    size_t index = 0;
    if (at == order.end()) {
        index = forward ? 0 : order.size() - 1;
    } else {
        const auto here = static_cast<size_t>(at - order.begin());
        index = forward ? (here + 1) % order.size() : (here + order.size() - 1) % order.size();
    }
    set_focus(order[index], true);
    return true;
}

bool Screen::move_focus_spatial(int dx, int dy) {
    if (focused_ == nullptr) return move_focus(true);
    const double cx = focused_->x + focused_->w / 2.0;
    const double cy = focused_->y + focused_->h / 2.0;
    Widget* best = nullptr;
    double best_distance = 1e18;
    for (auto& widget : widgets) {
        if (!widget->focusable() || widget.get() == focused_) continue;
        const double wx = widget->x + widget->w / 2.0;
        const double wy = widget->y + widget->h / 2.0;
        const double along = (wx - cx) * dx + (wy - cy) * dy;
        if (along <= 0.0) continue;
        const double across = std::abs((wx - cx) * dy) + std::abs((wy - cy) * dx);
        const double distance = along + across * 2.0;
        if (distance < best_distance) {
            best_distance = distance;
            best = widget.get();
        }
    }
    if (best == nullptr) return false;
    set_focus(best, true);
    return true;
}

void ScreenManager::set_screen(std::shared_ptr<Screen> screen) {
    pending_ = std::move(screen);
    has_pending_ = true;
}

void ScreenManager::apply_pending() {
    if (!has_pending_) return;
    has_pending_ = false;
    auto next = std::move(pending_);
    pending_.reset();
    if (current_ && current_ != next) current_->removed();
    current_ = std::move(next);
    if (current_) current_->open(*this, gui_width, gui_height);
}

void ScreenManager::resize(int w, int h) {
    if (w == gui_width && h == gui_height) return;
    gui_width = w;
    gui_height = h;
    if (current_) current_->open(*this, w, h);
}

int gui_scale(int framebuffer_width, int framebuffer_height, int option, bool force_unicode) {
    int scale = 1;
    while (scale != option && scale < framebuffer_width && scale < framebuffer_height &&
           framebuffer_width / (scale + 1) >= 320 && framebuffer_height / (scale + 1) >= 240) {
        ++scale;
    }
    if (force_unicode && scale % 2 != 0) ++scale;
    return scale;
}

int gui_extent(int pixels, int scale) { return (pixels + scale - 1) / scale; }

} // namespace mcpp::ui
