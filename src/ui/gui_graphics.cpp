#include "mcpp/ui/gui_graphics.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace mcpp::ui {

int Font::width(std::string_view legacy) const { return width(flatten_legacy(legacy)); }

int Font::width(const std::vector<Run>& runs) const {
    return static_cast<int>(
        ::mcpp::ui::width(runs, [this](uint32_t code) { return quads_.advance(code); }));
}

int Font::width(const Component& component) const { return width(runs(component)); }

std::vector<std::vector<Run>> Font::split(const Component& component, int max_width) const {
    return wrap(runs(component), static_cast<float>(max_width),
                [this](uint32_t code) { return quads_.advance(code); });
}

std::vector<Run> Font::runs(const Component& component) const {
    return flatten(component, resolver());
}

std::string Font::plain_substr_by_width(std::string_view text, int max_width) const {
    float used = 0.0f;
    size_t at = 0;
    while (at < text.size()) {
        size_t next = at;
        const float step = quads_.advance(next_codepoint(text, next));
        if (used + step > static_cast<float>(max_width)) break;
        used += step;
        at = next;
    }
    return std::string(text.substr(0, at));
}

std::string Font::tail_by_width(std::string_view text, int max_width) const {
    std::vector<size_t> starts;
    for (size_t at = 0; at < text.size();) {
        starts.push_back(at);
        next_codepoint(text, at);
    }
    float used = 0.0f;
    size_t from = text.size();
    for (auto it = starts.rbegin(); it != starts.rend(); ++it) {
        size_t at = *it;
        const float step = quads_.advance(next_codepoint(text, at));
        if (used + step > static_cast<float>(max_width)) break;
        used += step;
        from = *it;
    }
    return std::string(text.substr(from));
}

Resolve Font::resolver() const {
    Resolve resolve;
    if (lang_ != nullptr) {
        const Translations* language = lang_;
        resolve.translate = [language](std::string_view k) { return language->find(k); };
    }
    resolve.key_name = key_name;
    return resolve;
}

std::string Font::tr(std::string_view key) const {
    if (lang_ != nullptr) {
        if (const auto* found = lang_->find(key)) return *found;
    }
    return std::string(key);
}

GuiGraphics::GuiGraphics(gpu::UiRenderer& renderer, int gui_scale, int w, int h,
                         const Translations* language)
    : quads(renderer), font(renderer, language), scale(gui_scale), width(w), height(h),
      lang(language) {}

void GuiGraphics::push_pose() { poses_.push_back(pose_); }

void GuiGraphics::pop_pose() {
    if (poses_.empty()) return;
    pose_ = poses_.back();
    poses_.pop_back();
}

void GuiGraphics::translate(float x, float y) {
    pose_.x += x * pose_.sx;
    pose_.y += y * pose_.sy;
}

void GuiGraphics::scale_by(float x, float y) {
    pose_.sx *= x;
    pose_.sy *= y;
}

void GuiGraphics::fill(int x0, int y0, int x1, int y1, uint32_t argb) {
    if (x1 < x0) std::swap(x0, x1);
    if (y1 < y0) std::swap(y0, y1);
    if ((argb >> 24) == 0 || x0 == x1 || y0 == y1) return;
    quads.fill(px(static_cast<float>(x0)), py(static_cast<float>(y0)),
               pw(static_cast<float>(x1 - x0)), ph(static_cast<float>(y1 - y0)), rgba_of(argb));
}

void GuiGraphics::fill_gradient(int x0, int y0, int x1, int y1, uint32_t top, uint32_t bottom) {
    gpu::Quad quad;
    quad.x = px(static_cast<float>(x0));
    quad.y = py(static_cast<float>(y0));
    quad.width = pw(static_cast<float>(x1 - x0));
    quad.height = ph(static_cast<float>(y1 - y0));
    quad.fill_top = rgba_of(top);
    quad.fill_bottom = rgba_of(bottom);
    quads.add(quad);
}

void GuiGraphics::h_line(int x0, int x1, int y, uint32_t argb) {
    if (x1 < x0) std::swap(x0, x1);
    fill(x0, y, x1 + 1, y + 1, argb);
}

void GuiGraphics::v_line(int x, int y0, int y1, uint32_t argb) {
    if (y1 < y0) std::swap(y0, y1);
    fill(x, y0 + 1, x + 1, y1, argb);
}

void GuiGraphics::render_outline(int x, int y, int w, int h, uint32_t argb) {
    fill(x, y, x + w, y + 1, argb);
    fill(x, y + h - 1, x + w, y + h, argb);
    fill(x, y + 1, x + 1, y + h - 1, argb);
    fill(x + w - 1, y + 1, x + w, y + h - 1, argb);
}

