#include "mcpp/jmath.hpp"

#include <cmath>
#include <format>
#include <iostream>
#include <numbers>
#include <string_view>

namespace {

int failures = 0;

void check(bool condition, std::string_view what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << '\n';
        ++failures;
    }
}

// Values java.util.Random gives for these seeds, which the game's generator
// has to reproduce bit for bit.
void test_random() {
    mcpp::jmath::LegacyRandom a(42);
    const int32_t first = a.next_int();
    check(first == -1170105035, std::format("Random(42).nextInt() is {}", first));

    mcpp::jmath::LegacyRandom b(0);
    const int64_t wide = b.next_long();
    check(wide == -4962768465676381896LL, std::format("Random(0).nextLong() is {}", wide));

    mcpp::jmath::LegacyRandom c(42);
    const float f = c.next_float();
    check(f == 0.7275637f, std::format("Random(42).nextFloat() is {:.9g}", f));

    mcpp::jmath::LegacyRandom d(42);
    const int32_t bounded = d.next_int(10);
    check(bounded == 0, std::format("Random(42).nextInt(10) is {}", bounded));

    mcpp::jmath::LegacyRandom e(42);
    const int32_t power = e.next_int(16);
    check(power == 11, std::format("Random(42).nextInt(16) is {}", power));
}

void test_sin_table() {
    check(mcpp::jmath::sin(0.0) == 0.0f, "sin 0 is 0");
    check(mcpp::jmath::sin(std::numbers::pi / 2.0) == 1.0f, "sin of a right angle is 1");
    check(mcpp::jmath::cos(0.0) == 1.0f, "cos 0 is 1");
    // The table only knows 65536 angles, so most answers are the sine of a
    // slightly smaller angle than asked for.
    const float table = mcpp::jmath::sin(0.3);
    const auto exact = static_cast<float>(std::sin(0.3));
    check(table < exact && exact - table < 1e-4f,
          std::format("sin 0.3 comes from the table: {:.9g} against {:.9g}", table, exact));
    check(std::abs(mcpp::jmath::sin(-0.3) + table) < 1e-6f,
          std::format("negative angles wrap into the table: {:.9g}", mcpp::jmath::sin(-0.3)));
}

} // namespace

int main() {
    test_random();
    test_sin_table();
    if (failures == 0) {
        std::cout << "all jmath tests passed\n";
        return 0;
    }
    return 1;
}
