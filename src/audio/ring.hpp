#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <optional>

namespace mcpp::audio {

// One writer thread, one reader thread, no locks: how the game's thread talks
// to the audio callback and to the decoder.
template <typename T, size_t Capacity>
class Ring {
    static_assert((Capacity & (Capacity - 1)) == 0);

public:
    bool push(const T& value) {
        const size_t head = head_.load(std::memory_order_relaxed);
        if (head - tail_.load(std::memory_order_acquire) == Capacity) return false;
        slots_[head & (Capacity - 1)] = value;
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    std::optional<T> pop() {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) return std::nullopt;
        T value = slots_[tail & (Capacity - 1)];
        tail_.store(tail + 1, std::memory_order_release);
        return value;
    }

    bool empty() const {
        return tail_.load(std::memory_order_acquire) == head_.load(std::memory_order_acquire);
    }

private:
    std::array<T, Capacity> slots_{};
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
};

} // namespace mcpp::audio