void GuiGraphics::blit_sprite(std::string_view name, int x, int y, int w, int h, uint32_t argb) {
    if (w <= 0 || h <= 0) return;
    quads.nine_slice(name, px(static_cast<float>(x)), py(static_cast<float>(y)),
                     pw(static_cast<float>(w)), ph(static_cast<float>(h)),
                     static_cast<float>(scale) * pose_.sx, rgba_of(argb));
}

void GuiGraphics::blit_sprite(std::string_view name, int sprite_w, int sprite_h, int u, int v,
                              int x, int y, int w, int h, uint32_t argb) {
    if (w <= 0 || h <= 0) return;
    const auto [tw, th] = quads.sprite_size(name);
    if (tw <= 0.0f || sprite_w <= 0 || sprite_h <= 0) return;
    const float kx = tw / static_cast<float>(sprite_w);
    const float ky = th / static_cast<float>(sprite_h);
    quads.sprite_region(name, static_cast<float>(u) * kx, static_cast<float>(v) * ky,
                        static_cast<float>(w) * kx, static_cast<float>(h) * ky,
                        px(static_cast<float>(x)), py(static_cast<float>(y)),
                        pw(static_cast<float>(w)), ph(static_cast<float>(h)), rgba_of(argb));
}

void GuiGraphics::blit(std::string_view texture, int x, int y, float u, float v, int w, int h,
                       int texture_w, int texture_h, uint32_t argb) {
    blit(texture, x, y, u, v, w, h, w, h, texture_w, texture_h, argb);
}

void GuiGraphics::blit(std::string_view texture, int x, int y, float u, float v, int w, int h,
                       int region_w, int region_h, int texture_w, int texture_h, uint32_t argb) {
    if (w <= 0 || h <= 0) return;
    const auto [tw, th] = quads.sprite_size(texture);
    if (tw <= 0.0f || texture_w <= 0 || texture_h <= 0) return;
    const float kx = tw / static_cast<float>(texture_w);
    const float ky = th / static_cast<float>(texture_h);
    quads.sprite_region(texture, u * kx, v * ky, static_cast<float>(region_w) * kx,
                        static_cast<float>(region_h) * ky, px(static_cast<float>(x)),
                        py(static_cast<float>(y)), pw(static_cast<float>(w)),
                        ph(static_cast<float>(h)), rgba_of(argb));
}

void GuiGraphics::blit_tiled(std::string_view name, int x, int y, int w, int h, float tile_w,
                             float tile_h, float u, float v, uint32_t argb) {
    const auto [tw, th] = quads.sprite_size(name);
    if (tw <= 0.0f || th <= 0.0f || w <= 0 || h <= 0) return;
    const float texel_x = tw / tile_w;
    const float texel_y = th / tile_h;
    float phase_y = std::fmod(v, tile_h);
    if (phase_y < 0.0f) phase_y += tile_h;
    float phase_x = std::fmod(u, tile_w);
    if (phase_x < 0.0f) phase_x += tile_w;
    for (float gy = 0.0f; gy < static_cast<float>(h);) {
        const float row_start = std::fmod(phase_y + gy, tile_h);
        const float row_h = std::min(tile_h - row_start, static_cast<float>(h) - gy);
        for (float gx = 0.0f; gx < static_cast<float>(w);) {
            const float col_start = std::fmod(phase_x + gx, tile_w);
            const float col_w = std::min(tile_w - col_start, static_cast<float>(w) - gx);
            quads.sprite_region(name, col_start * texel_x, row_start * texel_y, col_w * texel_x,
                                row_h * texel_y, px(static_cast<float>(x) + gx),
                                py(static_cast<float>(y) + gy), pw(col_w), ph(row_h),
                                rgba_of(argb));
            gx += col_w;
        }
        gy += row_h;
    }
}

int GuiGraphics::draw_string(std::string_view legacy, int x, int y, uint32_t argb, bool shadow) {
    return draw_string(flatten_legacy(legacy), x, y, argb, shadow);
}

int GuiGraphics::draw_string(const std::vector<Run>& runs, int x, int y, uint32_t argb,
                             bool shadow) {
    // The game draws nothing for text with no alpha at all.
    if ((argb >> 24) == 0) return x;
    const float drawn =
        draw(quads, runs, px(static_cast<float>(x)), py(static_cast<float>(y)),
             static_cast<float>(scale) * pose_.sx, rgba_of(argb), shadow,
             static_cast<uint32_t>(seconds * 20.0));
    return x + static_cast<int>(std::lround(drawn / (static_cast<float>(scale) * pose_.sx))) +
           (shadow ? 1 : 0);
}

int GuiGraphics::draw_string(const Component& component, int x, int y, uint32_t argb,
                             bool shadow) {
    return draw_string(font.runs(component), x, y, argb, shadow);
}

void GuiGraphics::draw_centered_string(std::string_view legacy, int cx, int y, uint32_t argb) {
    draw_centered_string(flatten_legacy(legacy), cx, y, argb);
}

