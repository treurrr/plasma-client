#pragma once
#include "mcpp/gpu/ui.hpp"
#include "mcpp/translations.hpp"
#include "mcpp/ui/text_component.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// The screen spine: a stack of screens drawn in the game's own gui units, each
// owning its widgets, every widget hoverable, focusable and driven by the
// keyboard as well as the mouse. Coordinates are in gui units throughout; the
// one conversion to screen pixels happens in Gfx.
namespace mcpp {
class Options;
class Keybinds;
} // namespace mcpp

namespace mcpp::ui {

// GLFW's key and modifier numbers, which are also the game's.
namespace key {
constexpr int Space = 32, Escape = 256, Enter = 257, Tab = 258, Backspace = 259, Insert = 260,
              Delete = 261, Right = 262, Left = 263, Down = 264, Up = 265, PageUp = 266,
              PageDown = 267, Home = 268, End = 269, KeypadEnter = 335, LeftShift = 340,
              RightShift = 344, A = 65, C = 67, V = 86, X = 88;
constexpr int ModShift = 1, ModControl = 2, ModAlt = 4, ModSuper = 8;
} // namespace key

// The game writes its colours as ARGB; the renderer takes RGBA. Screens speak
// ARGB, so the game's own constants can be read straight across.
constexpr uint32_t rgba_of(uint32_t argb) { return (argb << 8) | (argb >> 24); }

// Click sounds and the like: block F hangs the mixer here. Each call names the
// sound event, "minecraft:ui.button.click".
void set_sound_hook(std::function<void(std::string_view)> hook);
void play_sound(std::string_view event);

// What screens may ask of the platform.
struct Platform {
    std::function<std::string()> get_clipboard;
    std::function<void(const std::string&)> set_clipboard;
};

class Gfx {
public:
    Gfx(gpu::UiRenderer& quads, int scale, int width, int height, const Translations* lang);

    gpu::UiRenderer& quads;
    const int scale;
    const int width;    // gui units
    const int height;
    const Translations* lang;
    std::function<std::string(std::string_view)> key_name;
    // Ticks since the screen opened and how far into the next one this frame is.
    uint32_t ticks = 0;
    float partial = 0.0f;

    // The game's fill: corners, not a size, and ARGB.
    void fill(int x0, int y0, int x1, int y1, uint32_t argb);
    void fill_gradient(int x0, int y0, int x1, int y1, uint32_t top, uint32_t bottom);
    void outline(int x, int y, int w, int h, uint32_t argb);
    void sprite(std::string_view name, int x, int y, int w, int h, uint32_t argb = 0xFFFFFFFFu);
    // Sprites with nine-slice metadata grow by it; the rest stretch.
    void nine(std::string_view name, int x, int y, int w, int h, uint32_t argb = 0xFFFFFFFFu);
    // A window cut out of a sheet, in the sheet's own texels, drawn 1:1.
    void blit(std::string_view sheet, int x, int y, float u, float v, int w, int h,
              uint32_t argb = 0xFFFFFFFFu);
    // A texture repeated across the rectangle at `tile` gui units per copy,
    // starting at texel offset (u, v), the way menu backgrounds are drawn.
    void tiled(std::string_view name, int x, int y, int w, int h, float tile, float u = 0.0f,
               float v = 0.0f, uint32_t argb = 0xFFFFFFFFu);
    void item(int32_t id, int x, int y);
    void item_count(int x, int y, int32_t count);

    int text(std::string_view legacy, int x, int y, uint32_t argb, bool shadow = true);
    int text(const std::vector<Run>& runs, int x, int y, uint32_t argb, bool shadow = true);
    int text(const Component& component, int x, int y, uint32_t argb, bool shadow = true);
    void centred(std::string_view legacy, int cx, int y, uint32_t argb, bool shadow = true);
    void centred(const Component& component, int cx, int y, uint32_t argb, bool shadow = true);
    // Text wider than its box slides back and forth inside it, the way a
    // button's label does in the game.
    void scrolling(const std::vector<Run>& runs, int x0, int y0, int x1, int y1, uint32_t argb);

    int width_of(std::string_view legacy) const;
    int width_of(const std::vector<Run>& runs) const;
    int width_of(const Component& component) const;
    std::vector<std::vector<Run>> wrap(const Component& component, int max_width) const;
    std::vector<Run> runs(const Component& component) const;

    std::string tr(std::string_view key) const;
    Resolve resolver() const;

    void scissor(int x0, int y0, int x1, int y1);
    void end_scissor();

    // The vanilla tooltip, placed beside the pointer and kept on screen.
    void tooltip(const std::vector<std::vector<Run>>& lines, int mouse_x, int mouse_y);

    double seconds = 0.0;   // wall clock, for things the game animates by time
};

struct KeyEvent {
    int key = 0;
    int scancode = 0;
    int mods = 0;
    bool repeat = false;
    bool shift() const { return (mods & key::ModShift) != 0; }
    bool control() const { return (mods & key::ModControl) != 0; }
};

class Screen;

class Widget {
public:
    virtual ~Widget() = default;

    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    bool visible = true;
    bool active = true;
    bool focused = false;
    bool hovered = false;
    Component message;
    float alpha = 1.0f;
    // Shown when the pointer rests on the widget.
    std::vector<Component> tooltip;

