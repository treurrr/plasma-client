#include "mcpp/perf.hpp"

#include "mcpp/image_png.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <format>
#include <fstream>
#include <numeric>
#include <sstream>

namespace mcpp::perf {
namespace {

constexpr std::string_view kMagic = "plasma-perf 1";
constexpr size_t kColumns = 18;

double mean(std::span<const double> values) {
    if (values.empty()) return 0.0;
    return std::accumulate(values.begin(), values.end(), 0.0) /
           static_cast<double>(values.size());
}

std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    return text;
}

} // namespace

double thread_cpu_ms() {
    timespec now{};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now);
    return static_cast<double>(now.tv_sec) * 1000.0 + static_cast<double>(now.tv_nsec) / 1e6;
}

void Recorder::open(size_t capacity, Context context) {
    ring_.assign(std::max<size_t>(capacity, 16), Sample{});
    head_ = 0;
    stored_ = 0;
    seen_ = 0;
    context_ = std::move(context);
    context_.capacity = ring_.size();
    recording_ = true;
}

void Recorder::close() { recording_ = false; }

void Recorder::push(const Sample& sample) {
    if (!recording_) return;
    ring_[head_] = sample;
    head_ = head_ + 1 == ring_.size() ? 0 : head_ + 1;
    if (stored_ < ring_.size()) ++stored_;
    ++seen_;
}

std::vector<Sample> Recorder::frames() const {
    std::vector<Sample> out;
    out.reserve(stored_);
    if (ring_.empty()) return out;
    const size_t first = stored_ < ring_.size() ? 0 : head_;
    for (size_t i = 0; i < stored_; ++i) out.push_back(ring_[(first + i) % ring_.size()]);
    return out;
}

bool Recorder::write(const std::filesystem::path& path) const {
    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream file(path, std::ios::trunc);
    if (!file) return false;
    file << kMagic << '\n';
    const auto line = [&](std::string_view key, std::string_view value) {
        file << "# " << key << ' ' << (value.empty() ? "-" : value) << '\n';
    };
    line("started", context_.started);
    line("source", context_.source);
    line("server", context_.server);
    line("dimension", context_.dimension);
    line("profile", context_.profile);
    line("note", context_.note);
    file << std::format("# capacity {}\n", context_.capacity);
    file << std::format("# seen {}\n", seen_);
    file << std::format("# dropped {}\n", dropped());
    file << "# columns wall_ms cpu_ms gpu_ms sort sort_upload sky overlay geometry record submit"
            " readback sections entities particles width height rd flags\n";
    std::string out;
    out.reserve(128);
    if (!ring_.empty()) {
        const size_t first = stored_ < ring_.size() ? 0 : head_;
        for (size_t i = 0; i < stored_; ++i) {
            const Sample& s = ring_[(first + i) % ring_.size()];
            out.clear();
            std::format_to(std::back_inserter(out),
                           "{:.4f}\t{:.4f}\t{:.4f}\t{:.4f}\t{:.4f}\t{:.4f}\t{:.4f}\t{:.4f}"
                           "\t{:.4f}\t{:.4f}\t{:.4f}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\n",
                           s.wall_ms, s.cpu_ms, s.gpu_ms, s.cost.sort, s.cost.sort_upload,
                           s.cost.sky, s.cost.overlay, s.cost.geometry, s.cost.record,
                           s.cost.submit, s.cost.readback, s.sections, s.entities, s.particles,
                           s.width, s.height, static_cast<int>(s.render_distance),
                           static_cast<int>(s.flags));
            file << out;
        }
    }
    file.flush();
    return file.good();
}

