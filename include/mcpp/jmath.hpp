#pragma once
#include <array>
#include <cmath>
#include <cstdint>

// The game's own arithmetic where it differs from the exact one. Movement,
// particles and sound picks run through these on the server too, so a client
// that uses std::sin or its own random numbers drifts from what the server
// predicts, one rounding at a time.
namespace mcpp::jmath {

// Sine from a 65536-entry table of floats, indexed by the truncated angle.
class SinTable {
public:
    SinTable() {
        for (size_t i = 0; i < values_.size(); ++i) {
            values_[i] = static_cast<float>(std::sin(static_cast<double>(i) / kScale));
        }
    }
    float sin(double radians) const { return values_[index(radians * kScale)]; }
    float cos(double radians) const { return values_[index(radians * kScale + 16384.0)]; }

private:
    static constexpr double kScale = 10430.378350470453;
    static size_t index(double scaled) {
        return static_cast<size_t>(static_cast<int64_t>(scaled) & 65535);
    }
    std::array<float, 65536> values_{};
};

inline const SinTable& sin_table() {
    static const SinTable table;
    return table;
}
inline float sin(double radians) { return sin_table().sin(radians); }
inline float cos(double radians) { return sin_table().cos(radians); }

// java.util.Random's generator, which the game uses for everything seeded by
// a packet: which variant of a sound plays, how particles scatter.
class LegacyRandom {
public:
    explicit LegacyRandom(int64_t seed) { set_seed(seed); }

    void set_seed(int64_t seed) { seed_ = (static_cast<uint64_t>(seed) ^ kMultiplier) & kMask; }

    int32_t next(int bits) {
        seed_ = (seed_ * kMultiplier + kIncrement) & kMask;
        return static_cast<int32_t>(static_cast<int64_t>(seed_) >> (48 - bits));
    }

    int32_t next_int() { return next(32); }

    int32_t next_int(int32_t bound) {
        if ((bound & (bound - 1)) == 0) {
            return static_cast<int32_t>((static_cast<int64_t>(bound) * next(31)) >> 31);
        }
        int32_t bits;
        int32_t value;
        do {
            bits = next(31);
            value = bits % bound;
            // Java's int wraps here; the loop rejects the uneven tail.
        } while (static_cast<int32_t>(static_cast<uint32_t>(bits) - static_cast<uint32_t>(value) +
                                      static_cast<uint32_t>(bound - 1)) < 0);
        return value;
    }

    int64_t next_long() {
        const int64_t high = next(32);
        const int64_t low = next(32);
        return static_cast<int64_t>(static_cast<uint64_t>(high) << 32) + low;
    }

    bool next_bool() { return next(1) != 0; }
    float next_float() { return static_cast<float>(next(24)) * 5.9604645e-8f; }

    // The game multiplies by a float constant, not by 2^-53 the way
    // java.util.Random does, so its doubles differ in the last bits.
    double next_double() {
        const int64_t high = next(26);
        const int64_t low = next(27);
        return static_cast<double>((high << 27) + low) * static_cast<double>(1.110223e-16f);
    }

private:
    static constexpr uint64_t kMultiplier = 25214903917ull;
    static constexpr uint64_t kIncrement = 11ull;
    static constexpr uint64_t kMask = (1ull << 48) - 1;
    uint64_t seed_ = 0;
};

} // namespace mcpp::jmath
