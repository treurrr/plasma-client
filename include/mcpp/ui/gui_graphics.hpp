#pragma once
#include "mcpp/gpu/ui.hpp"
#include "mcpp/translations.hpp"
#include "mcpp/ui/text_component.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

// The drawing layer every screen and the hud are written against: the game's
// own GuiGraphics calls, in gui units, ARGB colours, with a pose stack, a
// scissor stack and a tooltip that is drawn last. Everything goes into the one
// quad buffer of UiRenderer in the order it is asked for, which is the order
// the game's layering resolves to: something drawn later over something it
// overlaps is on top of it.
namespace mcpp::ui {

// The game writes its colours as ARGB; the renderer takes RGBA.
constexpr uint32_t rgba_of(uint32_t argb) { return (argb << 8) | (argb >> 24); }

// The game's font: widths, splitting and the line height, from the glyphs the
// interface atlas was baked with.
class Font {
public:
    static constexpr int kLineHeight = 9;

    Font(const gpu::UiRenderer& quads, const Translations* lang) : quads_(quads), lang_(lang) {}

    int width(std::string_view legacy) const;
    int width(const std::vector<Run>& runs) const;
    int width(const Component& component) const;
    float advance(uint32_t code) const { return quads_.advance(code); }
    std::vector<std::vector<Run>> split(const Component& component, int max_width) const;
    std::vector<Run> runs(const Component& component) const;
    // The longest head of `text` that fits in `max_width`.
    std::string plain_substr_by_width(std::string_view text, int max_width) const;
    std::string tail_by_width(std::string_view text, int max_width) const;
    Resolve resolver() const;
    std::string tr(std::string_view key) const;

    std::function<std::string(std::string_view)> key_name;

private:
    const gpu::UiRenderer& quads_;
    const Translations* lang_;
};

class GuiGraphics {
public:
    GuiGraphics(gpu::UiRenderer& quads, int scale, int width, int height, const Translations* lang);

    gpu::UiRenderer& quads;
    Font font;
    const int scale;
    const int width;    // gui units
    const int height;
    const Translations* lang;
    // Ticks since the screen opened and how far into the next one this frame is.
    uint32_t ticks = 0;
    float partial = 0.0f;
    double seconds = 0.0;   // wall clock, for what the game animates by time
    // Wall-clock milliseconds, the game's Util.getMillis(), and the ticks of
    // real time since the last frame: what the splash and the panorama run on.
    int64_t millis = 0;
    float realtime_delta_ticks = 0.0f;
    // The framebuffer in pixels, when the host knows it: the gui size times
    // the scale can be a pixel or two larger.
    int pixel_width = 0;
    int pixel_height = 0;
    std::function<std::string(std::string_view)>& key_name = font.key_name;

    int gui_width() const { return width; }
    int gui_height() const { return height; }

    // The pose: a translation and a scale applied to everything drawn after.
    void push_pose();
    void pop_pose();
    void translate(float x, float y);
    void scale_by(float x, float y);
    // The game layers by strata; the buffer here is already in draw order.
    void next_stratum() {}
    // `GuiGraphics.blurBeforeThisStratum`: everything drawn so far is blurred
    // before the rest goes on top.
    void blur_before_this_stratum() { quads.mark_blur(); }

    void fill(int x0, int y0, int x1, int y1, uint32_t argb);
    void fill_gradient(int x0, int y0, int x1, int y1, uint32_t top, uint32_t bottom);
    void h_line(int x0, int x1, int y, uint32_t argb);
    void v_line(int x, int y0, int y1, uint32_t argb);
    void render_outline(int x, int y, int w, int h, uint32_t argb);
    // A sprite from the gui atlas, grown the way its own metadata says:
    // nine slices, or stretched.
    void blit_sprite(std::string_view name, int x, int y, int w, int h, uint32_t argb = 0xFFFFFFFFu);
    // A window of a sprite of `sprite_w` by `sprite_h`, at (u, v) in it.
    void blit_sprite(std::string_view name, int sprite_w, int sprite_h, int u, int v, int x, int y,
                     int w, int h, uint32_t argb = 0xFFFFFFFFu);
    // A window of a whole texture, `texture_w` by `texture_h` texels, drawn
    // from (u, v), `w` by `h` gui units of it.
    void blit(std::string_view texture, int x, int y, float u, float v, int w, int h,
              int texture_w, int texture_h, uint32_t argb = 0xFFFFFFFFu);
    // The same, the region `region_w` by `region_h` texels stretched over w by h.
    void blit(std::string_view texture, int x, int y, float u, float v, int w, int h,
              int region_w, int region_h, int texture_w, int texture_h,
              uint32_t argb = 0xFFFFFFFFu);

