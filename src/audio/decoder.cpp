#include "audio/decoder.hpp"

#include <algorithm>
#include <chrono>

namespace mcpp::audio {

Decoder::Decoder(FileSource files, bool threaded) : files_(std::move(files)) {
    if (threaded) thread_ = std::thread([this] { run(); });
}

Decoder::~Decoder() {
    running_.store(false);
    wake_.release();
    if (thread_.joinable()) thread_.join();
    while (auto job = jobs_.pop()) delete job->stream;
    for (auto* stream : streams_) delete stream;
}

bool Decoder::decode(const std::string& file) {
    if (!jobs_.push({file, nullptr})) return false;
    wake_.release();
    return true;
}

bool Decoder::open(StreamBuffer* stream) {
    if (!jobs_.push({{}, stream})) return false;
    wake_.release();
    return true;
}

void Decoder::run() {
    while (running_.load()) {
        service();
        wake_.try_acquire_for(streams_.empty() ? std::chrono::milliseconds(500)
                                               : std::chrono::milliseconds(5));
    }
}

void Decoder::service() {
    while (auto job = jobs_.pop()) work(*job);
    refill();
}

void Decoder::work(const Job& job) {
    if (job.stream != nullptr) {
        auto& stream = *job.stream;
        if (auto bytes = files_ ? files_(stream.file) : std::nullopt) {
            stream.ogg = OggStream::open(std::move(*bytes));
        }
        if (stream.ogg && stream.ogg->channels() >= 1 && stream.ogg->channels() <= 2) {
            stream.channels.store(stream.ogg->channels());
            stream.rate.store(stream.ogg->rate());
        } else {
            stream.ogg.reset();
            stream.ended.store(true);
        }
        streams_.push_back(job.stream);
        return;
    }
    const auto start = std::chrono::steady_clock::now();
    std::shared_ptr<const Pcm> pcm;
    if (auto bytes = files_ ? files_(job.file) : std::nullopt) {
        if (auto decoded = decode_ogg(*bytes)) pcm = std::make_shared<Pcm>(std::move(*decoded));
    }
    const double ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    while (!results_.push({job.file, pcm, ms})) {
        if (!running_.load()) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void Decoder::refill() {
    std::erase_if(streams_, [](StreamBuffer* stream) {
        if (!stream->closed.load(std::memory_order_acquire)) return false;
        delete stream;
        return true;
    });
    for (auto* stream : streams_) top_up(*stream);
}

void Decoder::top_up(StreamBuffer& stream) {
    if (!stream.ogg || stream.ended.load(std::memory_order_relaxed)) return;
    constexpr size_t kChunk = 4096;
    const auto channels = static_cast<size_t>(stream.ogg->channels());
    int16_t chunk[kChunk * 2];
    bool rewound = false;
    while (true) {
        const uint64_t written = stream.written.load(std::memory_order_relaxed);
        const uint64_t room = StreamBuffer::kFrames - (written - stream.read.load(std::memory_order_acquire));
        if (room < kChunk) return;
        const size_t got = stream.ogg->read(std::span(chunk, kChunk * channels));
        if (got == 0) {
            if (!stream.looping || rewound) {
                stream.ended.store(true, std::memory_order_release);
                return;
            }
            stream.ogg->rewind();
            rewound = true;
            continue;
        }
        rewound = false;
        for (size_t i = 0; i < got; ++i) {
            const size_t at = static_cast<size_t>((written + i) & (StreamBuffer::kFrames - 1)) * channels;
            std::copy_n(chunk + i * channels, channels, stream.samples.data() + at);
        }
        stream.written.store(written + got, std::memory_order_release);
    }
}

} // namespace mcpp::audio