    bool over(double mx, double my) const {
        return visible && mx >= x && my >= y && mx < x + w && my < y + h;
    }
    virtual bool focusable() const { return visible && active; }
    // Whether the highlight shows: the pointer is on it, or the keyboard put
    // the focus there.
    bool highlighted() const;

    virtual void render(Gfx& gfx, double mx, double my) = 0;
    virtual void tick() {}
    virtual bool mouse_clicked(double, double, int) { return false; }
    virtual bool mouse_released(double, double, int) { return false; }
    virtual bool mouse_dragged(double, double, int, double, double) { return false; }
    virtual bool mouse_scrolled(double, double, double, double) { return false; }
    virtual bool key_pressed(const KeyEvent&) { return false; }
    virtual bool char_typed(uint32_t, int) { return false; }
    virtual void set_focused(bool on) { focused = on; }
    // What the narrator would say; recorded now so it is not retrofitted later.
    virtual std::string narration(const Gfx& gfx) const;

    static bool keyboard_focus;   // the last focus change came from the keyboard
};

class ScreenManager;

class Screen {
public:
    explicit Screen(Component name) : title(std::move(name)) {}
    virtual ~Screen() = default;

    Component title;
    int width = 0;
    int height = 0;
    ScreenManager* manager = nullptr;
    // Where Escape and Cancel go back to.
    std::shared_ptr<Screen> parent;

    template <typename T, typename... Args> T& add(Args&&... args) {
        auto widget = std::make_unique<T>(std::forward<Args>(args)...);
        T& out = *widget;
        widgets.push_back(std::move(widget));
        return out;
    }

    // Built again whenever the window, and so the gui size, changes.
    void open(ScreenManager& owner, int w, int h);
    virtual void init() {}
    virtual void removed() {}
    virtual void tick();
    virtual void render(Gfx& gfx, double mx, double my);
    virtual void render_background(Gfx& gfx);
    virtual bool mouse_clicked(double mx, double my, int button);
    virtual bool mouse_released(double mx, double my, int button);
    virtual bool mouse_dragged(double mx, double my, int button, double dx, double dy);
    virtual bool mouse_scrolled(double mx, double my, double dx, double dy);
    virtual bool key_pressed(const KeyEvent& event);
    virtual bool key_released(const KeyEvent&) { return false; }
    virtual bool char_typed(uint32_t code, int mods);

    virtual bool should_close_on_esc() const { return true; }
    virtual void on_close();
    // Vanilla pauses a single-player world behind these; on a server nothing
    // pauses, and that is all this client plays.
    virtual bool is_pause_screen() const { return true; }
    // In-world screens over which the world keeps drawing with no blur or
    // darkening: the containers.
    virtual bool draws_world_plain() const { return false; }

    void set_focus(Widget* widget, bool from_keyboard);
    Widget* focus() const { return focused_; }
    bool move_focus(bool forward);
    bool move_focus_spatial(int dx, int dy);

    std::vector<std::unique_ptr<Widget>> widgets;
    Widget* dragging = nullptr;

protected:
    Widget* focused_ = nullptr;
};

// Who is behind the screens: the title flow or a world.
struct Host {
    std::function<void(const std::string& address)> connect;
    std::function<void()> cancel_connect;
    std::function<void()> disconnect;
    std::function<void()> quit;
    std::function<void()> respawn;
    std::function<void(const std::string&)> chat;
    std::function<void(const std::string&)> command;
};

class ServerList;
struct World;

// One screen at a time, swapped at the end of whatever event asked for it so a
// screen never deletes itself from inside its own handler.
class ScreenManager {
public:
    void set_screen(std::shared_ptr<Screen> screen);
    void close() { set_screen(nullptr); }
    Screen* current() const { return current_.get(); }
    std::shared_ptr<Screen> current_shared() const { return current_; }
    void apply_pending();
    void resize(int gui_width, int gui_height);

    int gui_width = 320;
    int gui_height = 240;
    bool in_world = false;
    const Translations* lang = nullptr;
    Options* options = nullptr;
    Keybinds* keybinds = nullptr;
    ServerList* servers = nullptr;
    Platform platform;
    Host host;
    std::function<std::string(std::string_view)> key_name;
    // The panorama behind every screen of the title flow; blurred everywhere
    // but on the title screen itself.
    std::function<void(Gfx&, bool blurred)> panorama;
    // What a world screen needs to know about the world: set while playing.
    World* world = nullptr;

private:
    std::shared_ptr<Screen> current_;
    std::shared_ptr<Screen> pending_;
    bool has_pending_ = false;
};

// The largest whole scale that leaves at least 320 by 240 gui units, capped by
// the guiScale option when it is not 0 (auto), and made even for the unicode
// font, which is how the game picks it.
int gui_scale(int framebuffer_width, int framebuffer_height, int option, bool force_unicode);
// A framebuffer that does not divide evenly gets one more gui unit, so the
// last partial unit is still inside the screen.
int gui_extent(int pixels, int scale);

} // namespace mcpp::ui
