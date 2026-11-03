#pragma once
#include <cstdint>

namespace mcpp::audio {

// The linear congruential generator java.util.Random documents in its API: a
// 48-bit seed scrambled with 0x5DEECE66D, and nextInt(bound) with its
// power-of-two shortcut and its rejection loop. The sound packets' seed goes in
// here, so every client that follows the same steps hears the same variant.
class LegacyRandom {
public:
    explicit LegacyRandom(int64_t seed) { set_seed(seed); }

    void set_seed(int64_t seed) {
        seed_ = (static_cast<uint64_t>(seed) ^ kMultiplier) & kMask;
    }

    int32_t next(int bits) {
        seed_ = (seed_ * kMultiplier + kIncrement) & kMask;
        return static_cast<int32_t>(static_cast<uint32_t>(seed_ >> (48 - bits)));
    }

    int32_t next_int(int32_t bound) {
        if ((bound & -bound) == bound) {
            return static_cast<int32_t>((static_cast<int64_t>(bound) * next(31)) >> 31);
        }
        int32_t bits = 0;
        int32_t value = 0;
        do {
            bits = next(31);
            value = bits % bound;
        } while (static_cast<int32_t>(static_cast<uint32_t>(bits) - static_cast<uint32_t>(value) +
                                      static_cast<uint32_t>(bound - 1)) < 0);
        return value;
    }

    int64_t next_long() {
        const auto high = static_cast<int64_t>(next(32));
        return static_cast<int64_t>(static_cast<uint64_t>(high) << 32) + next(32);
    }

    float next_float() { return static_cast<float>(next(24)) / static_cast<float>(1 << 24); }

private:
    static constexpr uint64_t kMultiplier = 0x5DEECE66DULL;
    static constexpr uint64_t kIncrement = 0xBULL;
    static constexpr uint64_t kMask = (1ULL << 48) - 1;
    uint64_t seed_ = 0;
};

} // namespace mcpp::audio
