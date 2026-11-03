#pragma once
#include "audio/pcm.hpp"
#include "audio/ring.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <semaphore>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace mcpp::audio {

// Reads a file out of the player's own assets, by its object store name.
using FileSource = std::function<std::optional<std::vector<uint8_t>>(std::string_view)>;

// A long sound on its way from the decoder to the mixer: a ring of decoded
// frames the decoder writes ahead of the mixer reading them.
struct StreamBuffer {
    static constexpr size_t kFrames = size_t{1} << 16;

    std::string file;
    bool looping = false;
    std::atomic<int> channels{0};
    std::atomic<int> rate{0};
    std::vector<int16_t> samples = std::vector<int16_t>(kFrames * 2);
    std::atomic<uint64_t> written{0};
    std::atomic<uint64_t> read{0};
    // The decoder has nothing more to give.
    std::atomic<bool> ended{false};
    // The mixer has let go; the decoder frees it.
    std::atomic<bool> closed{false};
    std::unique_ptr<OggStream> ogg;
};

struct Decoded {
    std::string file;
    std::shared_ptr<const Pcm> pcm;   // empty when the file is missing or broken
    double milliseconds = 0.0;
};

// Decodes whole sounds and keeps streams topped up on a thread of its own, so
// neither the frame nor the audio callback ever waits on a file or on Vorbis.
// Without a thread it does the same work when service() is called, which is
// how a test or a replay gets the same answer every time.
class Decoder {
public:
    Decoder(FileSource files, bool threaded);
    ~Decoder();
    Decoder(const Decoder&) = delete;
    Decoder& operator=(const Decoder&) = delete;

    // False when the queue is full and the job was not taken.
    bool decode(const std::string& file);
    // Takes the stream, unless it says false; it is freed on the decoder's
    // side once closed.
    bool open(StreamBuffer* stream);
    std::optional<Decoded> poll() { return results_.pop(); }
    // The work of one pass, for a decoder without a thread.
    void service();

private:
    struct Job {
        std::string file;
        StreamBuffer* stream = nullptr;
    };
    void run();
    void work(const Job& job);
    void refill();
    static void top_up(StreamBuffer& stream);

    FileSource files_;
    Ring<Job, 1024> jobs_;
    Ring<Decoded, 1024> results_;
    std::vector<StreamBuffer*> streams_;
    std::counting_semaphore<> wake_{0};
    std::atomic<bool> running_{true};
    std::thread thread_;
};

} // namespace mcpp::audio
