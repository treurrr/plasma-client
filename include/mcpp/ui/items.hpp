#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// What a screen needs to know of each item, read from the table
// tools/bake_gui.py writes out of the game's own item report.
namespace mcpp::ui {

class ItemTable {
public:
    enum class Slot : uint8_t { None, Head, Chest, Legs, Feet, Body, Offhand, Saddle };
    enum class Rarity : uint8_t { Common, Uncommon, Rare, Epic };

    bool load(const std::filesystem::path& path);
    static std::filesystem::path default_path(int32_t protocol);

    size_t size() const { return rows_.size(); }
    std::string_view name(int32_t id) const;
    std::string_view name_key(int32_t id) const;
    int max_stack(int32_t id) const;
    int max_damage(int32_t id) const;
    Slot slot(int32_t id) const;
    Rarity rarity(int32_t id) const;
    // Id by registry name without the namespace, or -1.
    int32_t find(std::string_view name) const;

    // The colour a name of this rarity is drawn in, 0xRRGGBB.
    static uint32_t rarity_colour(Rarity rarity);

private:
    struct Row {
        std::string name;
        std::string key;
        int max_stack = 64;
        int max_damage = 0;
        Slot slot = Slot::None;
        Rarity rarity = Rarity::Common;
    };
    const Row* row(int32_t id) const;
    std::vector<Row> rows_;
};

} // namespace mcpp::ui
