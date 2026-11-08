#include "mcpp/entity/objects.hpp"
#include "mcpp/ui/container_menus.hpp"

#include <cmath>
#include <format>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace mcpp;
using namespace mcpp::entity;

int failures = 0;

void check(bool condition, std::string_view what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << '\n';
        ++failures;
    }
}

void same(int got, int want, std::string_view what) {
    check(got == want, std::format("{}: {} (want {})", what, got, want));
}

bool near(float a, float b, float tolerance = 1e-5f) { return std::fabs(a - b) <= tolerance; }

void close(float got, float want, std::string_view what) {
    check(near(got, want), std::format("{}: {} (want {})", what, got, want));
}

// `ItemClusterRenderState.getRenderedAmount`, worked out from the thresholds
// the game writes.
void test_cluster_counts() {
    same(rendered_amount(0), 1, "an empty stack is drawn once");
    same(rendered_amount(1), 1, "one of an item");
    same(rendered_amount(16), 2, "sixteen is two copies");
    same(rendered_amount(17), 3, "seventeen is three");
    same(rendered_amount(32), 3, "thirty-two is three");
    same(rendered_amount(33), 4, "thirty-three is four");
    same(rendered_amount(48), 4, "forty-eight is four");
    same(rendered_amount(64), 5, "a full stack is five");

    ItemStack empty;
    same(static_cast<int>(item_cluster_seed(empty)), 187, "an empty stack seeds with 187");
    ItemStack sword;
    sword.id = 831;
    sword.count = 1;
    same(static_cast<int>(item_cluster_seed(sword)), 831, "a stack seeds with its item id");
}

// `ItemEntity.getSpin`: a twentieth of a turn per tick off its own offset.
void test_spin() {
    close(item_spin(0.0f, 0.0f), 0.0f, "no age is no spin");
    close(item_spin(20.0f, 0.0f), 1.0f, "twenty ticks is one radian");
    close(item_spin(10.0f, 0.25f), 0.75f, "the bob offset is added");
}

// The two ground transforms vanilla's whole model tree comes down to, and the
// handful of models that name their own.
void test_ground_display() {
    const auto flat = ground_display("diamond_sword", true);
    close(flat.translation[1], 2.0f / 16.0f, "a sprite's ground transform lifts two pixels");
    close(flat.scale[0], 0.5f, "and is half size");
    const auto block = ground_display("stone", false);
    close(block.translation[1], 3.0f / 16.0f, "a block's lifts three pixels");
    close(block.scale[0], 0.25f, "and is a quarter size");
    const auto shield = ground_display("shield", false);
    close(shield.translation[0], 2.0f / 16.0f, "the shield names its own");
    close(shield.translation[1], 4.0f / 16.0f, "the shield's own lift");
    const auto bed = ground_display("red_bed", false);
    close(bed.translation[2], 2.0f / 16.0f, "a bed is pushed back two pixels");
    const auto skull = ground_display("zombie_head", false);
    close(skull.scale[0], 0.5f, "a head is half size on the ground");
}

// The table has one row per type and no duplicates, and every registry name
// it names is one the game has.
void test_type_table() {
    const auto all = object_types();
    check(!all.empty(), "the object table is not empty");
    for (const ObjectType& row : all) {
        check(row.type.starts_with("minecraft:"), std::format("{} is a registry name", row.type));
        check(object_type(row.type) == &row, std::format("{} is found by name", row.type));
        check(row.kind != ObjectKind::None, std::format("{} has a renderer", row.type));
    }
    check(object_type("minecraft:zombie") == nullptr, "a living body is not an object type");
    check(object_type("minecraft:item") != nullptr, "the dropped item is");
    check(object_type("minecraft:text_display") != nullptr, "and the hologram");
    same(static_cast<int>(object_type("minecraft:oak_boat")->kind), static_cast<int>(ObjectKind::Boat),
         "an oak boat is a boat");
    check(object_type("minecraft:oak_boat")->layer == "boat/oak", "with its own wood's layer");
}