    // A texture repeated across the rectangle, `tile` gui units per copy,
    // the pattern starting `u`, `v` gui units in: the menu backgrounds and the
    // header and footer lines, which the game draws with a repeating sampler.
    void blit_tiled(std::string_view texture, int x, int y, int w, int h, float tile_w,
                    float tile_h, float u = 0.0f, float v = 0.0f, uint32_t argb = 0xFFFFFFFFu);

    int draw_string(std::string_view legacy, int x, int y, uint32_t argb, bool shadow = true);
    int draw_string(const std::vector<Run>& runs, int x, int y, uint32_t argb, bool shadow = true);
    int draw_string(const Component& component, int x, int y, uint32_t argb, bool shadow = true);
    void draw_centered_string(std::string_view legacy, int cx, int y, uint32_t argb);
    void draw_centered_string(const Component& component, int cx, int y, uint32_t argb);
    void draw_centered_string(const std::vector<Run>& runs, int cx, int y, uint32_t argb);
    void draw_word_wrap(const Component& component, int x, int y, int max_width, uint32_t argb,
                        bool shadow = true);
    // A label too wide for its box slides back and forth inside it.
    void draw_scrolling_string(const std::vector<Run>& runs, int x0, int y0, int x1, int y1,
                               uint32_t argb);
    // The same, centred on `cx` when it fits rather than on the box.
    void draw_scrolling_string(const std::vector<Run>& runs, int cx, int x0, int y0, int x1,
                               int y1, uint32_t argb);

    void render_item(int32_t id, int x, int y);
    // The count in the corner, or `text` in its place; then the damage bar.
    void render_item_decorations(int32_t id, int32_t count, int x, int y,
                                 const std::string* text = nullptr);
    // 0 to 13 pixels of bar and its colour; nothing when `width` is negative.
    void render_item_bar(int x, int y, int width, uint32_t rgb);

    void enable_scissor(int x0, int y0, int x1, int y1);
    void disable_scissor();

    // The tooltip, drawn by render_deferred after everything else this frame.
    void set_tooltip_for_next_frame(std::vector<std::vector<Run>> lines, int mouse_x,
                                    int mouse_y);
    void set_tooltip_for_next_frame(const Component& component, int mouse_x, int mouse_y);
    void render_tooltip(const std::vector<std::vector<Run>>& lines, int mouse_x, int mouse_y);
    void render_deferred();

    // Kept for the widgets written before the layer took the game's names.
    int width_of(std::string_view legacy) const { return font.width(legacy); }
    int width_of(const std::vector<Run>& runs) const { return font.width(runs); }
    int width_of(const Component& component) const { return font.width(component); }
    std::vector<Run> runs(const Component& component) const { return font.runs(component); }
    std::vector<std::vector<Run>> wrap(const Component& component, int max_width) const {
        return font.split(component, max_width);
    }
    std::string tr(std::string_view key) const { return font.tr(key); }
    Resolve resolver() const { return font.resolver(); }

private:
    struct Pose {
        float x = 0.0f;
        float y = 0.0f;
        float sx = 1.0f;
        float sy = 1.0f;
    };
    float px(float x) const { return (pose_.x + x * pose_.sx) * static_cast<float>(scale); }
    float py(float y) const { return (pose_.y + y * pose_.sy) * static_cast<float>(scale); }
    float pw(float w) const { return w * pose_.sx * static_cast<float>(scale); }
    float ph(float h) const { return h * pose_.sy * static_cast<float>(scale); }

    Pose pose_;
    std::vector<Pose> poses_;
    std::vector<std::vector<Run>> tooltip_;
    int tooltip_x_ = 0;
    int tooltip_y_ = 0;
    bool has_tooltip_ = false;
};

} // namespace mcpp::ui
