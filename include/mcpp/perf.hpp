#pragma once
// What the client's frames cost while someone is playing, recorded in the
// running client and read back by `plasma-client perfreport`.
//
// worldbench measures the draw path over a world that is already uploaded; a
// recording from here measures a game, with the server, the physics, the
// particles and whatever the player is doing in it. The two are not the same
// number and the report says so, which is why a recording carries the context
// of the session it came from rather than frame times on their own.
#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace mcpp::perf {

// The renderer's own cpu split, in the order `WorldRenderer::FrameCost` counts
// it. Floats: a frame's parts are tenths of a millisecond and the recording is
// one of these per frame.
struct Cost {
    float sort = 0.0f;
    float sort_upload = 0.0f;
    float sky = 0.0f;
    float overlay = 0.0f;
    float geometry = 0.0f;
    float record = 0.0f;
    float submit = 0.0f;
    float readback = 0.0f;
};

enum Flag : uint8_t {
    kScreenOpen = 1u << 0,
    kTyping = 1u << 1,
    kFeatureMenu = 1u << 2,
};

// One frame. Fixed size, no owned memory: the ring holds these by value.
struct Sample {
    double wall_ms = 0.0;
    double cpu_ms = 0.0;
    double gpu_ms = 0.0;
    Cost cost;
    uint32_t sections = 0;
    uint32_t entities = 0;
    uint32_t particles = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    uint8_t render_distance = 0;
    uint8_t flags = 0;
};

// What the session was, taken once when the recording starts. A frame time
// without this is a number nobody can place.
struct Context {
    std::string started;     // local time, when the recording was opened
    std::string source;      // "live", or the tool that recorded it
    std::string server;      // host:port, or what the tool replayed
    std::string dimension;
    std::string profile;     // plasma or vanilla
    std::string note;        // whatever the caller wants kept with it
    size_t capacity = 0;
};

// A ring of frames with its memory taken once. push() never allocates, never
// locks and never touches the clock: the caller has already read it.
class Recorder {
public:
    void open(size_t capacity, Context context);
    void close();
    bool recording() const { return recording_; }

    void push(const Sample& sample);

    const Context& context() const { return context_; }
    // Frames handed to push(), frames still held, and frames the ring dropped
    // because the session outlasted it.
    size_t seen() const { return seen_; }
    size_t stored() const { return stored_; }
    size_t dropped() const { return seen_ > stored_ ? seen_ - stored_ : 0; }

    // Oldest first. Allocates, so it is for writing and for tests, never for
    // a frame.
    std::vector<Sample> frames() const;
    bool write(const std::filesystem::path& path) const;

private:
    std::vector<Sample> ring_;
    size_t head_ = 0;
    size_t stored_ = 0;
    size_t seen_ = 0;
    bool recording_ = false;
    Context context_;
};

// This thread's own cpu time, the clock worldbench's `cpu ms` column uses:
// the wait for the gpu is not in it.
double thread_cpu_ms();

// A recording read back.
struct Recording {
    Context context;
    size_t seen = 0;
    size_t dropped = 0;
    std::vector<Sample> frames;
};
std::optional<Recording> read(const std::filesystem::path& path);

// The two definitions of a low that tools disagree about, both spelled out
// here so a column can say which one it is.
//
//   mean_of_slowest(frames, 0.01) is the mean frame time of the slowest 1% of
//   frames. "1% low fps" below is 1000 / that, the same definition worldbench
//   prints and the one CapFrameX calls a 1% low average.
//
//   percentile_ms(frames, 0.99) is one frame's time: the frame at index
//   floor(0.99 * n) of the frames sorted fastest first. That is the p99 column.
double mean_of_slowest(std::span<const double> frame_ms, double fraction);
double percentile_ms(std::span<const double> frame_ms, double quantile);

// A stretch of frames, in the columns worldbench prints.
struct Summary {
    size_t frames = 0;
    double seconds = 0.0;
    double avg_fps = 0.0;
    double low_1_fps = 0.0;
    double low_01_fps = 0.0;
    double p99_ms = 0.0;
    double median_ms = 0.0;
    double worst_ms = 0.0;
    double cpu_ms = 0.0;
    double gpu_ms = 0.0;
    Cost cost;
    // What the frames held, so the number can be placed.
    uint32_t sections_min = 0;
    uint32_t sections_max = 0;
    uint32_t entities_min = 0;
    uint32_t entities_max = 0;
    uint32_t particles_max = 0;
    size_t frames_with_screen = 0;
};
Summary summarise(std::span<const Sample> frames);

// The whole session and then a row a minute, with what the session held and
// what it cannot be compared with.
std::string report(const Recording& recording);
// Frame time against time, with the minute boundaries and the lows marked.
bool write_graph(const std::filesystem::path& path, const Recording& recording);

} // namespace mcpp::perf