// `ArrowRenderer.submit`: the arrow turns by its yaw less ninety, then by its
// pitch about z, so with both at zero the matrix is the entity's own place.
void test_arrow_placement() {
    const ObjectType* row = object_type("minecraft:arrow");
    check(row != nullptr, "the arrow is in the table");
    if (row == nullptr) return;
    ObjectState state;
    state.x = 3.0f;
    state.y = 4.0f;
    state.z = 5.0f;
    ObjectAssets assets;
    std::vector<ObjectPiece> pieces;
    build_object(*row, state, assets, pieces);
    same(static_cast<int>(pieces.size()), 1, "an arrow is one piece");
    if (pieces.empty()) return;
    close(pieces[0].root[12], 3.0f, "its x is where the entity is");
    close(pieces[0].root[13], 4.0f, "its y");
    close(pieces[0].root[14], 5.0f, "its z");
    // The yaw turn of minus ninety about y takes +x to +z.
    close(pieces[0].root[0], 0.0f, "the yaw turn of minus ninety, column x to x");
    close(pieces[0].root[2], 1.0f, "and x to z");
}

// `AbstractBoatRenderer.submit`: the model is lifted three eighths of a block,
// flipped onto its feet and turned a quarter.
void test_boat_placement() {
    const ObjectType* row = object_type("minecraft:oak_boat");
    if (row == nullptr) {
        check(false, "the oak boat is in the table");
        return;
    }
    ObjectState state;
    ObjectAssets assets;
    std::vector<ObjectPiece> pieces;
    build_object(*row, state, assets, pieces);
    same(static_cast<int>(pieces.size()), 1, "a boat is one model piece");
    if (pieces.empty()) return;
    close(pieces[0].root[13], 0.375f, "a boat sits three eighths of a block up");
    // 180 degrees about y, then the flip, then 90 about y: x maps to -z.
    close(pieces[0].root[1], 0.0f, "the flip leaves no y in the x column");
    close(pieces[0].root[5], -1.0f, "and turns y upside down");
}

// `TntRenderer.submit`: the swell in the last ten ticks of fuse is the fourth
// power of how far through them it is, three tenths at most.
void test_tnt_swell() {
    const auto swell = [](float fuse) {
        float s = 1.0f - fuse / 10.0f;
        s = s < 0.0f ? 0.0f : s > 1.0f ? 1.0f : s;
        s *= s;
        s *= s;
        return 1.0f + s * 0.3f;
    };
    close(swell(10.0f), 1.0f, "ten ticks of fuse is no swell");
    close(swell(0.0f), 1.3f, "no fuse left is the whole swell");
    close(swell(5.0f), 1.0f + 0.0625f * 0.3f, "half way is a sixteenth of it");
}

// `ExperienceOrb.getIcon` through the cell the renderer reads it as.
void test_orb_cells() {
    const auto cell = [](int icon) {
        return std::pair<float, float>{static_cast<float>(icon % 4 * 16) / 64.0f,
                                       static_cast<float>(icon / 4 * 16) / 64.0f};
    };
    const auto first = cell(0);
    close(first.first, 0.0f, "the first cell starts at the sheet's corner");
    const auto fifth = cell(4);
    close(fifth.first, 0.0f, "the fifth cell wraps to the next row");
    close(fifth.second, 0.25f, "a quarter of the way down");
    const auto last = cell(10);
    close(last.first, 0.5f, "the eleventh cell is the third of its row");
    close(last.second, 0.5f, "half way down");
}

