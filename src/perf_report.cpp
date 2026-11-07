// A recording turned into the table worldbench prints and a picture of the
// frame times. Comparability is the point: every row says what the session
// held, because a frame of a game and a frame of the draw-path benchmark are
// different frames and get quoted as different numbers.
#include "mcpp/image_png.hpp"
#include "mcpp/perf.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <numeric>
#include <string>
#include <vector>

namespace mcpp::perf {
namespace {

// One stretch of the session: a minute of it, or all of it.
struct Slice {
    size_t from = 0;
    size_t to = 0;
};

std::vector<Slice> minutes(const std::vector<Sample>& frames) {
    std::vector<Slice> out;
    double elapsed = 0.0;
    size_t minute = 0;
    Slice current;
    for (size_t i = 0; i < frames.size(); ++i) {
        const size_t which = static_cast<size_t>(elapsed / 60000.0);
        if (which != minute) {
            current.to = i;
            out.push_back(current);
            minute = which;
            current = {i, i};
        }
        elapsed += frames[i].wall_ms;
    }
    current.to = frames.size();
    if (current.to > current.from) out.push_back(current);
    return out;
}

// Every value a column took, in the order it first took it, with the second
// it changed at: a session whose window or render distance moved is not one
// measurement, and the report has to say that rather than average over it.
template <typename Pick>
std::string changes(const std::vector<Sample>& frames, Pick pick) {
    std::string out;
    double elapsed = 0.0;
    auto previous = pick(frames.front());
    out = std::format("{}", previous);
    size_t moved = 0;
    for (const auto& frame : frames) {
        const auto value = pick(frame);
        if (value != previous) {
            ++moved;
            if (moved <= 6) out += std::format(", then {} at {:.0f} s", value, elapsed / 1000.0);
            previous = value;
        }
        elapsed += frame.wall_ms;
    }
    if (moved > 6) out += std::format(", and {} more changes", moved - 6);
    return out;
}

std::string window_size(const Sample& sample) {
    return std::format("{}x{}", sample.width, sample.height);
}

} // namespace

std::string report(const Recording& recording) {
    const auto& frames = recording.frames;
    std::string out;
    const auto add = [&out](std::string text) { out += text; };
    if (frames.empty()) {
        return "the recording holds no frames\n";
    }

    const Summary all = summarise(frames);
    const bool live = recording.context.source.starts_with("live");

    add("session\n");
    add(std::format("  started           {}\n",
                    recording.context.started.empty() ? "-" : recording.context.started));
    add(std::format("  source            {}{}\n", recording.context.source,
                    live ? " (a played game, not the draw-path benchmark)"
                         : " (a tool, not a played game)"));
    add(std::format("  server            {}\n",
                    recording.context.server.empty() ? "-" : recording.context.server));
    add(std::format("  dimension         {}\n",
                    recording.context.dimension.empty() ? "-" : recording.context.dimension));
    add(std::format("  profile           {}\n",
                    recording.context.profile.empty() ? "-" : recording.context.profile));
    if (!recording.context.note.empty()) {
        add(std::format("  note              {}\n", recording.context.note));
    }
    add(std::format("  frames            {} kept of {} the session had\n", frames.size(),
                    recording.seen));
    if (recording.dropped > 0) {
        add(std::format("  dropped           {} frames fell out of the ring: the session was "
                        "longer than the {} frames it was opened with, and the oldest frames\n"
                        "                    are gone. The whole-session and per-minute rows "
                        "below are the kept frames only, and the first minute is not the\n"
                        "                    first minute of the session.\n",
                        recording.dropped, recording.context.capacity));
    }
    add(std::format("  length            {:.1f} s of frames\n", all.seconds));
    add(std::format("  window            {}\n", changes(frames, window_size)));
    add(std::format("  render distance   {}\n", changes(frames, [](const Sample& s) {
                        return static_cast<int>(s.render_distance);
                    })));
    add(std::format("  screens open      {} frames ({:.1f}% of them)\n", all.frames_with_screen,
                    100.0 * static_cast<double>(all.frames_with_screen) /
                        static_cast<double>(frames.size())));
    add(std::format("  entities drawn    {} to {} a frame\n", all.entities_min, all.entities_max));
    add(std::format("  sections drawn    {} to {} a frame\n", all.sections_min, all.sections_max));
    add(std::format("  particles alive   at most {}\n", all.particles_max));

    add("\nwhat this measures\n");
    if (live) {
        add("  Every frame of a real session: the server's packets, the physics, chunks\n"
            "  streaming in and being meshed on their own thread, entities, particles, the\n"
            "  hand, sound, the hud and whatever screen was open. That is more work than\n"
            "  `worldbench` does, so these numbers are lower than its table and the two may\n"
            "  not be quoted as the same measurement. A `worldbench` row is the draw path\n"
            "  over a world with nothing left to load; this is a game.\n"
            "  cpu ms is this thread's own cpu time between the same point in two frames, so\n"
            "  the wait for the gpu is not in it. gpu ms is the renderer's timestamp query\n"
            "  around the frame, which is the frame before last - a frame's own gpu time is\n"
            "  not readable until its fence is passed. Over a session that shift does not\n"
            "  move the average; for one frame it is the wrong frame's gpu time.\n"
            "  wall ms is the whole loop, measured once a frame at one fixed point in it, so\n"
            "  the fps below is the fps the player had, vsync and all.\n");
    } else {
        add("  A recording taken from a tool rather than from a played game: what it holds is\n"
            "  whatever that tool does per frame, which is less than a game does. It is here\n"
            "  to prove the recorder, and is not a number about playing.\n");
    }

    add("\nwhole session\n");
    add("  frames  seconds  avg fps  1% low  0.1% low   p99 ms  median ms   worst ms   cpu ms"
        "   gpu ms\n");
    add(std::format("  {:>6} {:>8.1f} {:>8.0f} {:>7.0f} {:>9.0f} {:>8.3f} {:>10.3f} {:>10.3f}"
                    " {:>8.3f} {:>8.3f}\n",
                    all.frames, all.seconds, all.avg_fps, all.low_1_fps, all.low_01_fps,
                    all.p99_ms, all.median_ms, all.worst_ms, all.cpu_ms, all.gpu_ms));

    add("\nthe renderer's own cpu split, ms a frame (what WorldRenderer::FrameCost counts)\n");
    add("  record    sort  upload    geom     sky     hud    rest  submit  readbk\n");
    const auto split_row = [](const Cost& cost) {
        const double rest = std::max(0.0, static_cast<double>(cost.record) - cost.sort -
                                              cost.geometry - cost.sky - cost.overlay);
        return std::format("  {:>6.3f} {:>7.3f} {:>7.3f} {:>7.3f} {:>7.3f} {:>7.3f} {:>7.3f}"
                           " {:>7.3f} {:>7.3f}\n",
                           cost.record, cost.sort, cost.sort_upload, cost.geometry, cost.sky,
                           cost.overlay, rest, cost.submit, cost.readback);
    };
    add(split_row(all.cost));
    add("  record is the whole recording into the command buffer and the five columns after it\n"
        "  are inside it, not beside it: sort is the translucent quads re-sorted as the camera\n"
        "  moves (upload is the part of that sort which copies into the command buffer), geom\n"
        "  the loop that culls sections and records a draw each, hud the overlay pass, and rest\n"
        "  the part of the recording those do not name. submit and readbk are outside record:\n"
        "  the queue submit and the timestamp readback. All of it is the world renderer only -\n"
        "  physics, the server's packets, meshing hand-offs and the hud's build are in cpu ms\n"
        "  and not split out anywhere here.\n");

    const auto slices = minutes(frames);
    add(std::format("\nper minute ({} of them), so a drop in the middle of a game shows up\n",
                    slices.size()));
    add("  minute  frames  avg fps  1% low  0.1% low   p99 ms   cpu ms   gpu ms   sect    ent"
        "   part  screen\n");
    for (size_t i = 0; i < slices.size(); ++i) {
        const auto& slice = slices[i];
        const Summary piece = summarise(std::span<const Sample>(frames).subspan(
            slice.from, slice.to - slice.from));
        add(std::format("  {:>6} {:>7} {:>8.0f} {:>7.0f} {:>9.0f} {:>8.3f} {:>8.3f} {:>8.3f}"
                        " {:>6} {:>6} {:>6} {:>7}\n",
                        i, piece.frames, piece.avg_fps, piece.low_1_fps, piece.low_01_fps,
                        piece.p99_ms, piece.cpu_ms, piece.gpu_ms, piece.sections_max,
                        piece.entities_max, piece.particles_max, piece.frames_with_screen));
    }

    add("\nwhich low is which\n"
        "  avg fps     1000 / the mean frame time.\n"
        "  1% low      1000 / the mean frame time of the slowest 1% of frames (ceil to at\n"
        "              least one frame). This is the average-of-the-worst-1% definition, the\n"
        "              one CapFrameX calls a 1% low average and the one worldbench prints. It\n"
        "              is NOT the 99th percentile frame converted to fps, which is a higher\n"
        "              number, and it is not a 1% percentile of the fps series either.\n"
        "  0.1% low    the same over the slowest 0.1% of frames.\n"
        "  p99 ms      one frame: the frame at index floor(0.99 * n) of the frames sorted\n"
        "              fastest first.\n"
        "  Both definitions live in mcpp::perf::mean_of_slowest and ::percentile_ms, and\n"
        "  tests/test_perf.cpp checks them against a series worked out by hand.\n");
    return out;
}

namespace {

// A 5x7 pixel alphabet, one byte a row, bit 4 leftmost. The graph has to be
// readable on its own, and the only text we can draw into a png is text we
// carry ourselves.
struct Glyph {
    char code;
    uint8_t rows[7];
};

constexpr Glyph kFont[] = {
    {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}},
    {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},
    {'3', {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}},
    {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}},
    {'5', {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}},
    {'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}},
    {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},
    {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}},
    {'A', {0x04, 0x0A, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
    {'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}},
    {'D', {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}},
    {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}},
    {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
    {'G', {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E}},
    {'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'I', {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'J', {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}},
    {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}},
    {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
    {'M', {0x11, 0x1B, 0x15, 0x11, 0x11, 0x11, 0x11}},
    {'N', {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}},
    {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
    {'Q', {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}},
    {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
    {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}},
    {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},
    {'W', {0x11, 0x11, 0x11, 0x11, 0x15, 0x1B, 0x11}},
    {'X', {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}},
    {'Y', {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}},
    {'Z', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}},
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04}},
    {',', {0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x08}},
    {':', {0x00, 0x04, 0x00, 0x00, 0x00, 0x04, 0x00}},
    {'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}},
    {'/', {0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10}},
    {'%', {0x11, 0x12, 0x02, 0x04, 0x08, 0x09, 0x11}},
    {'(', {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02}},
    {')', {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08}},
    {'+', {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00}},
    {'=', {0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00}},
    {'x', {0x00, 0x00, 0x11, 0x0A, 0x04, 0x0A, 0x11}},
};

const Glyph* glyph_of(char code) {
    const char upper = code >= 'a' && code <= 'z' && code != 'x'
                           ? static_cast<char>(code - 'a' + 'A')
                           : code;
    for (const auto& glyph : kFont) {
        if (glyph.code == upper) return &glyph;
    }
    return nullptr;
}

class Canvas {
public:
    Canvas(uint32_t width, uint32_t height, uint32_t background)
        : width_(width), height_(height), pixels_(static_cast<size_t>(width) * height * 4) {
        for (uint32_t y = 0; y < height_; ++y) {
            for (uint32_t x = 0; x < width_; ++x) set(x, y, background);
        }
    }

    void set(int32_t x, int32_t y, uint32_t rgba) {
        if (x < 0 || y < 0 || x >= static_cast<int32_t>(width_) ||
            y >= static_cast<int32_t>(height_)) {
            return;
        }
        const size_t at = (static_cast<size_t>(y) * width_ + static_cast<size_t>(x)) * 4;
        pixels_[at] = static_cast<uint8_t>(rgba >> 24);
        pixels_[at + 1] = static_cast<uint8_t>(rgba >> 16);
        pixels_[at + 2] = static_cast<uint8_t>(rgba >> 8);
        pixels_[at + 3] = static_cast<uint8_t>(rgba);
    }

    void fill(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t rgba) {
        for (int32_t dy = 0; dy < h; ++dy) {
            for (int32_t dx = 0; dx < w; ++dx) set(x + dx, y + dy, rgba);
        }
    }

    void dashes(int32_t x, int32_t y, int32_t w, uint32_t rgba, int32_t on, int32_t off) {
        for (int32_t dx = 0; dx < w; ++dx) {
            if (dx % (on + off) < on) set(x + dx, y, rgba);
        }
    }

    void text(std::string_view line, int32_t x, int32_t y, int32_t scale, uint32_t rgba) {
        int32_t at = x;
        for (const char code : line) {
            const Glyph* glyph = glyph_of(code);
            if (glyph != nullptr) {
                for (int32_t row = 0; row < 7; ++row) {
                    for (int32_t column = 0; column < 5; ++column) {
                        if ((glyph->rows[row] & (1u << (4 - column))) == 0) continue;
                        fill(at + column * scale, y + row * scale, scale, scale, rgba);
                    }
                }
            }
            at += 6 * scale;
        }
    }

    static int32_t text_width(std::string_view line, int32_t scale) {
        return static_cast<int32_t>(line.size()) * 6 * scale;
    }

    const std::vector<uint8_t>& pixels() const { return pixels_; }
    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }

private:
    uint32_t width_;
    uint32_t height_;
    std::vector<uint8_t> pixels_;
};

} // namespace

bool write_graph(const std::filesystem::path& path, const Recording& recording) {
    const auto& frames = recording.frames;
    if (frames.empty()) return false;

    constexpr uint32_t kWidth = 1600;
    constexpr uint32_t kHeight = 648;
    constexpr int32_t kLeft = 78;
    constexpr int32_t kRight = 78;
    constexpr int32_t kTop = 92;
    constexpr int32_t kBottom = 80;
    constexpr uint32_t kBack = 0x111114FFu;
    constexpr uint32_t kPlot = 0x17171AFFu;
    constexpr uint32_t kGrid = 0x2A2A30FFu;
    constexpr uint32_t kText = 0xD2D2DAFFu;
    constexpr uint32_t kMute = 0x8A8A94FFu;
    constexpr uint32_t kBand = 0x2B5E72FFu;
    constexpr uint32_t kLine = 0x4FD1FFFFu;
    constexpr uint32_t kSpike = 0xFF6B6BFFu;
    constexpr uint32_t kGpu = 0xC77DFFFFu;
    constexpr uint32_t kCpu = 0x7CE38BFFu;

    const int32_t plot_x = kLeft;
    const int32_t plot_y = kTop;
    const int32_t plot_w = static_cast<int32_t>(kWidth) - kLeft - kRight;
    const int32_t plot_h = static_cast<int32_t>(kHeight) - kTop - kBottom;

    const Summary all = summarise(frames);
    std::vector<double> wall;
    wall.reserve(frames.size());
    for (const auto& frame : frames) wall.push_back(frame.wall_ms);
    const double p999 = percentile_ms(wall, 0.999);
    // The scale follows the frames that matter, not the single worst one: a
    // 400 ms hitch would flatten everything else into one line. Frames above
    // the top are drawn clamped, in the spike colour, and counted in the
    // caption so they are not quietly cropped away.
    double top_ms = std::max(p999 * 1.25, all.median_ms * 2.0);
    if (top_ms <= 0.0) top_ms = 1.0;
    size_t clipped = 0;
    for (const double ms : wall) {
        if (ms > top_ms) ++clipped;
    }

    Canvas canvas(kWidth, kHeight, kBack);
    canvas.fill(plot_x, plot_y, plot_w, plot_h, kPlot);

    const auto to_y = [&](double ms) {
        const double fraction = std::clamp(ms / top_ms, 0.0, 1.0);
        return plot_y + plot_h - static_cast<int32_t>(fraction * (plot_h - 1));
    };

    // Horizontal grid: five lines of frame time, labelled in ms and in the fps
    // a frame of that length is.
    for (int i = 0; i <= 5; ++i) {
        const double ms = top_ms * static_cast<double>(i) / 5.0;
        const int32_t y = to_y(ms);
        canvas.fill(plot_x, y, plot_w, 1, kGrid);
        const std::string label = std::format("{:.1f}", ms);
        canvas.text(label, plot_x - 10 - Canvas::text_width(label, 2), y - 7, 2, kMute);
        if (ms > 0.01) {
            const std::string fps = std::format("{:.0f}", 1000.0 / ms);
            canvas.text(fps, plot_x + plot_w + 8, y - 7, 2, kGrid);
        }
    }
    canvas.text("MS", 10, plot_y + plot_h + 10, 2, kMute);
    canvas.text("FPS", plot_x + plot_w + 8, plot_y + plot_h + 10, 2, kMute);

    // The lines a player cares about: 60 and 120 frames a second.
    for (const double ms : {16.667, 8.333}) {
        if (ms > top_ms) continue;
        const int32_t y = to_y(ms);
        canvas.dashes(plot_x, y, plot_w, kBand, 6, 6);
        const std::string label = std::format("{:.0f} FPS", 1000.0 / ms);
        canvas.text(label, plot_x + 8, y - 11, 2, kBand);
    }

    // One column of pixels per x: the fastest and slowest frame in it, so a
    // single bad frame in a thousand is still a visible spike.
    const double total_ms = all.seconds * 1000.0;
    std::vector<double> column_low(static_cast<size_t>(plot_w), 0.0);
    std::vector<double> column_high(static_cast<size_t>(plot_w), 0.0);
    std::vector<double> column_sum(static_cast<size_t>(plot_w), 0.0);
    std::vector<double> column_cpu(static_cast<size_t>(plot_w), 0.0);
    std::vector<double> column_gpu(static_cast<size_t>(plot_w), 0.0);
    std::vector<size_t> column_count(static_cast<size_t>(plot_w), 0);
    double elapsed = 0.0;
    for (const auto& frame : frames) {
        const double fraction = total_ms > 0.0 ? elapsed / total_ms : 0.0;
        elapsed += frame.wall_ms;
        const size_t column = std::min(static_cast<size_t>(plot_w) - 1,
                                       static_cast<size_t>(fraction * (plot_w - 1)));
        if (column_count[column] == 0) {
            column_low[column] = column_high[column] = frame.wall_ms;
        } else {
            column_low[column] = std::min(column_low[column], frame.wall_ms);
            column_high[column] = std::max(column_high[column], frame.wall_ms);
        }
        column_sum[column] += frame.wall_ms;
        column_cpu[column] += frame.cpu_ms;
        column_gpu[column] += frame.gpu_ms;
        ++column_count[column];
    }
    for (int32_t column = 0; column < plot_w; ++column) {
        const size_t at = static_cast<size_t>(column);
        if (column_count[at] == 0) continue;
        const int32_t x = plot_x + column;
        const int32_t low = to_y(column_low[at]);
        const int32_t high = to_y(column_high[at]);
        for (int32_t y = high; y <= low; ++y) {
            canvas.set(x, y, column_high[at] > top_ms ? kSpike : kLine);
        }
        const double average = column_sum[at] / static_cast<double>(column_count[at]);
        canvas.set(x, to_y(average), 0xFFFFFFFFu);
        canvas.set(x, to_y(column_cpu[at] / static_cast<double>(column_count[at])), kCpu);
        canvas.set(x, to_y(column_gpu[at] / static_cast<double>(column_count[at])), kGpu);
    }

    // Minute marks along the bottom, and the per-minute average fps above them.
    const auto marks = static_cast<int>(all.seconds / 60.0);
    for (int minute = 0; minute <= marks; ++minute) {
        const double at_ms = minute * 60000.0;
        if (total_ms <= 0.0) break;
        const int32_t x = plot_x + static_cast<int32_t>(at_ms / total_ms * (plot_w - 1));
        canvas.fill(x, plot_y, 1, plot_h, minute == 0 ? kGrid : 0x33333BFFu);
        const std::string label = std::format("{}:00", minute);
        canvas.text(label, x - Canvas::text_width(label, 2) / 2, plot_y + plot_h + 10, 2, kMute);
    }
    const std::string tail = std::format("{:.0f} S", all.seconds);
    canvas.text(tail, plot_x + plot_w - Canvas::text_width(tail, 2), plot_y + plot_h + 10, 2,
                kGrid);

    // Heading and captions.
    canvas.text("PLASMA FRAME TIME", 24, 22, 3, kText);
    const std::string headline =
        std::format("{:.0f} FPS AVG / {:.0f} FPS 1% LOW / {:.0f} FPS 0.1% LOW / P99 {:.2f} MS",
                    all.avg_fps, all.low_1_fps, all.low_01_fps, all.p99_ms);
    canvas.text(headline, 24, 52, 2, kLine);
    const std::string context = std::format(
        "{} / {} / {} / {} FRAMES / {:.0f} S / RD {} / {}x{}", recording.context.source,
        recording.context.server.empty() ? "-" : recording.context.server,
        recording.context.dimension.empty() ? "-" : recording.context.dimension, frames.size(),
        all.seconds, static_cast<int>(frames.front().render_distance), frames.front().width,
        frames.front().height);
    canvas.text(context, 24, 70, 2, kMute);

    const std::string legend = std::format(
        "WHITE=MEAN PER COLUMN  BLUE=FASTEST TO SLOWEST FRAME IN IT  GREEN=CPU MS  "
        "PURPLE=GPU MS  RED=OVER {:.1f} MS ({} FRAMES)",
        top_ms, clipped);
    canvas.text(legend, 24, static_cast<int32_t>(kHeight) - 44, 2, kMute);
    const std::string split = std::format(
        "CPU {:.3f} MS  GPU {:.3f} MS  MEDIAN {:.2f} MS  WORST {:.2f} MS  SECTIONS {}-{}  "
        "ENTITIES {}-{}  PARTICLES MAX {}",
        all.cpu_ms, all.gpu_ms, all.median_ms, all.worst_ms, all.sections_min, all.sections_max,
        all.entities_min, all.entities_max, all.particles_max);
    canvas.text(split, 24, static_cast<int32_t>(kHeight) - 24, 2, kMute);

    write_png(path, canvas.width(), canvas.height(), canvas.pixels());
    return true;
}

} // namespace mcpp::perf
