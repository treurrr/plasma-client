#include "mcpp/jmath.hpp"

#include <bit>
#include <cstdint>
#include <iostream>
#include <numbers>

// The C++ half of MathOracle: the same inputs in the same order, values only.
int main() {
    const double angles[] = {0.0, 0.3, -0.3, 1.0, std::numbers::pi / 2, std::numbers::pi,
                             2.5, -7.25, 100.0, 12345.678};
    for (const double a : angles) {
        std::cout << std::bit_cast<int32_t>(mcpp::jmath::sin(a)) << '\n';
        std::cout << std::bit_cast<int32_t>(mcpp::jmath::cos(a)) << '\n';
    }
    const int64_t seeds[] = {0, 42, -1, 123456789, INT64_MIN};
    for (const int64_t seed : seeds) {
        mcpp::jmath::LegacyRandom r(seed);
        std::cout << r.next_int() << '\n';
        std::cout << r.next_int(10) << '\n';
        std::cout << r.next_int(16) << '\n';
        std::cout << r.next_long() << '\n';
        std::cout << std::bit_cast<int32_t>(r.next_float()) << '\n';
        std::cout << std::bit_cast<int64_t>(r.next_double()) << '\n';
        std::cout << (r.next_bool() ? "true" : "false") << '\n';
    }
}