// Every menu's own slot count, as each `AbstractContainerMenu` subclass adds
// them, and the thirty-six player slots after them.
void test_menu_sizes() {
    using mcpp::ui::make_menu;
    using mcpp::ui::menu_container_slots;
    using mcpp::ui::MenuKind;
    struct Want {
        MenuKind kind;
        int own;
        int total;
    };
    const std::vector<Want> wanted = {
        {MenuKind::Generic9x1, 9, 45},   {MenuKind::Generic9x2, 18, 54},
        {MenuKind::Generic9x3, 27, 63},  {MenuKind::Generic9x4, 36, 72},
        {MenuKind::Generic9x5, 45, 81},  {MenuKind::Generic9x6, 54, 90},
        {MenuKind::Generic3x3, 9, 45},   {MenuKind::Crafter3x3, 9, 46},
        {MenuKind::Hopper, 5, 41},       {MenuKind::ShulkerBox, 27, 63},
        {MenuKind::Furnace, 3, 39},      {MenuKind::BlastFurnace, 3, 39},
        {MenuKind::Smoker, 3, 39},       {MenuKind::BrewingStand, 5, 41},
        {MenuKind::Anvil, 3, 39},        {MenuKind::Grindstone, 3, 39},
        {MenuKind::Smithing, 4, 40},     {MenuKind::CartographyTable, 3, 39},
        {MenuKind::Stonecutter, 2, 38},  {MenuKind::Loom, 4, 40},
        {MenuKind::Enchantment, 2, 38},  {MenuKind::Beacon, 1, 37},
        {MenuKind::Merchant, 3, 39},     {MenuKind::Crafting, 10, 46},
    };
    for (const Want& one : wanted) {
        const std::string name(mcpp::ui::menu_name(one.kind));
        same(menu_container_slots(one.kind), one.own, name + " has its own slots");
        const auto menu = make_menu(one.kind, nullptr);
        same(static_cast<int>(menu.size()), one.total, name + " and the player's thirty-six");
        // The hotbar is the last nine of the player's own thirty-six, which
        // start where the menu's own slots end. A crafter puts its result
        // after them, so it is the one menu whose last slot is not the hotbar.
        const size_t hotbar = static_cast<size_t>(one.own) + 27;
        same(menu.rule(hotbar).inventory_index, 0, name + "'s hotbar starts at one");
        same(menu.rule(hotbar + 8).inventory_index, 8, name + "'s hotbar ends at nine");
    }
    const auto furnace = make_menu(MenuKind::Furnace, nullptr);
    check(furnace.rule(2).result, "a furnace's result cannot be put into");
    const auto anvil = make_menu(MenuKind::Anvil, nullptr);
    check(anvil.rule(2).result, "an anvil's result either");
    const auto crafter = make_menu(MenuKind::Crafter3x3, nullptr);
    check(crafter.rule(45).result, "a crafter's result comes after the player's own slots");
}

// The registry names round-trip, which is what `open_screen`'s type number
// resolves through.
void test_menu_names() {
    using mcpp::ui::menu_kind;
    using mcpp::ui::menu_name;
    using mcpp::ui::MenuKind;
    for (int at = 0; at <= static_cast<int>(MenuKind::Stonecutter); ++at) {
        const auto kind = static_cast<MenuKind>(at);
        const std::string_view name = menu_name(kind);
        check(!name.empty(), std::format("menu {} has a name", at));
        const auto back = menu_kind(name);
        check(back.has_value() && *back == kind, std::format("{} round-trips", name));
    }
    check(menu_name(MenuKind::Generic9x6) == "minecraft:generic_9x6", "the six-row chest");
    check(menu_name(MenuKind::CartographyTable) == "minecraft:cartography_table",
          "the cartography table");
    check(!menu_kind("minecraft:not_a_menu").has_value(), "an unknown name is nothing");
}

