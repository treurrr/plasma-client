#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace mcpp::audio {

// The sound categories in the order the protocol numbers them, which is also
// the order of the soundCategory_* lines in options.txt.
enum class Category : uint8_t {
    Master, Music, Record, Weather, Block, Hostile, Neutral, Player, Ambient, Voice, Ui
};
constexpr size_t kCategoryCount = 11;

constexpr std::array<std::string_view, kCategoryCount> kCategoryNames = {
    "master", "music", "record", "weather", "block", "hostile",
    "neutral", "player", "ambient", "voice", "ui"};

constexpr std::string_view category_name(Category category) {
    return kCategoryNames[static_cast<size_t>(category)];
}

inline std::optional<Category> category_from_id(int32_t id) {
    if (id < 0 || id >= static_cast<int32_t>(kCategoryCount)) return std::nullopt;
    return static_cast<Category>(id);
}

inline std::optional<Category> category_from_name(std::string_view name) {
    for (size_t i = 0; i < kCategoryCount; ++i) {
        if (kCategoryNames[i] == name) return static_cast<Category>(i);
    }
    return std::nullopt;
}

// The volume sliders. The options screen sets these; every voice multiplies
// master by its own category, and master by itself only once.
struct CategoryVolumes {
    std::array<float, kCategoryCount> volume{1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};

    float& operator[](Category category) { return volume[static_cast<size_t>(category)]; }
    float operator[](Category category) const { return volume[static_cast<size_t>(category)]; }

    float gain(Category category) const {
        const float master = (*this)[Category::Master];
        return category == Category::Master ? master : master * (*this)[category];
    }
};

} // namespace mcpp::audio
