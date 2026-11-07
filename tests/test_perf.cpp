// P: the live frame recorder. Every expected value below is worked out by
// hand from the series above it, never taken from the code under test.
//
// The two definitions a "1% low" can mean are both checked, on a series built
// so they disagree: the mean of the slowest 1% of frames is not the 99th
// percentile frame.
#include "mcpp/perf.hpp"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace mcpp::perf;

int failures = 0;

void check(bool condition, std::string_view what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << '\n';
        ++failures;
    }
}

void near(double got, double want, double slack, std::string_view what) {
    if (std::abs(got - want) > slack) {
        std::cerr << "FAIL: " << what << ": got " << got << ", want " << want << '\n';
        ++failures;
    }
}

std::filesystem::path scratch_file(const char* name) {
    const char* base = std::getenv("TMPDIR");
    return std::filesystem::path(base != nullptr ? base : "/tmp") / name;
}

std::vector<Sample> series(const std::vector<double>& frame_ms) {
    std::vector<Sample> out;
    out.reserve(frame_ms.size());
    for (const double ms : frame_ms) {
        Sample sample;
        sample.wall_ms = ms;
        sample.cpu_ms = ms * 0.5;
        sample.gpu_ms = ms * 0.25;
        sample.width = 1920;
        sample.height = 1080;
        sample.render_distance = 12;
        out.push_back(sample);
    }
    return out;
}

// 100 frames: ninety of 10 ms, nine of 20 ms, one of 100 ms.
//   sum           900 + 180 + 100 = 1180 ms, so the mean is 11.8 ms
//   avg fps       1000 / 11.8 = 84.7457627...
//   slowest 1%    floor(0.01 * 100) = 1 frame, the 100 ms one: 10 fps
//   slowest 0.1%  max(1, floor(0.1)) = 1 frame, the same one: 10 fps
//   sorted        indices 0..89 are 10 ms, 90..98 are 20 ms, 99 is 100 ms
//   p99           index floor(0.99 * 100) = 99, so 100 ms
//   median        index 50, so 10 ms
void test_known_series() {
    std::vector<double> ms(90, 10.0);
    ms.insert(ms.end(), 9, 20.0);
    ms.push_back(100.0);
    const auto frames = series(ms);
    const Summary summary = summarise(frames);

    check(summary.frames == 100, "100 frames");
    near(summary.seconds, 1.18, 1e-9, "1.18 s of frames");
    near(summary.avg_fps, 1000.0 / 11.8, 1e-9, "average fps");
    near(summary.low_1_fps, 10.0, 1e-9, "1% low is the mean of the slowest frame");
    near(summary.low_01_fps, 10.0, 1e-9, "0.1% low on 100 frames is still one frame");
    near(summary.p99_ms, 100.0, 1e-9, "p99 is the frame at index 99");
    near(summary.median_ms, 10.0, 1e-9, "median");
    near(summary.worst_ms, 100.0, 1e-9, "worst");
    near(summary.cpu_ms, 5.9, 1e-9, "cpu ms is the mean of half of each frame");
    near(summary.gpu_ms, 2.95, 1e-9, "gpu ms is the mean of a quarter of each frame");
}

// 1000 frames: 990 of 5 ms and 10 of 50 ms. Here the two definitions part:
//   slowest 1%    floor(0.01 * 1000) = 10 frames, all 50 ms: 20 fps
//   p99           sorted index floor(0.99 * 1000) = 990, the first 50 ms frame
//   p99 as fps    1000 / 50 = 20 as well, so a third series separates them
void test_definitions_disagree() {
    std::vector<double> ms(990, 5.0);
    ms.insert(ms.end(), 10, 50.0);
    const auto frames = series(ms);
    const Summary summary = summarise(frames);
    near(summary.avg_fps, 1000.0 / 5.45, 1e-9, "average fps over 5.45 ms");
    near(summary.low_1_fps, 20.0, 1e-9, "1% low over ten 50 ms frames");
    near(summary.p99_ms, 50.0, 1e-9, "p99 is the first 50 ms frame");

    // 1000 frames: 990 of 5 ms, 9 of 40 ms, 1 of 200 ms.
    //   slowest 1%   10 frames: (9 * 40 + 200) / 10 = 56 ms, so 1% low is
    //                1000 / 56 = 17.857 fps
    //   p99          sorted index 990, which is a 40 ms frame, so 40 ms -
    //                25 fps if anyone converted it, which is not the 1% low
    std::vector<double> mixed(990, 5.0);
    mixed.insert(mixed.end(), 9, 40.0);
    mixed.push_back(200.0);
    const Summary other = summarise(series(mixed));
    near(other.low_1_fps, 1000.0 / 56.0, 1e-9, "1% low averages the worst ten");
    near(other.p99_ms, 40.0, 1e-9, "p99 is one frame, not an average");
    check(std::abs(other.low_1_fps - 1000.0 / other.p99_ms) > 7.0,
          "the two definitions differ on this series");
}