// The slot positions each screen's own `init` works out, written out by hand
// from the vanilla constructors.
void test_screen_slots() {
    using mcpp::ui::AbstractContainerScreen;
    using mcpp::ui::make_container_screen;
    using mcpp::ui::MenuKind;
    using mcpp::ui::literal;

    struct Want {
        MenuKind kind;
        int slots;
        int image_height;
        // One slot to check, by index, and where it must sit.
        int index;
        int x;
        int y;
    };
    // The six-row chest: the grid starts at (8, 18) and the player's own at
    // (8, 18 + 6*18 + 13) = (8, 139).
    const std::vector<Want> wanted = {
        {MenuKind::Generic9x1, 45, 132, 0, 8, 18},
        {MenuKind::Generic9x1, 45, 132, 9, 8, 49},
        {MenuKind::Generic9x3, 63, 168, 27, 8, 85},
        {MenuKind::Generic9x6, 90, 222, 54, 8, 139},
        {MenuKind::Generic9x6, 90, 222, 89, 8 + 8 * 18, 197},
        {MenuKind::Generic3x3, 45, 166, 4, 80, 35},
        {MenuKind::Crafter3x3, 46, 166, 0, 26, 17},
        {MenuKind::Crafter3x3, 46, 166, 45, 134, 35},
        {MenuKind::Hopper, 41, 133, 4, 44 + 4 * 18, 20},
        {MenuKind::Hopper, 41, 133, 5, 8, 51},
        {MenuKind::ShulkerBox, 63, 166, 26, 8 + 8 * 18, 18 + 2 * 18},
        {MenuKind::Furnace, 39, 166, 0, 56, 17},
        {MenuKind::Furnace, 39, 166, 1, 56, 53},
        {MenuKind::Furnace, 39, 166, 2, 116, 35},
        {MenuKind::BrewingStand, 41, 166, 1, 79, 58},
        {MenuKind::BrewingStand, 41, 166, 3, 79, 17},
        {MenuKind::BrewingStand, 41, 166, 4, 17, 17},
        {MenuKind::Anvil, 39, 166, 0, 27, 47},
        {MenuKind::Anvil, 39, 166, 2, 134, 47},
        {MenuKind::Grindstone, 39, 166, 1, 49, 40},
        {MenuKind::Smithing, 40, 166, 3, 98, 48},
        {MenuKind::CartographyTable, 39, 166, 2, 145, 39},
        {MenuKind::Stonecutter, 38, 166, 1, 143, 33},
        {MenuKind::Loom, 40, 166, 3, 143, 57},
        {MenuKind::Enchantment, 38, 166, 1, 35, 47},
        {MenuKind::Beacon, 37, 219, 0, 136, 110},
        {MenuKind::Beacon, 37, 219, 1, 36, 137},
        {MenuKind::Merchant, 39, 166, 2, 220, 37},
        {MenuKind::Merchant, 39, 166, 3, 108, 84},
    };
    for (const Want& one : wanted) {
        auto screen = make_container_screen(one.kind, literal("x"), nullptr);
        const std::string name(mcpp::ui::menu_name(one.kind));
        auto* container = dynamic_cast<AbstractContainerScreen*>(screen.get());
        check(container != nullptr, name + " has a screen");
        if (container == nullptr) continue;
        same(static_cast<int>(container->slots.size()), one.slots, name + " lays out every slot");
        same(container->image_height, one.image_height, name + "'s window is as tall as vanilla's");
        if (static_cast<size_t>(one.index) >= container->slots.size()) continue;
        const auto& slot = container->slots[static_cast<size_t>(one.index)];
        same(slot.index, one.index, name + " keeps the menu's own slot numbers");
        same(slot.x, one.x, std::format("{} slot {} x", name, one.index));
        same(slot.y, one.y, std::format("{} slot {} y", name, one.index));
    }
    // The lectern has no container screen of its own.
    check(make_container_screen(MenuKind::Lectern, literal("x"), nullptr) == nullptr,
          "the lectern opens a book, not a container");
}

} // namespace

int main() {
    test_cluster_counts();
    test_spin();
    test_ground_display();
    test_type_table();
    test_arrow_placement();
    test_boat_placement();
    test_tnt_swell();
    test_orb_cells();
    test_menu_sizes();
    test_menu_names();
    test_screen_slots();
    if (failures == 0) std::cout << "objects: all passed\n";
    return failures == 0 ? 0 : 1;
}
