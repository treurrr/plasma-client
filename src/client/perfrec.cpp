// P: the frame recorder inside the running client. One timestamp a frame, the
// gpu timestamp the renderer already queries, and the cheap context that
// explains the frame, into a ring that was allocated when the recording
// started and never grows. Nothing here reads the clock twice for the same
// thing and nothing here formats a string while a recording is on.
#include "client/breaking.hpp"
#include "client/game.hpp"

#include "mcpp/features.hpp"
#include "mcpp/perf.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <format>
#include <iostream>
#include <string>
#include <string_view>

namespace client {
namespace {

// Twenty minutes at 200 fps, 16 MB of samples. A longer session keeps its
// newest frames and the report says how many fell out.
constexpr size_t kDefaultFrames = 262144;
constexpr int kToggleKey = GLFW_KEY_F10;

struct Rec {
    mcpp::perf::Recorder recorder;
    bool started = false;
    bool key_was_down = false;
    bool feature_was_on = false;
    bool have_previous = false;
    std::chrono::steady_clock::time_point previous{};
    std::chrono::steady_clock::time_point opened{};
    double previous_cpu_ms = 0.0;
    std::filesystem::path path;
    std::string wrote;
    std::chrono::steady_clock::time_point wrote_at{};
    double overhead_ms = 0.0;
    size_t overhead_frames = 0;
    size_t capacity = kDefaultFrames;
};

Rec& rec() {
    static Rec state;
    return state;
}

std::string stamp(const char* format) {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    char text[64] = {};
    std::strftime(text, sizeof(text), format, &local);
    return text;
}

std::filesystem::path default_directory() {
    if (const char* given = std::getenv("PLASMA_PERF_DIR"); given != nullptr && *given != '\0') {
        return given;
    }
    if (const char* data = std::getenv("XDG_DATA_HOME"); data != nullptr && *data != '\0') {
        return std::filesystem::path(data) / "plasma-client" / "perf";
    }
    const char* home = std::getenv("HOME");
    return std::filesystem::path(home != nullptr ? home : ".") / ".local" / "share" /
           "plasma-client" / "perf";
}

size_t capacity_from_environment() {
    if (const char* given = std::getenv("PLASMA_PERF_FRAMES"); given != nullptr) {
        const long long asked = std::strtoll(given, nullptr, 10);
        if (asked > 0) return static_cast<size_t>(asked);
    }
    return kDefaultFrames;
}

void begin(Game& game, const std::filesystem::path& path) {
    auto& state = rec();
    mcpp::perf::Context context;
    context.started = stamp("%Y-%m-%d %H:%M:%S");
    context.source = "live";
    context.server = std::format("{}:{}", game.address.host, game.address.port);
    context.dimension = game.session ? game.session->dimension().name : std::string{};
    context.profile = mcpp::features::Registry::instance().profile() ==
                              mcpp::features::Profile::Vanilla
                          ? "vanilla"
                          : "plasma";
    state.path = path;
    state.capacity = capacity_from_environment();
    state.recorder.open(state.capacity, std::move(context));
    state.have_previous = false;
    state.overhead_ms = 0.0;
    state.overhead_frames = 0;
    state.opened = std::chrono::steady_clock::now();
    state.wrote.clear();
    std::cout << std::format("- frame recorder on: up to {} frames, writing to {}\n",
                             state.capacity, path.string());
}

void end() {
    auto& state = rec();
    if (!state.recorder.recording()) return;
    state.recorder.close();
    const size_t kept = state.recorder.stored();
    const size_t dropped = state.recorder.dropped();
    const bool written = state.recorder.write(state.path);
    const double overhead_us =
        state.overhead_frames == 0
            ? 0.0
            : state.overhead_ms / static_cast<double>(state.overhead_frames) * 1000.0;
    if (written) {
        state.wrote = state.path.string();
        state.wrote_at = std::chrono::steady_clock::now();
        std::cout << std::format(
            "- frame recorder off: {} frames in {}{}; the recorder itself cost {:.2f} us of cpu "
            "a frame\n  read it with: plasma-client perfreport {}\n",
            kept, state.path.string(),
            dropped > 0 ? std::format(", {} older frames dropped", dropped) : std::string{},
            overhead_us, state.path.string());
    } else {
        std::cerr << std::format("- frame recorder: could not write {}\n", state.path.string());
    }
}

} // namespace

void perf_frame(Game& game) {
    auto& state = rec();
    auto& registry = mcpp::features::Registry::instance();
    if (!state.started) {
        state.started = true;
        // A recording never carries over from a config file: the card starts
        // off, whatever the last session left in it, so what the menu shows is
        // what is happening.
        registry.set(mcpp::features::Id::FrameRecorder, false);
        state.feature_was_on = false;
        const char* asked = std::getenv("PLASMA_PERF");
        if (asked != nullptr && *asked != '\0' && std::string_view(asked) != "0") {
            const std::string_view value(asked);
            const bool bare = value == "1" || value == "on" || value == "yes";
            begin(game, bare ? default_directory() /
                                   std::format("frames-{}.plperf", stamp("%Y%m%d-%H%M%S"))
                             : std::filesystem::path(value));
            registry.set(mcpp::features::Id::FrameRecorder, true);
            state.feature_was_on = true;
        }
    }

    // The key is the Plasma profile's; the vanilla profile draws nothing of
    // its own and takes no keys of its own, so there it is the environment
    // variable or the menu.
    const bool plasma = registry.profile() == mcpp::features::Profile::Plasma;
    const bool key_down = plasma && !game.window->typing() && game.window->key_down(kToggleKey);
    const bool pressed = key_down && !state.key_was_down;
    state.key_was_down = key_down;

    const bool feature_on = registry.wanted(mcpp::features::Id::FrameRecorder);
    const bool switched = feature_on != state.feature_was_on;
    state.feature_was_on = feature_on;

    const bool on = state.recorder.recording();
    if (pressed || (switched && feature_on != on)) {
        if (on) {
            end();
            registry.set(mcpp::features::Id::FrameRecorder, false);
            state.feature_was_on = false;
        } else {
            begin(game, default_directory() /
                            std::format("frames-{}.plperf", stamp("%Y%m%d-%H%M%S")));
            registry.set(mcpp::features::Id::FrameRecorder, true);
            state.feature_was_on = true;
        }
    }
    if (!state.recorder.recording()) {
        state.have_previous = false;
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    const double cpu_ms = mcpp::perf::thread_cpu_ms();
    if (!state.have_previous) {
        state.previous = now;
        state.previous_cpu_ms = cpu_ms;
        state.have_previous = true;
        return;
    }

    const auto& renderer = *game.renderer;
    const auto& cost = renderer.last_frame_cost();
    mcpp::perf::Sample sample;
    sample.wall_ms = std::chrono::duration<double, std::milli>(now - state.previous).count();
    sample.cpu_ms = cpu_ms - state.previous_cpu_ms;
    sample.gpu_ms = renderer.last_gpu_ms();
    sample.cost.sort = static_cast<float>(cost.sort_ms);
    sample.cost.sort_upload = static_cast<float>(cost.sort_upload_ms);
    sample.cost.sky = static_cast<float>(cost.sky_ms);
    sample.cost.overlay = static_cast<float>(cost.overlay_ms);
    sample.cost.geometry = static_cast<float>(cost.geometry_ms);
    sample.cost.record = static_cast<float>(cost.record_ms);
    sample.cost.submit = static_cast<float>(cost.submit_ms);
    sample.cost.readback = static_cast<float>(cost.readback_ms);
    sample.sections = static_cast<uint32_t>(renderer.drawn_sections());
    sample.entities = game.entities.scene
                          ? static_cast<uint32_t>(game.entities.scene->stats().drawn)
                          : 0u;
    sample.particles = static_cast<uint32_t>(breaking_particles(game));
    sample.width = static_cast<uint16_t>(std::min<uint32_t>(game.window->width(), 65535u));
    sample.height = static_cast<uint16_t>(std::min<uint32_t>(game.window->height(), 65535u));
    sample.render_distance =
        static_cast<uint8_t>(std::clamp(game.world.render_distance, 0, 255));
    if (game.screens.inventory_open) sample.flags |= mcpp::perf::kScreenOpen;
    if (game.window->typing()) sample.flags |= mcpp::perf::kTyping;
    if (game.screens.menu_open) sample.flags |= mcpp::perf::kFeatureMenu;
    state.recorder.push(sample);
    state.previous = now;
    state.previous_cpu_ms = cpu_ms;

    // What the recorder costs, measured rather than claimed. The two clock
    // reads that measure it are in it; the one before the gathering is not.
    state.overhead_ms += mcpp::perf::thread_cpu_ms() - cpu_ms;
    ++state.overhead_frames;
}

void perf_draw(Game& game) {
    auto& state = rec();
    if (!game.screens.hud) return;
    auto& hud = *game.screens.hud;
    const float width = static_cast<float>(game.window->width());
    const float scale = hud.scale_for(13.0f);
    if (state.recorder.recording()) {
        const double seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - state.opened).count();
        const auto line = std::format("REC {:02}:{:02}  {} frames", static_cast<int>(seconds) / 60,
                                      static_cast<int>(seconds) % 60, state.recorder.stored());
        const float text_width = hud.width(line, scale, mcpp::gpu::Style::Mono);
        const float x = width - text_width - 18.0f;
        hud.fill(x - 22.0f, 10.0f, text_width + 32.0f, 20.0f, 0x08080AC8u);
        // The dot blinks the way a recorder's does, once a second.
        const bool lit = static_cast<int>(seconds * 2.0) % 2 == 0;
        hud.fill(x - 14.0f, 16.0f, 8.0f, 8.0f, lit ? 0xE05252FFu : 0x5A2A2AFFu);
        hud.draw(line, x, 13.0f, scale, 0xF0F0F4FFu, mcpp::gpu::Style::Mono);
        return;
    }
    if (state.wrote.empty()) return;
    const double since =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - state.wrote_at).count();
    if (since > 12.0) {
        state.wrote.clear();
        return;
    }
    const auto line = std::format("frames written to {}", state.wrote);
    const float text_width = hud.width(line, scale, mcpp::gpu::Style::Mono);
    const float x = width - text_width - 18.0f;
    hud.fill(x - 10.0f, 10.0f, text_width + 20.0f, 20.0f, 0x08080AC8u);
    hud.draw(line, x, 13.0f, scale, 0xB8F0C0FFu, mcpp::gpu::Style::Mono);
}

void perf_stop(Game& game) {
    (void)game;
    end();
}

} // namespace client