// The helpers on their own, on a series short enough to count on fingers:
// 1..10 ms. percentile_ms takes the frame at index floor(q * n), no
// interpolation: 0.99 -> index 9 (10 ms), 0.5 -> index 5 (6 ms).
void test_helpers() {
    const std::vector<double> ms{3.0, 1.0, 4.0, 1.0, 5.0, 9.0, 2.0, 6.0, 8.0, 7.0};
    near(percentile_ms(ms, 0.99), 9.0, 1e-9, "p99 of ten frames is the slowest");
    near(percentile_ms(ms, 0.5), 5.0, 1e-9, "the median index is floor(0.5 * 10) = 5");
    near(mean_of_slowest(ms, 0.01), 9.0, 1e-9, "at least one frame is taken");
    near(mean_of_slowest(ms, 0.5), (9.0 + 8.0 + 7.0 + 6.0 + 5.0) / 5.0, 1e-9,
         "the slowest half of ten frames");
    near(mean_of_slowest(ms, 1.0), 4.6, 1e-9, "all ten average 4.6 ms");
    near(percentile_ms({}, 0.99), 0.0, 1e-9, "no frames, no percentile");
}

// The ring keeps the newest frames and says how many it dropped.
void test_ring() {
    Recorder recorder;
    Context context;
    context.source = "test";
    recorder.open(16, context);
    check(recorder.recording(), "open means recording");
    for (int i = 0; i < 40; ++i) {
        Sample sample;
        sample.wall_ms = static_cast<double>(i);
        recorder.push(sample);
    }
    check(recorder.seen() == 40, "40 frames seen");
    check(recorder.stored() == 16, "16 frames held");
    check(recorder.dropped() == 24, "24 frames dropped");
    const auto held = recorder.frames();
    check(held.size() == 16, "16 frames handed back");
    check(held.front().wall_ms == 24.0 && held.back().wall_ms == 39.0,
          "oldest first, and the oldest held is frame 24");
    recorder.close();
    recorder.push(Sample{});
    check(recorder.seen() == 40, "a closed recorder takes nothing");
}

// What is written is what comes back.
void test_round_trip() {
    Recorder recorder;
    Context context;
    context.started = "2026-11-07 22:00:00";
    context.source = "live";
    context.server = "127.0.0.1:25601";
    context.dimension = "minecraft:overworld";
    context.profile = "plasma";
    recorder.open(64, context);
    for (int i = 0; i < 50; ++i) {
        Sample sample;
        sample.wall_ms = 4.0 + static_cast<double>(i) * 0.125;
        sample.cpu_ms = 1.5;
        sample.gpu_ms = 2.25;
        sample.cost.sort = 0.0625f;
        sample.cost.geometry = 0.125f;
        sample.cost.submit = 0.0312f;
        sample.sections = 300u + static_cast<uint32_t>(i);
        sample.entities = 7;
        sample.particles = 64;
        sample.width = 2560;
        sample.height = 1440;
        sample.render_distance = 16;
        sample.flags = i % 10 == 0 ? kScreenOpen : 0;
        recorder.push(sample);
    }
    const auto path = scratch_file("plasma-test-perf.plperf");
    check(recorder.write(path), "the recording writes");
    const auto back = read(path);
    check(back.has_value(), "the recording reads back");
    if (!back) return;
    check(back->frames.size() == 50, "50 frames back");
    check(back->context.server == "127.0.0.1:25601", "the server came back");
    check(back->context.dimension == "minecraft:overworld", "the dimension came back");
    check(back->dropped == 0, "nothing dropped");
    const auto& last = back->frames.back();
    near(last.wall_ms, 4.0 + 49 * 0.125, 1e-4, "the last frame's wall time");
    near(last.cost.geometry, 0.125, 1e-4, "the geometry column survived");
    check(last.sections == 349 && last.entities == 7 && last.particles == 64,
          "the context columns survived");
    check(last.width == 2560 && last.height == 1440 && last.render_distance == 16,
          "window and render distance survived");
    const Summary summary = summarise(back->frames);
    check(summary.frames_with_screen == 5, "five frames had a screen open");
    near(summary.cpu_ms, 1.5, 1e-4, "cpu ms back");
    near(summary.gpu_ms, 2.25, 1e-4, "gpu ms back");
    std::filesystem::remove(path);
}

