#include "mcpp/ui/items.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace mcpp::ui {
namespace {

ItemTable::Slot slot_of(std::string_view name) {
    if (name == "head") return ItemTable::Slot::Head;
    if (name == "chest") return ItemTable::Slot::Chest;
    if (name == "legs") return ItemTable::Slot::Legs;
    if (name == "feet") return ItemTable::Slot::Feet;
    if (name == "body") return ItemTable::Slot::Body;
    if (name == "offhand") return ItemTable::Slot::Offhand;
    if (name == "saddle") return ItemTable::Slot::Saddle;
    return ItemTable::Slot::None;
}

ItemTable::Rarity rarity_of(std::string_view name) {
    if (name == "uncommon") return ItemTable::Rarity::Uncommon;
    if (name == "rare") return ItemTable::Rarity::Rare;
    if (name == "epic") return ItemTable::Rarity::Epic;
    return ItemTable::Rarity::Common;
}

} // namespace

std::filesystem::path ItemTable::default_path(int32_t protocol) {
    const char* xdg = std::getenv("XDG_DATA_HOME");
    const char* home = std::getenv("HOME");
    const std::filesystem::path base =
        xdg != nullptr && *xdg != '\0'
            ? std::filesystem::path(xdg)
            : std::filesystem::path(home != nullptr ? home : ".") / ".local" / "share";
    return base / "plasma-client" / "data" / ("items-ui-" + std::to_string(protocol) + ".txt");
}

bool ItemTable::load(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) return false;
    rows_.clear();
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream fields(line);
        Row row;
        std::string stack, damage, slot, rarity;
        std::getline(fields, row.name, '\t');
        std::getline(fields, row.key, '\t');
        std::getline(fields, stack, '\t');
        std::getline(fields, damage, '\t');
        std::getline(fields, slot, '\t');
        std::getline(fields, rarity, '\t');
        if (row.key == "-") row.key.clear();
        row.max_stack = stack.empty() ? 64 : std::atoi(stack.c_str());
        row.max_damage = std::atoi(damage.c_str());
        row.slot = slot_of(slot);
        row.rarity = rarity_of(rarity);
        rows_.push_back(std::move(row));
    }
    return !rows_.empty();
}

const ItemTable::Row* ItemTable::row(int32_t id) const {
    if (id < 0 || static_cast<size_t>(id) >= rows_.size()) return nullptr;
    return &rows_[static_cast<size_t>(id)];
}

std::string_view ItemTable::name(int32_t id) const {
    const Row* r = row(id);
    return r != nullptr ? std::string_view(r->name) : std::string_view{};
}

std::string_view ItemTable::name_key(int32_t id) const {
    const Row* r = row(id);
    return r != nullptr ? std::string_view(r->key) : std::string_view{};
}

int ItemTable::max_stack(int32_t id) const {
    const Row* r = row(id);
    return r != nullptr ? r->max_stack : 64;
}

int ItemTable::max_damage(int32_t id) const {
    const Row* r = row(id);
    return r != nullptr ? r->max_damage : 0;
}

ItemTable::Slot ItemTable::slot(int32_t id) const {
    const Row* r = row(id);
    return r != nullptr ? r->slot : Slot::None;
}

ItemTable::Rarity ItemTable::rarity(int32_t id) const {
    const Row* r = row(id);
    return r != nullptr ? r->rarity : Rarity::Common;
}

int32_t ItemTable::find(std::string_view wanted) const {
    for (size_t i = 0; i < rows_.size(); ++i) {
        if (rows_[i].name == wanted) return static_cast<int32_t>(i);
    }
    return -1;
}

uint32_t ItemTable::rarity_colour(Rarity rarity) {
    switch (rarity) {
    case Rarity::Uncommon: return 0xFFFF55u;
    case Rarity::Rare: return 0x55FFFFu;
    case Rarity::Epic: return 0xFF55FFu;
    case Rarity::Common: break;
    }
    return 0xFFFFFFu;
}

} // namespace mcpp::ui