std::optional<Recording> read(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) return std::nullopt;
    std::string line;
    if (!std::getline(file, line) || trim(line) != kMagic) return std::nullopt;
    Recording out;
    while (std::getline(file, line)) {
        const std::string_view text = trim(line);
        if (text.empty()) continue;
        if (text.front() == '#') {
            const std::string_view body = trim(text.substr(1));
            const size_t space = body.find(' ');
            if (space == std::string_view::npos) continue;
            const std::string_view key = body.substr(0, space);
            const std::string_view value = trim(body.substr(space + 1));
            const std::string held(value == "-" ? std::string_view{} : value);
            if (key == "started") {
                out.context.started = held;
            } else if (key == "source") {
                out.context.source = held;
            } else if (key == "server") {
                out.context.server = held;
            } else if (key == "dimension") {
                out.context.dimension = held;
            } else if (key == "profile") {
                out.context.profile = held;
            } else if (key == "note") {
                out.context.note = held;
            } else if (key == "capacity") {
                out.context.capacity = std::strtoull(held.c_str(), nullptr, 10);
            } else if (key == "seen") {
                out.seen = std::strtoull(held.c_str(), nullptr, 10);
            } else if (key == "dropped") {
                out.dropped = std::strtoull(held.c_str(), nullptr, 10);
            }
            continue;
        }
        double field[kColumns] = {};
        const char* at = line.c_str();
        size_t taken = 0;
        while (taken < kColumns && *at != '\0') {
            char* end = nullptr;
            const double value = std::strtod(at, &end);
            if (end == at) break;
            field[taken++] = value;
            at = end;
        }
        if (taken < kColumns) continue;
        Sample s;
        s.wall_ms = field[0];
        s.cpu_ms = field[1];
        s.gpu_ms = field[2];
        s.cost.sort = static_cast<float>(field[3]);
        s.cost.sort_upload = static_cast<float>(field[4]);
        s.cost.sky = static_cast<float>(field[5]);
        s.cost.overlay = static_cast<float>(field[6]);
        s.cost.geometry = static_cast<float>(field[7]);
        s.cost.record = static_cast<float>(field[8]);
        s.cost.submit = static_cast<float>(field[9]);
        s.cost.readback = static_cast<float>(field[10]);
        s.sections = static_cast<uint32_t>(field[11]);
        s.entities = static_cast<uint32_t>(field[12]);
        s.particles = static_cast<uint32_t>(field[13]);
        s.width = static_cast<uint16_t>(field[14]);
        s.height = static_cast<uint16_t>(field[15]);
        s.render_distance = static_cast<uint8_t>(field[16]);
        s.flags = static_cast<uint8_t>(field[17]);
        out.frames.push_back(s);
    }
    if (out.seen < out.frames.size()) out.seen = out.frames.size();
    return out;
}

double mean_of_slowest(std::span<const double> frame_ms, double fraction) {
    if (frame_ms.empty()) return 0.0;
    std::vector<double> sorted(frame_ms.begin(), frame_ms.end());
    std::ranges::sort(sorted);
    const size_t take = std::max<size_t>(
        1, static_cast<size_t>(fraction * static_cast<double>(sorted.size())));
    return mean(std::span<const double>(sorted).last(take));
}

double percentile_ms(std::span<const double> frame_ms, double quantile) {
    if (frame_ms.empty()) return 0.0;
    std::vector<double> sorted(frame_ms.begin(), frame_ms.end());
    std::ranges::sort(sorted);
    const size_t at = std::min(sorted.size() - 1,
                               static_cast<size_t>(quantile * static_cast<double>(sorted.size())));
    return sorted[at];
}

Summary summarise(std::span<const Sample> frames) {
    Summary out;
    out.frames = frames.size();
    if (frames.empty()) return out;
    std::vector<double> wall;
    std::vector<double> cpu;
    std::vector<double> gpu;
    wall.reserve(frames.size());
    cpu.reserve(frames.size());
    gpu.reserve(frames.size());
    double cost[8] = {};
    out.sections_min = out.sections_max = frames.front().sections;
    out.entities_min = out.entities_max = frames.front().entities;
    for (const auto& frame : frames) {
        wall.push_back(frame.wall_ms);
        cpu.push_back(frame.cpu_ms);
        gpu.push_back(frame.gpu_ms);
        cost[0] += frame.cost.sort;
        cost[1] += frame.cost.sort_upload;
        cost[2] += frame.cost.sky;
        cost[3] += frame.cost.overlay;
        cost[4] += frame.cost.geometry;
        cost[5] += frame.cost.record;
        cost[6] += frame.cost.submit;
        cost[7] += frame.cost.readback;
        out.sections_min = std::min(out.sections_min, frame.sections);
        out.sections_max = std::max(out.sections_max, frame.sections);
        out.entities_min = std::min(out.entities_min, frame.entities);
        out.entities_max = std::max(out.entities_max, frame.entities);
        out.particles_max = std::max(out.particles_max, frame.particles);
        if ((frame.flags & kScreenOpen) != 0) ++out.frames_with_screen;
    }
    const double count = static_cast<double>(frames.size());
    out.seconds = std::accumulate(wall.begin(), wall.end(), 0.0) / 1000.0;
    out.avg_fps = 1000.0 / mean(wall);
    out.low_1_fps = 1000.0 / mean_of_slowest(wall, 0.01);
    out.low_01_fps = 1000.0 / mean_of_slowest(wall, 0.001);
    out.p99_ms = percentile_ms(wall, 0.99);
    out.median_ms = percentile_ms(wall, 0.5);
    out.worst_ms = *std::ranges::max_element(wall);
    out.cpu_ms = mean(cpu);
    out.gpu_ms = mean(gpu);
    out.cost.sort = static_cast<float>(cost[0] / count);
    out.cost.sort_upload = static_cast<float>(cost[1] / count);
    out.cost.sky = static_cast<float>(cost[2] / count);
    out.cost.overlay = static_cast<float>(cost[3] / count);
    out.cost.geometry = static_cast<float>(cost[4] / count);
    out.cost.record = static_cast<float>(cost[5] / count);
    out.cost.submit = static_cast<float>(cost[6] / count);
    out.cost.readback = static_cast<float>(cost[7] / count);
    return out;
}

} // namespace mcpp::perf