// A session with a bad middle minute: the per-minute table has to show it.
void test_report_and_graph() {
    Recording recording;
    recording.context.source = "live";
    recording.context.server = "127.0.0.1:25601";
    recording.context.dimension = "minecraft:overworld";
    recording.context.profile = "plasma";
    // Minute 0: 12000 frames of 5 ms = 60 s. Minute 1: 1200 frames of 50 ms =
    // 60 s. Minute 2: 6000 frames of 5 ms = 30 s.
    const auto push = [&](size_t count, double ms, uint16_t width, uint8_t rd) {
        for (size_t i = 0; i < count; ++i) {
            Sample sample;
            sample.wall_ms = ms;
            sample.cpu_ms = ms * 0.4;
            sample.gpu_ms = ms * 0.3;
            sample.width = width;
            sample.height = 1080;
            sample.render_distance = rd;
            recording.frames.push_back(sample);
        }
    };
    push(12000, 5.0, 1920, 12);
    push(1200, 50.0, 1920, 12);
    push(6000, 5.0, 1280, 8);
    recording.seen = recording.frames.size();

    const std::string text = report(recording);
    check(text.find("per minute (3 of them)") != std::string::npos,
          "three minutes in the per-minute table");
    check(text.find("1920x1080, then 1280x1080 at 120 s") != std::string::npos,
          "the window change is named with the second it happened");
    check(text.find("12, then 8 at 120 s") != std::string::npos,
          "the render distance change is named");
    check(text.find("a played game, not the draw-path benchmark") != std::string::npos,
          "a live recording says what it is not");
    check(text.find("CapFrameX") != std::string::npos, "the 1% low definition is spelled out");

    const auto path = scratch_file("plasma-test-perf.png");
    check(write_graph(path, recording), "the graph writes");
    check(std::filesystem::exists(path) && std::filesystem::file_size(path) > 1000,
          "the graph is a real png");
    std::filesystem::remove(path);

    Recording empty;
    check(report(empty).find("no frames") != std::string::npos, "an empty recording says so");
    check(!write_graph(path, empty), "no graph for no frames");
}

// What a push costs, so the overhead claim in the report has a number behind
// it. Printed rather than asserted tightly: the bound is there to catch a push
// that started allocating, not to measure this machine.
void test_push_cost() {
    Recorder recorder;
    Context context;
    context.source = "test";
    recorder.open(65536, context);
    Sample sample;
    sample.wall_ms = 4.0;
    sample.sections = 300;
    constexpr size_t kPushes = 200000;
    const double before = thread_cpu_ms();
    for (size_t i = 0; i < kPushes; ++i) recorder.push(sample);
    const double after = thread_cpu_ms();
    const double ns = (after - before) * 1e6 / static_cast<double>(kPushes);
    std::cout << "perf: " << ns << " ns a push\n";
    check(ns < 2000.0, "a push stays far away from a millisecond");
    check(recorder.stored() == 65536 && recorder.seen() == kPushes, "the ring held");

    // What the client pays around a push as well: the two clock reads the
    // frame hook makes, which are the rest of the per-frame cost that can be
    // measured without a window.
    double paid = 0.0;
    const double clocked_before = thread_cpu_ms();
    for (size_t i = 0; i < kPushes; ++i) {
        const double at = thread_cpu_ms();
        recorder.push(sample);
        paid += thread_cpu_ms() - at;
    }
    const double clocked = (thread_cpu_ms() - clocked_before) * 1e6 /
                           static_cast<double>(kPushes);
    std::cout << "perf: " << clocked << " ns a push with the frame hook's two clock reads\n";
    check(paid >= 0.0, "the measured overhead is not negative");
    check(recorder.stored() == 65536 && recorder.seen() == 2 * kPushes, "the ring held");
}

} // namespace

int main() {
    test_known_series();
    test_definitions_disagree();
    test_helpers();
    test_ring();
    test_round_trip();
    test_report_and_graph();
    test_push_cost();
    if (failures == 0) std::cout << "perf: ok\n";
    return failures == 0 ? 0 : 1;
}
