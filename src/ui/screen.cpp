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

std::string Widget::narration(const GuiGraphics& gfx) const { return plain(message, gfx.resolver()); }

void Screen::open(ScreenManager& owner, int w, int h) {
    manager = &owner;
    width = w;
    height = h;
    widgets.clear();
    layouts.clear();
    focused_ = nullptr;
    dragging = nullptr;
    init();
}

void Screen::tick() {
    for (auto& widget : widgets) widget->tick();
}

// `Screen.renderWithTooltipAndSubtitles`: background, then the screen, then deferred tooltips.
void Screen::render_with_tooltip(GuiGraphics& gfx, double mx, double my) {
    render_background(gfx, mx, my);
    render(gfx, mx, my);
    gfx.render_deferred();
}

// `Screen.renderTransparentBackground`: the dark in-game gradient.
void Screen::render_transparent_background(GuiGraphics& gfx) {
    gfx.fill_gradient(0, 0, width, height, 0xC0101010u, 0xD0101010u);
}

// `Screen.renderMenuBackground`, `Screen.renderMenuBackgroundTexture`: the tiled menu texture, in-world or not.
void Screen::render_menu_background(GuiGraphics& gfx, int x, int y, int w, int h) {
    const bool world = manager != nullptr && manager->in_world;
    gfx.blit_tiled(world ? "gui/inworld_menu_background" : "gui/menu_background", x, y, w, h,
                   32.0f, 32.0f);
}

// `Screen.renderBackground`: transparent in game, else panorama and menu background.
void Screen::render_background(GuiGraphics& gfx, double, double) {
    if (is_in_game_ui()) {
        render_transparent_background(gfx);
        return;
    }
    if (manager != nullptr && !manager->in_world && manager->panorama) {
        manager->panorama(gfx, true);
    }
    render_menu_background(gfx, 0, 0, width, height);
}

void Screen::render(GuiGraphics& gfx, double mx, double my) {
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
            gfx.set_tooltip_for_next_frame(lines, static_cast<int>(mx), static_cast<int>(my));
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

// `ContainerEventHandler.mouseDragged`: passes a left drag to the dragged widget.
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

// `Screen.keyPressed`: escape closes, then the focused widget, then tab and arrow navigation.
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

// `ContainerEventHandler.charTyped`: typed characters go to the focused widget.
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

bool ScreenManager::button(int which, bool pressed, int mods, double gx, double gy,
                           double now_seconds) {
    mouse_x = gx;
    mouse_y = gy;
    if (pressed) {
        active_button_ = which;
    } else if (active_button_ >= 0) {
        active_button_ = -1;
    }
    Screen* screen = current_.get();
    if (screen == nullptr) return false;
    (void)mods;
    bool taken = false;
    if (pressed) {
        screen->double_click = last_click_ >= 0.0 && now_seconds - last_click_ < 0.25 &&
                               last_click_screen_ == screen && last_click_button_ == which;
        if (screen->mouse_clicked(gx, gy, which)) {
            last_click_ = now_seconds;
            last_click_screen_ = screen;
            last_click_button_ = which;
        }
        taken = true;
    } else {
        screen->mouse_released(gx, gy, which);
        taken = true;
    }
    apply_pending();
    return taken;
}

void ScreenManager::move(double gx, double gy) {
    const double dx = gx - mouse_x;
    const double dy = gy - mouse_y;
    mouse_x = gx;
    mouse_y = gy;
    Screen* screen = current_.get();
    if (screen == nullptr) return;
    screen->mouse_moved(gx, gy);
    if (active_button_ >= 0) screen->mouse_dragged(gx, gy, active_button_, dx, dy);
    apply_pending();
}

bool ScreenManager::scroll(double gx, double gy, double dx, double dy) {
    Screen* screen = current_.get();
    if (screen == nullptr) return false;
    screen->mouse_scrolled(gx, gy, dx, dy);
    apply_pending();
    return true;
}

bool ScreenManager::key(int which, int scancode, int action, int mods) {
    Screen* screen = current_.get();
    if (screen == nullptr) return false;
    KeyEvent event{which, scancode, mods, action == 2};
    bool taken = false;
    if (action == 0) {
        taken = screen->key_released(event);
    } else {
        taken = screen->key_pressed(event);
    }
    apply_pending();
    return taken;
}

bool ScreenManager::character(uint32_t code, int mods) {
    Screen* screen = current_.get();
    if (screen == nullptr) return false;
    const bool taken = screen->char_typed(code, mods);
    apply_pending();
    return taken;
}

void ScreenManager::tick() {
    if (current_) current_->tick();
    apply_pending();
}

// `Window.calculateScale`: the largest GUI scale that keeps 320x240, even for Unicode.
int gui_scale(int framebuffer_width, int framebuffer_height, int option, bool force_unicode) {
    int scale = 1;
    while (scale != option && scale < framebuffer_width && scale < framebuffer_height &&
           framebuffer_width / (scale + 1) >= 320 && framebuffer_height / (scale + 1) >= 240) {
        ++scale;
    }
    if (force_unicode && scale % 2 != 0) ++scale;
    return scale;
}

// `Window.setGuiScale`: scaled size rounded up.
int gui_extent(int pixels, int scale) { return (pixels + scale - 1) / scale; }

} // namespace mcpp::ui
