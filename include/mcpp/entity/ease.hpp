#pragma once
#include "mcpp/jmath.hpp"

#include <cmath>

// `Ease`: the game's easing curves, as the animations and the spear ask for
// them. Every one is single precision in, single precision out, with the
// double-precision steps the game takes through `Math.pow` and `Math.sin`
// kept double, and the ones it takes through `Mth.sin`/`Mth.cos` going
// through the game's sine table.
namespace mcpp::entity::ease {

inline float square(float x) { return x * x; }
inline float cube(float x) { return x * x * x; }

inline float in_back(float x) { return square(x) * (2.70158f * x - 1.70158f); }

inline float out_bounce(float x) {
    if (x < 0.36363637f) return 7.5625f * square(x);
    if (x < 0.72727275f) return 7.5625f * square(x - 0.54545456f) + 0.75f;
    if (x < 0.9090909090909091) return 7.5625f * square(x - 0.8181818f) + 0.9375f;
    return 7.5625f * square(x - 0.95454544f) + 0.984375f;
}

inline float in_bounce(float x) { return 1.0f - out_bounce(1.0f - x); }
inline float in_cubic(float x) { return cube(x); }

inline float in_elastic(float x) {
    if (x == 0.0f) return 0.0f;
    if (x == 1.0f) return 1.0f;
    constexpr float k = static_cast<float>(M_PI * 2.0 / 3.0);
    return static_cast<float>(-std::pow(2.0, 10.0 * x - 10.0) *
                              std::sin((x * 10.0 - 10.75) * k));
}

inline float in_expo(float x) {
    return x == 0.0f ? 0.0f : static_cast<float>(std::pow(2.0, 10.0 * x - 10.0));
}

inline float in_quart(float x) { return square(square(x)); }
inline float in_quint(float x) { return square(square(x)) * x; }
inline float in_sine(float x) { return 1.0f - jmath::cos(x * static_cast<float>(M_PI / 2)); }

inline float in_out_bounce(float x) {
    return x < 0.5f ? (1.0f - out_bounce(1.0f - 2.0f * x)) / 2.0f
                    : (1.0f + out_bounce(2.0f * x - 1.0f)) / 2.0f;
}

inline float in_out_circ(float x) {
    return x < 0.5f
               ? static_cast<float>((1.0 - std::sqrt(1.0 - std::pow(2.0 * x, 2.0))) / 2.0)
               : static_cast<float>((std::sqrt(1.0 - std::pow(-2.0 * x + 2.0, 2.0)) + 1.0) / 2.0);
}

inline float in_out_cubic(float x) {
    return x < 0.5f ? 4.0f * cube(x)
                    : static_cast<float>(1.0 - std::pow(-2.0 * x + 2.0, 3.0) / 2.0);
}

inline float in_out_quad(float x) {
    return x < 0.5f ? 2.0f * square(x)
                    : static_cast<float>(1.0 - std::pow(-2.0 * x + 2.0, 2.0) / 2.0);
}

inline float in_out_quart(float x) {
    return x < 0.5f ? 8.0f * square(square(x))
                    : static_cast<float>(1.0 - std::pow(-2.0 * x + 2.0, 4.0) / 2.0);
}

inline float in_out_quint(float x) {
    return x < 0.5 ? 16.0f * x * x * x * x * x
                   : static_cast<float>(1.0 - std::pow(-2.0 * x + 2.0, 5.0) / 2.0);
}

inline float out_elastic(float x) {
    constexpr float k = static_cast<float>(M_PI * 2.0 / 3.0);
    if (x == 0.0f) return 0.0f;
    if (x == 1.0f) return 1.0f;
    return static_cast<float>(std::pow(2.0, -10.0 * x) * std::sin((x * 10.0 - 0.75) * k) + 1.0);
}

inline float out_expo(float x) {
    return x == 1.0f ? 1.0f : 1.0f - static_cast<float>(std::pow(2.0, -10.0 * x));
}

inline float out_quad(float x) { return 1.0f - square(1.0f - x); }
inline float out_quint(float x) { return 1.0f - static_cast<float>(std::pow(1.0 - x, 5.0)); }
inline float out_sine(float x) { return jmath::sin(x * static_cast<float>(M_PI / 2)); }

inline float in_out_sine(float x) {
    return -(jmath::cos(static_cast<float>(M_PI) * x) - 1.0f) / 2.0f;
}

inline float out_back(float x) {
    return 1.0f + 2.70158f * cube(x - 1.0f) + 1.70158f * square(x - 1.0f);
}

inline float out_quart(float x) { return 1.0f - square(square(1.0f - x)); }
inline float out_cubic(float x) { return 1.0f - cube(1.0f - x); }

inline float in_out_expo(float x) {
    if (x < 0.5f) {
        return x == 0.0f ? 0.0f : static_cast<float>(std::pow(2.0, 20.0 * x - 10.0) / 2.0);
    }
    return x == 1.0f ? 1.0f : static_cast<float>((2.0 - std::pow(2.0, -20.0 * x + 10.0)) / 2.0);
}

inline float in_quad(float x) { return x * x; }
inline float out_circ(float x) { return static_cast<float>(std::sqrt(1.0f - square(x - 1.0f))); }

inline float in_out_elastic(float x) {
    constexpr float k = static_cast<float>(M_PI) * 4.0f / 9.0f;
    if (x == 0.0f) return 0.0f;
    if (x == 1.0f) return 1.0f;
    const double s = std::sin((20.0 * x - 11.125) * k);
    return x < 0.5f ? static_cast<float>(-(std::pow(2.0, 20.0 * x - 10.0) * s) / 2.0)
                    : static_cast<float>(std::pow(2.0, -20.0 * x + 10.0) * s / 2.0 + 1.0);
}

inline float in_circ(float x) {
    return static_cast<float>(-std::sqrt(1.0f - x * x)) + 1.0f;
}

inline float in_out_back(float x) {
    if (x < 0.5f) return 4.0f * x * x * (7.189819f * x - 2.5949094f) / 2.0f;
    const float t = 2.0f * x - 2.0f;
    return (t * t * (3.5949094f * t + 2.5949094f) + 2.0f) / 2.0f;
}

} // namespace mcpp::entity::ease