void GuiGraphics::draw_centered_string(const Component& component, int cx, int y, uint32_t argb) {
    draw_centered_string(font.runs(component), cx, y, argb);
}

void GuiGraphics::draw_centered_string(const std::vector<Run>& runs, int cx, int y,
                                       uint32_t argb) {
    draw_string(runs, cx - font.width(runs) / 2, y, argb, true);
}

void GuiGraphics::draw_word_wrap(const Component& component, int x, int y, int max_width,
                                 uint32_t argb, bool shadow) {
    for (const auto& line : font.split(component, max_width)) {
        draw_string(line, x, y, argb, shadow);
        y += Font::kLineHeight;
    }
}

void GuiGraphics::draw_scrolling_string(const std::vector<Run>& r, int x0, int y0, int x1, int y1,
                                        uint32_t argb) {
    const int w = font.width(r);
    const int y = (y0 + y1 - Font::kLineHeight) / 2 + 1;
    const int box = x1 - x0;
    if (w <= box) {
        const int cx = std::clamp((x0 + x1) / 2, x0 + w / 2, x1 - w / 2);
        draw_centered_string(r, cx, y, argb);
        return;
    }
    const int overflow = w - box;
    const double period = std::max(static_cast<double>(overflow) * 0.5, 3.0);
    const double phase =
        std::sin(1.5707963267948966 * std::cos(6.283185307179586 * seconds / period)) / 2.0 +
        0.5;
    const double offset = phase * static_cast<double>(overflow);
    enable_scissor(x0, y0, x1, y1);
    draw_string(r, x0 - static_cast<int>(offset), y, argb);
    disable_scissor();
}

void GuiGraphics::render_item(int32_t id, int x, int y) {
    if (id <= 0) return;
    quads.icon(id, px(static_cast<float>(x)), py(static_cast<float>(y)), pw(16.0f));
}

void GuiGraphics::render_item_decorations(int32_t id, int32_t count, int x, int y,
                                          const std::string* text) {
    if (id <= 0 || count <= 0) return;
    if (count != 1 || text != nullptr) {
        const std::string label = text != nullptr ? *text : std::to_string(count);
        const auto runs = flatten_legacy(label);
        draw_string(runs, x + 19 - 2 - font.width(runs), y + 6 + 3, 0xFFFFFFFFu, true);
    }
}

void GuiGraphics::render_item_bar(int x, int y, int bar_width, uint32_t rgb) {
    if (bar_width < 0) return;
    fill(x + 2, y + 13, x + 2 + 13, y + 13 + 2, 0xFF000000u);
    fill(x + 2, y + 13, x + 2 + bar_width, y + 13 + 1, 0xFF000000u | rgb);
}

void GuiGraphics::enable_scissor(int x0, int y0, int x1, int y1) {
    quads.push_scissor(px(static_cast<float>(x0)), py(static_cast<float>(y0)),
                       pw(static_cast<float>(x1 - x0)), ph(static_cast<float>(y1 - y0)));
}

void GuiGraphics::disable_scissor() { quads.pop_scissor(); }

void GuiGraphics::set_tooltip_for_next_frame(std::vector<std::vector<Run>> lines, int mouse_x,
                                             int mouse_y) {
    tooltip_ = std::move(lines);
    tooltip_x_ = mouse_x;
    tooltip_y_ = mouse_y;
    has_tooltip_ = !tooltip_.empty();
}

void GuiGraphics::set_tooltip_for_next_frame(const Component& component, int mouse_x,
                                             int mouse_y) {
    set_tooltip_for_next_frame(font.split(component, std::max(width / 2, 170)), mouse_x, mouse_y);
}

void GuiGraphics::render_tooltip(const std::vector<std::vector<Run>>& lines, int mouse_x,
                                 int mouse_y) {
    if (lines.empty()) return;
    int w = 0;
    int h = lines.size() == 1 ? -2 : 0;
    for (const auto& line : lines) {
        w = std::max(w, font.width(line));
        h += 10;
    }
    int x = mouse_x + 12;
    int y = mouse_y - 12;
    if (x + w > width) x = std::max(x - 24 - w, 4);
    if (y + h + 3 > height) y = height - h - 3;
    blit_sprite("tooltip/background", x - 12, y - 12, w + 24, h + 24);
    blit_sprite("tooltip/frame", x - 12, y - 12, w + 24, h + 24);
    int line_y = y;
    for (size_t i = 0; i < lines.size(); ++i) {
        draw_string(lines[i], x, line_y, 0xFFFFFFFFu, true);
        line_y += 10 + (i == 0 ? 2 : 0);
    }
}

void GuiGraphics::render_deferred() {
    if (!has_tooltip_) return;
    has_tooltip_ = false;
    const Pose saved = pose_;
    pose_ = Pose{};
    render_tooltip(tooltip_, tooltip_x_, tooltip_y_);
    pose_ = saved;
}

} // namespace mcpp::ui
