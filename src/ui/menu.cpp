#include "mcpp/ui/menu.hpp"

#include "mcpp/features.hpp"

#include <format>
#include <string>
#include <string_view>

namespace mcpp::ui {
namespace {

using gpu::Icon;
using gpu::Quad;
using gpu::Style;

constexpr float kDesignWidth = 1600.0f;
constexpr float kDesignHeight = 900.0f;
constexpr float kLeft = 107.0f;
constexpr float kRight = 1493.0f;

constexpr uint32_t kScrim      = 0x06060899u;
constexpr uint32_t kPage       = 0x0A0A0BFFu;
constexpr uint32_t kCard       = 0x121214FFu;
constexpr uint32_t kCardEdge   = 0x232327FFu;
constexpr uint32_t kSunken     = 0x08080AFFu;
constexpr uint32_t kChip       = 0x141417FFu;
constexpr uint32_t kChipEdge   = 0x26262BFFu;
constexpr uint32_t kRule       = 0x1C1C20FFu;
constexpr uint32_t kText       = 0xECEDEFFFu;
constexpr uint32_t kDim        = 0x9BA0A6FFu;
constexpr uint32_t kMute       = 0x6B7076FFu;
constexpr uint32_t kFaint      = 0x4D5157FFu;
constexpr uint32_t kAccent     = 0x4A32F0FFu;
constexpr uint32_t kAccentLit  = 0x6B4CFFFFu;
constexpr uint32_t kAccentCore = 0xE055F0FFu;
constexpr uint32_t kAccentGlow = 0x4A32F055u;
constexpr uint32_t kOnFill     = 0x15361FFFu;
constexpr uint32_t kOnEdge     = 0x2C6B45FFu;
constexpr uint32_t kOnText     = 0x79E0A0FFu;
constexpr uint32_t kOffFill    = 0x1A1A1DFFu;
constexpr uint32_t kOffEdge    = 0x2A2A2FFFu;
constexpr uint32_t kHeartOff   = 0x3A3A40FFu;

// Latin text sits with its optical centre a little under the ascender line;
// this is the offset that puts a line of `size` pixels in the middle of a box.
constexpr float kMiddle = 0.734f;

struct Painter {
    gpu::UiRenderer& quads;
    gpu::TextRenderer& text;
    float scale = 1.0f;
    float origin_x = 0.0f;
    float origin_y = 0.0f;

    float px(float value) const { return value * scale; }
    float sx(float value) const { return origin_x + value * scale; }
    float sy(float value) const { return origin_y + value * scale; }

    void rect(float x, float y, float w, float h, float radius, uint32_t fill,
              uint32_t edge = 0, float border = 0.0f) const {
        quads.panel(sx(x), sy(y), px(w), px(h), px(radius), fill, edge,
                    border > 0.0f ? px(border) : 0.0f);
    }

    void glow(float x, float y, float w, float h, float radius, uint32_t fill, float spread,
              uint32_t colour) const {
        Quad quad;
        quad.x = sx(x);
        quad.y = sy(y);
        quad.width = px(w);
        quad.height = px(h);
        quad.radius = px(radius);
        quad.glow = px(spread);
        quad.fill_top = fill;
        quad.fill_bottom = fill;
        quad.glow_rgba = colour;
        quads.add(quad);
    }

    float measure(std::string_view line, float size, Style style = Style::Sans) const {
        return text.width(line, text.scale_for(size), style);
    }

    void label(std::string_view line, float x, float y, float size, uint32_t colour,
               Style style = Style::Sans) const {
        text.draw(line, sx(x), sy(y), text.scale_for(px(size)), colour, style);
    }

    void middle(std::string_view line, float x, float centre_y, float size, uint32_t colour,
                Style style = Style::Sans) const {
        label(line, x, centre_y - size * kMiddle, size, colour, style);
    }

    void right(std::string_view line, float edge_x, float centre_y, float size, uint32_t colour,
               Style style = Style::Sans) const {
        middle(line, edge_x - measure(line, size, style), centre_y, size, colour, style);
    }

    void centred(std::string_view line, float centre_x, float centre_y, float size,
                 uint32_t colour, Style style = Style::Sans) const {
        middle(line, centre_x - measure(line, size, style) * 0.5f, centre_y, size, colour, style);
    }

