#include "mcpp/ui/hud/hud.hpp"

#include <algorithm>
#include <cmath>

namespace mcpp::ui::hud {
namespace {

constexpr size_t kHistory = 100;

int floor_int(double value) { return static_cast<int>(std::floor(value)); }

uint32_t with_alpha(float alpha, uint32_t rgb) {
    return static_cast<uint32_t>(static_cast<int>(std::floor(alpha * 255.0f))) << 24 |
           (rgb & 0xFFFFFFu);
}

double scale_of(const Options& options) { return options.get_double("chatScale"); }

int line_height(const Options& options) {
    return static_cast<int>(9.0 * (options.get_double("chatLineSpacing") + 1.0));
}

} // namespace

int chat_width(double option) { return floor_int(option * 280.0 + 40.0); }
int chat_height(double option) { return floor_int(option * 160.0 + 20.0); }

int Chat::lines_per_page(const Options& options, bool focused) const {
    const double option = options.get_double(focused ? "chatHeightFocused" : "chatHeightUnfocused");
    return chat_height(option) / line_height(options);
}

void Chat::display(const Message& message, const Font& font, const Options& options,
                   bool focused) {
    const int width = floor_int(chat_width(options.get_double("chatWidth")) / scale_of(options));
    auto lines = font.split(message.content, width);
    if (lines.empty()) lines.emplace_back();
    for (size_t i = 0; i < lines.size(); ++i) {
        if (focused && scroll_ > 0) {
            new_since_scroll_ = true;
            scroll(1, options, focused);
        }
        Line line;
        line.added = message.added;
        // A wrapped line starts one space in, which is the game's indent.
        if (i > 0) line.runs.push_back(Run{" ", {}});
        for (auto& run : lines[i]) line.runs.push_back(std::move(run));
        line.tag = message.tag;
        line.end_of_entry = i + 1 == lines.size();
        trimmed_.push_front(std::move(line));
    }
    while (trimmed_.size() > kHistory) trimmed_.pop_back();
}

void Chat::add(const Component& content, int32_t tag, int32_t gui_ticks, const Font& font,
               const Options& options, bool focused) {
    Message message{gui_ticks, content, tag};
    display(message, font, options, focused);
    all_.push_front(std::move(message));
    while (all_.size() > kHistory) all_.pop_back();
}

void Chat::clear() {
    trimmed_.clear();
    all_.clear();
    reset_scroll();
}

void Chat::reset_scroll() {
    scroll_ = 0;
    new_since_scroll_ = false;
}

void Chat::scroll(int lines, const Options& options, bool focused) {
    scroll_ += lines;
    const int size = static_cast<int>(trimmed_.size());
    const int page = lines_per_page(options, focused);
    if (scroll_ > size - page) scroll_ = size - page;
    if (scroll_ <= 0) {
        scroll_ = 0;
        new_since_scroll_ = false;
    }
}

void Chat::rescale(const Font& font, const Options& options, bool focused) {
    reset_scroll();
    trimmed_.clear();
    for (auto it = all_.rbegin(); it != all_.rend(); ++it) display(*it, font, options, focused);
}

void Chat::render(GuiGraphics& gfx, const Options& options, int32_t gui_ticks, bool focused,
                  int, int) const {
    if (options.get_int("chatVisibility") == 2) return;
    const int size = static_cast<int>(trimmed_.size());
    if (size == 0) return;
    const float scale = static_cast<float>(scale_of(options));
    const int width = static_cast<int>(std::ceil(chat_width(options.get_double("chatWidth")) /
                                                 static_cast<double>(scale)));
    const int bottom = static_cast<int>(std::floor(static_cast<float>(gfx.height - 40) / scale));
    const float text_opacity =
        static_cast<float>(options.get_double("chatOpacity")) * 0.9f + 0.1f;
    const float background = static_cast<float>(options.get_double("textBackgroundOpacity"));
    const double spacing = options.get_double("chatLineSpacing");
    const int height = line_height(options);
    const int text_offset = static_cast<int>(std::lround(8.0 * (spacing + 1.0) - 4.0 * spacing));
    const int page = lines_per_page(options, focused);

    const auto alpha_of = [&](const Line& line) -> float {
        if (focused) return 1.0f;
        double t = static_cast<double>(gui_ticks - line.added) / 200.0;
        t = std::clamp((1.0 - t) * 10.0, 0.0, 1.0);
        return static_cast<float>(t * t);
    };

    gfx.push_pose();
    gfx.scale_by(scale, scale);
    gfx.translate(4.0f, 0.0f);
    const int visible = std::min(size - scroll_, page);
    for (int i = visible - 1; i >= 0; --i) {
        const Line& line = trimmed_[static_cast<size_t>(i + scroll_)];
        const float alpha = alpha_of(line);
        if (alpha <= 1.0e-5f) continue;
        const int line_bottom = bottom - i * height;
        gfx.fill(-4, line_bottom - height, width + 4 + 4, line_bottom, with_alpha(alpha * background, 0));
    }
    int drawn = 0;
    for (int i = visible - 1; i >= 0; --i) {
        const Line& line = trimmed_[static_cast<size_t>(i + scroll_)];
        const float alpha = alpha_of(line);
        if (alpha <= 1.0e-5f) continue;
        ++drawn;
        const int line_bottom = bottom - i * height;
        const int line_top = line_bottom - height;
        const int text_y = line_bottom - text_offset;
        gfx.draw_string(line.runs, 0, text_y, with_alpha(alpha * text_opacity, 0xFFFFFF), true);
        if (line.tag != 0) {
            gfx.fill(-4, line_top, -2, line_bottom, with_alpha(alpha * text_opacity, 0xD0D0D0));
        }
    }
    if (focused) {
        const int total = size * height;
        const int shown = drawn * height;
        if (total != shown && size > 0) {
            const int offset = scroll_ * shown / size - bottom;
            const int bar = shown * shown / total;
            const int alpha = offset > 0 ? 170 : 96;
            const uint32_t colour = new_since_scroll_ ? 0xCC3333u : 0x3333AAu;
            const int x = width + 4;
            gfx.fill(x, -offset, x + 2, -offset - bar, static_cast<uint32_t>(alpha) << 24 | colour);
            gfx.fill(x + 2, -offset, x + 1, -offset - bar,
                     static_cast<uint32_t>(alpha) << 24 | 0xCCCCCCu);
        }
    }
    gfx.pop_pose();
}

} // namespace mcpp::ui::hud
