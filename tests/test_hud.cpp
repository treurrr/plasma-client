#include "mcpp/ui/hud/hud.hpp"

#include <format>
#include <iostream>
#include <string_view>

namespace {

int failures = 0;

void check(bool condition, std::string_view what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << '\n';
        ++failures;
    }
}

// Rows and spacing of the heart rows, worked out by hand from the game's
// rule: one row per twenty points of health and absorption together, packed
// one pixel tighter for each row past the second, never under three.
void test_heart_layout() {
    using mcpp::ui::hud::heart_layout;
    const auto plain = heart_layout(20.0f, 20, 20, 0.0f);
    check(plain.rows == 1 && plain.max_hearts == 10 && plain.absorption_hearts == 0,
          "20 health is one row of ten");
    const auto golden = heart_layout(20.0f, 20, 20, 4.0f);
    check(golden.rows == 2 && golden.row_height == 10 && golden.absorption_hearts == 2,
          std::format("a golden apple adds a second row: {} rows of {}", golden.rows,
                      golden.row_height));
    const auto big = heart_layout(40.0f, 33, 33, 16.0f);
    check(big.rows == 3 && big.row_height == 9 && big.max_hearts == 20,
          std::format("40 + 16 is three rows of 9: {} rows of {}", big.rows, big.row_height));
    const auto huge = heart_layout(1024.0f, 1024, 1024, 0.0f);
    check(huge.row_height == 3, "rows never pack closer than three pixels");
    const auto odd = heart_layout(20.0f, 20, 20, 3.0f);
    check(odd.absorption_hearts == 2, "three absorption points round up to two hearts");
    const auto display = heart_layout(20.0f, 18, 26, 0.0f);
    check(display.max_hearts == 13, "the hearts shown while blinking count toward the row");
}

void test_chat_size() {
    using mcpp::ui::hud::chat_height;
    using mcpp::ui::hud::chat_width;
    check(chat_width(1.0) == 320, "full chat width is 320");
    check(chat_width(0.0) == 40, "narrowest chat is 40");
    check(chat_width(0.5) == 180, "half chat width is 180");
    check(chat_height(1.0) == 180, "focused chat is 180 tall");
    check(chat_height(0.4375) == 90, "unfocused chat is 90 tall by default");
}

void test_effects() {
    using mcpp::ui::hud::effect_info;
    check(effect_info(0) != nullptr && std::string_view(effect_info(0)->name) == "speed",
          "effect 0 is speed");
    check(effect_info(18) != nullptr && std::string_view(effect_info(18)->name) == "poison" &&
              !effect_info(18)->beneficial,
          "effect 18 is poison and harmful");
    check(effect_info(39) != nullptr &&
              std::string_view(effect_info(39)->name) == "breath_of_the_nautilus",
          "the last effect is breath of the nautilus");
    check(effect_info(40) == nullptr, "there are forty effects");
}

} // namespace

int main() {
    test_heart_layout();
    test_chat_size();
    test_effects();
    if (failures == 0) std::cout << "hud: all passed\n";
    return failures == 0 ? 0 : 1;
}