    void icon(Icon which, float x, float centre_y, float size, uint32_t colour) const {
        text.icon(which, sx(x), sy(centre_y - size * 0.5f), px(size), colour);
    }

    float icon_width(Icon which, float size) const { return text.icon_width(which, size); }

    void icon_centred(Icon which, float centre_x, float centre_y, float size,
                      uint32_t colour) const {
        icon(which, centre_x - icon_width(which, size) * 0.5f, centre_y, size, colour);
    }

    // Letter-spaced small caps, which no font gives us for free.
    void tracked(std::string_view line, float x, float centre_y, float size, uint32_t colour,
                 float tracking) const {
        for (const char ch : line) {
            const std::string_view one(&ch, 1);
            middle(one, x, centre_y, size, colour, Style::Mono);
            x += measure(one, size, Style::Mono) + tracking;
        }
    }

    float tracked_width(std::string_view line, float size, float tracking) const {
        float out = 0.0f;
        for (const char ch : line) out += measure(std::string_view(&ch, 1), size, Style::Mono) +
                                         tracking;
        return out > 0.0f ? out - tracking : 0.0f;
    }
};

void key_cap(const Painter& paint, std::string_view name, float x, float y, float w, float h) {
    paint.rect(x, y, w, h, 3.0f, kChip, kChipEdge, 1.0f);
    paint.centred(name, x + w * 0.5f, y + h * 0.5f, 8.5f, kDim, Style::Mono);
}

void preview(const Painter& paint, size_t index, float x, float y, float w,
             const MenuState& state) {
    const float cx = x + w * 0.5f;
    const float cy = y + 50.0f;

    const auto number = [&](std::string_view value, std::string_view unit, float box_w) {
        paint.rect(cx - box_w * 0.5f, cy - 23.0f, box_w, 46.0f, 6.0f, kSunken, 0x1F1F23FFu, 1.0f);
        const float value_w = paint.measure(value, 24.0f, Style::Mono);
        const float unit_w = paint.measure(unit, 9.0f, Style::Mono);
        const float total = value_w + 6.0f + unit_w;
        paint.middle(value, cx - total * 0.5f, cy, 24.0f, kText, Style::Mono);
        paint.middle(unit, cx - total * 0.5f + value_w + 6.0f, cy + 7.0f, 9.0f, kMute,
                     Style::Mono);
    };

    switch (index) {
    case 0:
        number(std::format("{}", state.fps > 0 ? state.fps : 486), "FPS", 110.0f);
        break;
    case 1: {
        const std::string left = std::format("{}", state.cps_left);
        const std::string right = std::format("{}", state.cps_right);
        paint.rect(cx - 55.0f, cy - 23.0f, 110.0f, 46.0f, 6.0f, kSunken, 0x1F1F23FFu, 1.0f);
        paint.middle(left, cx - 42.0f, cy, 24.0f, kText, Style::Mono);
        paint.middle("|", cx - 4.0f, cy, 20.0f, 0x34343AFFu, Style::Mono);
        paint.middle(right, cx + 10.0f, cy, 24.0f, kText, Style::Mono);
        paint.middle("CPS", cx + 30.0f, cy + 7.0f, 9.0f, kMute, Style::Mono);
        break;
    }
    case 2: {
        const std::string value = std::format("{}", state.ping_ms > 0 ? state.ping_ms : 24);
        paint.icon(Icon::Signal, cx - 48.0f, cy, 17.0f, kDim);
        paint.middle(value, cx - 18.0f, cy, 24.0f, kText, Style::Mono);
        paint.middle("ms", cx + 16.0f, cy + 7.0f, 9.0f, kMute, Style::Mono);
        break;
    }
    case 3:
        key_cap(paint, "W", cx - 11.0f, cy - 30.0f, 22.0f, 17.0f);
        key_cap(paint, "A", cx - 35.0f, cy - 11.0f, 22.0f, 17.0f);
        key_cap(paint, "S", cx - 11.0f, cy - 11.0f, 22.0f, 17.0f);
        key_cap(paint, "D", cx + 13.0f, cy - 11.0f, 22.0f, 17.0f);
        key_cap(paint, "SPACE", cx - 35.0f, cy + 8.0f, 70.0f, 13.0f);
        break;
    case 4: {
        constexpr Icon pieces[4] = {Icon::Helmet, Icon::Shirt, Icon::Socks, Icon::Boot};
        constexpr float fraction[4] = {0.9f, 0.55f, 0.75f, 0.3f};
        for (int i = 0; i < 4; ++i) {
            const float px_ = cx - 72.0f + static_cast<float>(i) * 48.0f;
            paint.icon_centred(pieces[i], px_, cy - 6.0f, 20.0f, kDim);
            paint.rect(px_ - 16.0f, cy + 12.0f, 32.0f, 2.0f, 1.0f, 0x26262BFFu);
            paint.rect(px_ - 16.0f, cy + 12.0f, 32.0f * fraction[i], 2.0f, 1.0f, kText);
        }
        break;
    }
    case 5:
        paint.icon(Icon::Flask, cx - 62.0f, cy, 22.0f, kDim);
        paint.middle("Speed II", cx - 30.0f, cy - 7.0f, 12.0f, kText, Style::Bold);
        paint.middle("01:24 remaining", cx - 30.0f, cy + 9.0f, 9.5f, kMute, Style::Mono);
        break;
    case 6: {
        const std::string rows[3] = {std::format("X {:8.1f}", state.x),
                                     std::format("Y {:8.1f}", state.y),
                                     std::format("Z {:8.1f}", state.z)};
        for (int i = 0; i < 3; ++i) {
            paint.middle(rows[static_cast<size_t>(i)], cx - 62.0f,
                         cy - 16.0f + static_cast<float>(i) * 16.0f, 11.0f, kDim, Style::Mono);
        }
        paint.middle("NW", cx + 34.0f, cy - 13.0f, 8.0f, kMute, Style::Mono);
        paint.middle(std::format("{:.0f}", state.yaw), cx + 44.0f, cy + 2.0f, 9.0f, kFaint,
                     Style::Mono);
        break;
    }
    case 7:
        paint.icon(Icon::Running, cx - 66.0f, cy, 24.0f, kDim);
        paint.middle("Sprinting (toggled)", cx - 38.0f, cy - 9.0f, 11.0f, kDim);
        key_cap(paint, "L CTRL", cx - 38.0f, cy + 5.0f, 46.0f, 14.0f);
        break;
    case 8:
        paint.icon(Icon::Sneaking, cx - 60.0f, cy, 24.0f, kDim);
        paint.middle("Sneak (hold)", cx - 34.0f, cy - 9.0f, 11.0f, kDim);
        key_cap(paint, "L SHIFT", cx - 34.0f, cy + 5.0f, 50.0f, 14.0f);
        break;
    case 9: {
        const float frame_x = cx - 42.0f;
        const float half = 25.0f;
        const float arm = 11.0f;
        const uint32_t edge = 0x53535AFFu;
        for (int corner = 0; corner < 4; ++corner) {
            const float left = (corner % 2 == 0) ? -half : half - arm;
            const float top = (corner < 2) ? -half : half - 2.0f;
            paint.rect(frame_x + left, cy + top, arm, 2.0f, 1.0f, edge);
            paint.rect(frame_x + left + ((corner % 2 == 0) ? 0.0f : arm - 2.0f),
                       cy + ((corner < 2) ? -half : half - arm), 2.0f, arm, 1.0f, edge);
        }
        paint.centred("+", frame_x, cy, 17.0f, kDim, Style::Mono);
        paint.middle("4x", cx + 22.0f, cy, 12.0f, kDim, Style::Mono);
        break;
    }
    case 10:
        paint.icon_centred(Icon::Cube, cx, cy - 6.0f, 34.0f, 0x2E2E34FFu);
        paint.centred("Ranked policy - outlines forbidden", cx, cy + 22.0f, 9.0f, kMute);
        break;
    case 11: {
        constexpr float heights[6] = {10.0f, 16.0f, 22.0f, 28.0f, 34.0f, 40.0f};
        for (int i = 0; i < 6; ++i) {
            const float bar_h = heights[i];
            const uint32_t tone = 0x26262BFFu + static_cast<uint32_t>(i) * 0x16161600u;
            paint.rect(cx - 60.0f + static_cast<float>(i) * 20.0f, cy + 12.0f - bar_h, 15.0f,
                       bar_h, 2.0f, i >= 4 ? 0xD8D9DCFFu : tone);
        }
        paint.centred("GAMMA 100%", cx, cy + 26.0f, 9.0f, kMute, Style::Mono);
        break;
    }
    case 12: {
        const uint32_t edge = 0x4A4A50FFu;
        paint.rect(cx - 36.0f, cy - 16.0f, 2.0f, 32.0f, 1.0f, edge);
        paint.rect(cx + 34.0f, cy - 16.0f, 2.0f, 32.0f, 1.0f, edge);
        paint.rect(cx - 36.0f, cy - 16.0f, 10.0f, 2.0f, 1.0f, edge);
        paint.rect(cx - 36.0f, cy + 14.0f, 10.0f, 2.0f, 1.0f, edge);
        paint.rect(cx + 26.0f, cy - 16.0f, 10.0f, 2.0f, 1.0f, edge);
        paint.rect(cx + 26.0f, cy + 14.0f, 10.0f, 2.0f, 1.0f, edge);
        paint.centred("|", cx, cy, 20.0f, kDim, Style::Mono);
        paint.centred("STEADY ON HIT", cx, cy + 28.0f, 9.0f, kMute, Style::Mono);
        break;
    }
    case 13:
        paint.icon_centred(Icon::Fire, cx, cy - 4.0f, 30.0f, 0x2E2E34FFu);
        paint.centred("CLEAR VIEW", cx, cy + 24.0f, 9.0f, kMute, Style::Mono);
        break;
    default: {
        paint.centred("BED WARS", cx, cy - 22.0f, 9.0f, kDim, Style::Bold);
        const char* names[4] = {"Diamond II", "Kills", "Final kills", "Beds broken"};
        const char* values[4] = {"02:46", "3", "1", "2"};
        for (int i = 0; i < 4; ++i) {
            const float row = cy - 6.0f + static_cast<float>(i) * 13.0f;
            paint.middle(names[i], cx - 62.0f, row, 9.0f, kMute, Style::Mono);
            paint.right(values[i], cx + 62.0f, row, 9.0f, kDim, Style::Mono);
        }
        break;
    }
    }
}

} // namespace

void draw_hud(gpu::UiRenderer& quads, gpu::TextRenderer& text, float width, float height,
              const HudState& state) {
    Painter paint{quads, text};
    paint.scale = std::min(width / kDesignWidth, height / kDesignHeight);
    paint.origin_x = (width - kDesignWidth * paint.scale) * 0.5f;
    paint.origin_y = (height - kDesignHeight * paint.scale) * 0.5f;

    const auto& registry = features::Registry::instance();
    // Readable over a bright sky is the whole job here, so the glass is dark
    // and the labels are not whispered.
    constexpr uint32_t kGlass = 0x08080AEEu;
    constexpr uint32_t kGlassEdge = 0x3C3C45CCu;

    float stack = 24.0f;
    bool first = true;
    const auto readout = [&](std::string_view value, std::string_view unit) {
        paint.rect(24.0f, stack, 152.0f, 38.0f, 10.0f, kGlass, kGlassEdge, 1.0f);
        if (first) {
            paint.icon(Icon::MarkBody, 36.0f, stack + 19.0f, 18.0f, kAccent);
            paint.icon(Icon::MarkCore, 36.0f, stack + 19.0f, 18.0f, kAccentCore);
            first = false;
        }
        paint.middle(value, 60.0f, stack + 19.0f, 20.0f, kText, Style::Mono);
        paint.right(unit, 164.0f, stack + 20.0f, 9.5f, kDim, Style::Mono);
        stack += 44.0f;
    };

    if (registry.enabled(features::Id::FpsCounter)) {
        readout(std::format("{}", state.fps), "FPS");
    }
    if (registry.enabled(features::Id::PingDisplay)) {
        readout(state.ping_ms > 0 ? std::format("{}", state.ping_ms) : "--", "MS");
    }
    if (registry.enabled(features::Id::CpsCounter)) {
        readout(std::format("{} | {}", state.cps_left, state.cps_right), "CPS");
    }

    // The frame cost, which is the claim this client is making, said in the
    // two halves that actually make it up.
    paint.rect(24.0f, stack, 152.0f, 28.0f, 9.0f, kGlass, kGlassEdge, 1.0f);
    paint.middle(std::format("{:.2f}", state.gpu_ms), 38.0f, stack + 14.0f, 11.0f, kDim,
                 Style::Mono);
    paint.middle("gpu", 38.0f + paint.measure("0.00 ", 11.0f, Style::Mono), stack + 14.5f, 9.0f,
                 kMute, Style::Mono);
    paint.right("cpu", 164.0f, stack + 14.5f, 9.0f, kMute, Style::Mono);
    paint.right(std::format("{:.2f}", state.cpu_ms),
                164.0f - paint.measure("cpu ", 9.0f, Style::Mono), stack + 14.0f, 11.0f, kDim,
                Style::Mono);

    if (registry.enabled(features::Id::Coordinates)) {
        const std::string line = std::format("{:.1f}  {:.1f}  {:.1f}", state.x, state.y, state.z);
        const float w = paint.measure(line, 13.0f, Style::Mono) + 30.0f;
        paint.rect(kDesignWidth - 24.0f - w, 24.0f, w, 34.0f, 10.0f, kGlass, kGlassEdge, 1.0f);
        paint.middle(line, kDesignWidth - 24.0f - w + 15.0f, 41.0f, 13.0f, kText, Style::Mono);
    }

    if (registry.enabled(features::Id::Keystrokes)) {
        const float base_x = 28.0f;
        const float base_y = kDesignHeight - 142.0f;
        const auto cap = [&](std::string_view name, float x, float y, float w, float h,
                             bool down) {
            paint.rect(x, y, w, h, 6.0f, down ? kAccent : kGlass, down ? kAccentLit : kGlassEdge,
                       1.0f);
            paint.centred(name, x + w * 0.5f, y + h * 0.5f, 11.0f, down ? 0xFFFFFFFFu : kDim,
                          Style::Mono);
        };
        cap("W", base_x + 34.0f, base_y, 32.0f, 28.0f, state.forward);
        cap("A", base_x, base_y + 31.0f, 32.0f, 28.0f, state.left);
        cap("S", base_x + 34.0f, base_y + 31.0f, 32.0f, 28.0f, state.back);
        cap("D", base_x + 68.0f, base_y + 31.0f, 32.0f, 28.0f, state.right);
        cap("SPACE", base_x, base_y + 62.0f, 100.0f, 20.0f, state.jump);
    }

    const std::string trace = std::format("{} sections   {} entities", state.sections,
                                          state.entities);
    paint.right(trace, kDesignWidth - 24.0f, kDesignHeight - 28.0f, 10.0f, 0xC8CCD2CCu,
                Style::Mono);
}

void draw_menu(gpu::UiRenderer& quads, gpu::TextRenderer& text, float width, float height,
               const MenuState& state) {
    Painter paint{quads, text};
    paint.scale = std::min(width / kDesignWidth, height / kDesignHeight);
    paint.origin_x = (width - kDesignWidth * paint.scale) * 0.5f;
    paint.origin_y = (height - kDesignHeight * paint.scale) * 0.5f;

    const auto& registry = features::Registry::instance();
    const auto all = registry.all();

    quads.panel(0.0f, 0.0f, width, height, 0.0f, kScrim);
    paint.rect(0.0f, 0.0f, kDesignWidth, kDesignHeight, 0.0f, kPage);

    // Top bar.
    constexpr float mark = 44.0f;
    paint.icon(Icon::MarkBody, kLeft, 44.0f, mark, kAccent);
    paint.icon(Icon::MarkCore, kLeft, 44.0f, mark, kAccentCore);
    const float wordmark_x = kLeft + paint.icon_width(Icon::MarkBody, mark) + 18.0f;
    paint.icon(Icon::Wordmark, wordmark_x, 44.0f, 26.0f, kText);
    const float after = wordmark_x + paint.icon_width(Icon::Wordmark, 26.0f) + 24.0f;
    paint.rect(after, 28.0f, 1.0f, 32.0f, 0.0f, 0x2A2A30FFu);
    paint.tracked("JAVA EDITION", after + 16.0f, 37.0f, 9.0f, kMute, 1.2f);
    paint.tracked("CLIENT MENU", after + 16.0f, 51.0f, 9.0f, kMute, 1.2f);

    const float esc_w = 36.0f;
    paint.rect(kRight - esc_w, 33.0f, esc_w, 22.0f, 6.0f, kChip, kChipEdge, 1.0f);
    paint.centred("ESC", kRight - esc_w * 0.5f, 44.0f, 9.0f, kDim, Style::Mono);
    const std::string ping = std::format("/ {} ms", state.ping_ms > 0 ? state.ping_ms : 24);
    paint.right(ping, kRight - esc_w - 18.0f, 44.0f, 10.5f, kMute, Style::Mono);
    const float server_right = kRight - esc_w - 18.0f - paint.measure(ping, 10.5f, Style::Mono) -
                               10.0f;
    paint.right(state.server, server_right, 44.0f, 12.0f, kText, Style::Bold);
    paint.icon(Icon::Signal, server_right - paint.measure(state.server, 12.0f, Style::Bold) -
                                 20.0f, 44.0f, 13.0f, kDim);
    paint.rect(kLeft, 88.0f, kRight - kLeft, 1.0f, 0.0f, kRule);

    // Tabs.
    paint.glow(kLeft, 115.0f, 124.0f, 38.0f, 9.0f, kAccent, 16.0f, kAccentGlow);
    paint.icon(Icon::Grid, kLeft + 20.0f, 134.0f, 14.0f, 0xFFFFFFFFu);
    paint.middle("Features", kLeft + 46.0f, 134.0f, 13.5f, 0xFFFFFFFFu, Style::Bold);
    constexpr Icon tabs[3] = {Icon::Display, Icon::Layers, Icon::Gear};
    for (int i = 0; i < 3; ++i) {
        const float tab_x = kLeft + 133.0f + static_cast<float>(i) * 48.0f;
        paint.rect(tab_x, 115.0f, 38.0f, 38.0f, 9.0f, kChip, kCardEdge, 1.0f);
        paint.icon_centred(tabs[i], tab_x + 19.0f, 134.0f, 15.0f, kDim);
    }

    size_t enabled_count = 0;
    size_t locked_count = 0;
    for (size_t i = 0; i < all.size(); ++i) {
        const auto id = static_cast<features::Id>(i);
        if (registry.enabled(id)) ++enabled_count;
        if (registry.locked(id)) ++locked_count;
    }
    const std::string summary = std::format("{} FEATURES / {} ENABLED / {} SERVER LOCK",
                                            all.size(), enabled_count, locked_count);
    paint.tracked(summary, kRight - paint.tracked_width(summary, 9.5f, 0.9f), 134.0f, 9.5f, kMute,
                  0.9f);

    // Filters.
    constexpr std::string_view filters[6] = {"All", "New", "HUD", "Input", "Visual", "PvP"};
    float filter_x = kLeft;
    for (size_t i = 0; i < 6; ++i) {
        const float pill_w = paint.measure(filters[i], 11.5f, Style::Bold) + 26.0f;
        if (i == 0) {
            paint.rect(filter_x, 179.0f, pill_w, 28.0f, 7.0f, kAccent);
        } else {
            paint.rect(filter_x, 179.0f, pill_w, 28.0f, 7.0f, kChip, kCardEdge, 1.0f);
        }
        paint.centred(filters[i], filter_x + pill_w * 0.5f, 193.0f, 11.5f,
                      i == 0 ? 0xFFFFFFFFu : kDim, Style::Bold);
        filter_x += pill_w + 8.0f;
    }

    paint.rect(kRight - 250.0f, 179.0f, 250.0f, 28.0f, 7.0f, kCard, kCardEdge, 1.0f);
    paint.icon(Icon::Search, kRight - 236.0f, 193.0f, 11.0f, kMute);
    paint.middle("Search features", kRight - 215.0f, 193.0f, 11.0f, kMute);
    paint.rect(kRight - 34.0f, 184.0f, 20.0f, 18.0f, 5.0f, kChip, kChipEdge, 1.0f);
    paint.centred("/", kRight - 24.0f, 193.0f, 9.5f, kMute, Style::Mono);

    // What the server has taken away, said plainly rather than hidden.
    paint.rect(kLeft, 232.0f, kRight - kLeft, 32.0f, 8.0f, kCard, kCardEdge, 1.0f);
    paint.icon(Icon::Lock, kLeft + 17.0f, 248.0f, 12.0f, kDim);
    const std::string headline = locked_count == 1 ? "This server controls 1 feature."
                                                   : std::format("This server controls {} features.",
                                                                 locked_count);
    paint.middle(headline, kLeft + 39.0f, 248.0f, 11.5f, kText, Style::Bold);
    paint.middle("Hitboxes is locked off - entity outlines are not allowed in ranked matches.",
                 kLeft + 39.0f + paint.measure(headline, 11.5f, Style::Bold) + 10.0f, 248.0f,
                 11.0f, kDim);
    const std::string policy = "SERVER POLICY - PLUGIN CHANNEL";
    paint.tracked(policy, kRight - 16.0f - paint.tracked_width(policy, 9.0f, 0.9f), 248.0f, 9.0f,
                  kMute, 0.9f);

    // Cards.
    constexpr float gap = 14.0f;
    constexpr float card_h = 178.0f;
    const float card_w = (kRight - kLeft - gap * 4.0f) / 5.0f;
    for (size_t i = 0; i < all.size(); ++i) {
        const auto& feature = all[i];
        const auto id = static_cast<features::Id>(i);
        const bool locked = registry.locked(id);
        const bool on = registry.enabled(id);
        const float x = kLeft + static_cast<float>(i % 5) * (card_w + gap);
        const float y = 284.0f + static_cast<float>(i / 5) * (card_h + gap);

        paint.rect(x, y, card_w, card_h, 10.0f, kCard, locked ? 0x2E2E34FFu : kCardEdge, 1.0f);
        paint.middle(feature.label, x + 16.0f, y + 23.0f, 12.5f, kText, Style::Bold);
        if (feature.server_may_object) {
            paint.icon(Icon::Warning, x + 20.0f + paint.measure(feature.label, 12.5f, Style::Bold),
                       y + 23.0f, 9.0f, kMute);
        }
        paint.icon(Icon::Heart, x + card_w - 30.0f, y + 23.0f, 13.0f,
                   feature.enabled_by_default ? kText : kHeartOff);

        preview(paint, i, x, y + 44.0f, card_w, state);

        const float bar_y = y + card_h - 40.0f;
        paint.rect(x + 14.0f, bar_y, 26.0f, 26.0f, 7.0f, kChip, kChipEdge, 1.0f);
        paint.icon_centred(Icon::Gear, x + 27.0f, bar_y + 13.0f, 11.0f, kMute);

        const float status_x = x + 48.0f;
        const float status_w = card_w - 62.0f;
        if (locked) {
            paint.rect(status_x, bar_y, status_w, 26.0f, 7.0f, kChip, kChipEdge, 1.0f);
            const std::string_view line = "Locked by server";
            const float line_w = paint.measure(line, 10.5f, Style::Bold);
            const float from = status_x + (status_w - line_w - 16.0f) * 0.5f;
            paint.icon(Icon::Lock, from, bar_y + 13.0f, 9.5f, kMute);
            paint.middle(line, from + 16.0f, bar_y + 13.0f, 10.5f, kDim, Style::Bold);
        } else {
            paint.rect(status_x, bar_y, status_w, 26.0f, 7.0f, on ? kOnFill : kOffFill,
                       on ? kOnEdge : kOffEdge, 1.0f);
            paint.centred(on ? "Enabled" : "Disabled", status_x + status_w * 0.5f, bar_y + 13.0f,
                          10.5f, on ? kOnText : kDim, Style::Bold);
        }
    }

    // Footer.
    paint.icon(Icon::Warning, kLeft, 884.0f, 10.0f, kMute);
    paint.middle("Some servers forbid this feature. Check their rules before enabling.",
                 kLeft + 17.0f, 884.0f, 10.5f, kMute);
    const std::string footer = std::format("PLASMA {}  /  MINECRAFT {}  /  VULKAN",
                                           state.client_version, state.version);
    paint.tracked(footer, kRight - paint.tracked_width(footer, 9.0f, 0.9f), 884.0f, 9.0f, kFaint,
                  0.9f);
}

} // namespace mcpp::ui
