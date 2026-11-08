#include "mcpp/ui/container_menus.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace mcpp::ui {
namespace {

// The registry names, in `MenuKind`'s own order.
constexpr std::array<std::string_view, 25> kNames = {
    "minecraft:generic_9x1",      "minecraft:generic_9x2",  "minecraft:generic_9x3",
    "minecraft:generic_9x4",      "minecraft:generic_9x5",  "minecraft:generic_9x6",
    "minecraft:generic_3x3",      "minecraft:crafter_3x3",  "minecraft:anvil",
    "minecraft:beacon",           "minecraft:blast_furnace", "minecraft:brewing_stand",
    "minecraft:crafting",         "minecraft:enchantment",  "minecraft:furnace",
    "minecraft:grindstone",       "minecraft:hopper",       "minecraft:lectern",
    "minecraft:loom",             "minecraft:merchant",     "minecraft:shulker_box",
    "minecraft:smithing",         "minecraft:smoker",       "minecraft:cartography_table",
    "minecraft:stonecutter",
};

// `AbstractContainerMenu.addStandardInventorySlots`: the player's twenty-seven
// and then the hotbar's nine, fifty-eight below them.
void add_inventory_slots(std::vector<ScreenSlot>& out, int first, int x, int y) {
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 9; ++column) {
            out.push_back({first + column + row * 9, x + column * 18, y + row * 18, {}});
        }
    }
    for (int column = 0; column < 9; ++column) {
        out.push_back({first + 27 + column, x + column * 18, y + 58, {}});
    }
}

// The rules on the thirty-six player slots a menu ends with: each one maps to
// the inventory index a number key swaps with, and they are one container for
// a shift double click.
void inventory_rules(std::vector<SlotRule>& rules, int first) {
    for (int i = 0; i < 27; ++i) {
        rules[static_cast<size_t>(first + i)].inventory_index = 9 + i;
        rules[static_cast<size_t>(first + i)].container = 2;
    }
    for (int i = 0; i < 9; ++i) {
        rules[static_cast<size_t>(first + 27 + i)].inventory_index = i;
        rules[static_cast<size_t>(first + 27 + i)].container = 2;
    }
}

ContainerMenu plain_menu(int container_slots, const ItemTable* items, int result_slot = -1,
                         int result_max = 99) {
    std::vector<SlotRule> rules(static_cast<size_t>(container_slots) + 36);
    for (int i = 0; i < container_slots; ++i) rules[static_cast<size_t>(i)].container = 1;
    if (result_slot >= 0) {
        rules[static_cast<size_t>(result_slot)].result = true;
        rules[static_cast<size_t>(result_slot)].accepts = SlotRule::Accepts::Nothing;
        rules[static_cast<size_t>(result_slot)].max_stack = result_max;
    }
    inventory_rules(rules, container_slots);
    return ContainerMenu(std::move(rules), items);
}

int32_t property_of(const World* world, int index) {
    if (world == nullptr || world->container_property == nullptr) return 0;
    return world->container_property(index);
}

} // namespace

std::string_view menu_name(MenuKind kind) {
    const auto at = static_cast<size_t>(kind);
    return at < kNames.size() ? kNames[at] : std::string_view{};
}

std::optional<MenuKind> menu_kind(std::string_view registry_name) {
    for (size_t at = 0; at < kNames.size(); ++at) {
        if (kNames[at] == registry_name) return static_cast<MenuKind>(at);
    }
    return std::nullopt;
}

int menu_container_slots(MenuKind kind) {
    switch (kind) {
    case MenuKind::Generic9x1: return 9;
    case MenuKind::Generic9x2: return 18;
    case MenuKind::Generic9x3: return 27;
    case MenuKind::Generic9x4: return 36;
    case MenuKind::Generic9x5: return 45;
    case MenuKind::Generic9x6: return 54;
    case MenuKind::Generic3x3: return 9;
    // `CrafterMenu`: the nine grid slots come first, the result after the
    // player's own thirty-six.
    case MenuKind::Crafter3x3: return 9;
    case MenuKind::Anvil: return 3;
    case MenuKind::Beacon: return 1;
    case MenuKind::BlastFurnace:
    case MenuKind::Furnace:
    case MenuKind::Smoker: return 3;
    case MenuKind::BrewingStand: return 5;
    case MenuKind::Crafting: return 10;
    case MenuKind::Enchantment: return 2;
    case MenuKind::Grindstone: return 3;
    case MenuKind::Hopper: return 5;
    case MenuKind::Lectern: return 0;
    case MenuKind::Loom: return 4;
    case MenuKind::Merchant: return 3;
    case MenuKind::ShulkerBox: return 27;
    case MenuKind::Smithing: return 4;
    case MenuKind::CartographyTable: return 3;
    case MenuKind::Stonecutter: return 2;
    }
    return 0;
}

ContainerMenu make_menu(MenuKind kind, const ItemTable* items) {
    switch (kind) {
    case MenuKind::Crafting: return ContainerMenu::crafting_table(items);
    // `CrafterMenu`: nine grid slots, the player's thirty-six, then a result
    // nothing can be taken out of.
    case MenuKind::Crafter3x3: {
        std::vector<SlotRule> rules(46);
        for (int i = 0; i < 9; ++i) rules[static_cast<size_t>(i)].container = 1;
        inventory_rules(rules, 9);
        rules[45].result = true;
        rules[45].accepts = SlotRule::Accepts::Nothing;
        return ContainerMenu(std::move(rules), items);
    }
    // `AbstractFurnaceMenu`: ingredient, fuel, result.
    case MenuKind::BlastFurnace:
    case MenuKind::Furnace:
    case MenuKind::Smoker: return plain_menu(3, items, 2);
    // `BrewingStandMenu`: three bottles that take one each, the ingredient
    // and the fuel.
    case MenuKind::BrewingStand: {
        ContainerMenu menu = plain_menu(5, items);
        return menu;
    }
    // `ItemCombinerMenu`: the inputs, then the result, then the player's own.
    case MenuKind::Anvil: return plain_menu(3, items, 2);
    case MenuKind::Grindstone: return plain_menu(3, items, 2);
    case MenuKind::Smithing: return plain_menu(4, items, 3);
    case MenuKind::CartographyTable: return plain_menu(3, items, 2);
    case MenuKind::Stonecutter: return plain_menu(2, items, 1);
    case MenuKind::Loom: return plain_menu(4, items, 3);
    // `MerchantMenu`: two payments and a result that uses up the trade.
    case MenuKind::Merchant: return plain_menu(3, items, 2);
    // `BeaconMenu`: one payment slot, which only takes the four metals; the
    // item table does not say which, so anything goes in and the server puts
    // back what it disagrees with.
    case MenuKind::Beacon: return plain_menu(1, items);
    case MenuKind::Enchantment: {
        ContainerMenu menu = plain_menu(2, items);
        return menu;
    }
    default: break;
    }
    return plain_menu(menu_container_slots(kind), items);
}

SimpleContainerScreen::SimpleContainerScreen(Component title, Look look, ContainerMenu* menu)
    : AbstractContainerScreen(std::move(title), translatable("container.inventory")),
      look_(look), menu_(menu) {
    image_width = look_.image_width;
    image_height = look_.image_height;
    inventory_label_y = image_height - 94;
}

ContainerMenu* SimpleContainerScreen::menu() const {
    return menu_ != nullptr ? menu_ : AbstractContainerScreen::menu();
}

// `DispenserScreen.init`: the dispenser and the dropper centre their title.
void SimpleContainerScreen::init() {
    AbstractContainerScreen::init();
    if (look_.centre_title && manager != nullptr && manager->font != nullptr) {
        title_label_x = (image_width - manager->font->width(title)) / 2;
    }
}

void SimpleContainerScreen::render_bg(GuiGraphics& gfx, double, double) {
    const int x = (width - image_width) / 2;
    const int y = (height - image_height) / 2;
    if (look_.rows <= 0) {
        gfx.blit(look_.texture, x, y, 0.0f, 0.0f, image_width, image_height, 256, 256);
        return;
    }
    // `ContainerScreen.renderBg`: the top of the picture for as many rows as
    // the chest has, then the player's own part out of the bottom of it.
    gfx.blit(look_.texture, x, y, 0.0f, 0.0f, image_width, look_.rows * 18 + 17, 256, 256);
    gfx.blit(look_.texture, x, y + look_.rows * 18 + 17, 0.0f, 126.0f, image_width, 96, 256, 256);
}

FurnaceScreen::FurnaceScreen(Component title, std::string_view texture, ContainerMenu* menu)
    : AbstractContainerScreen(std::move(title), translatable("container.inventory")),
      texture_(texture), menu_(menu) {
    slots.push_back({0, 56, 17, {}});
    slots.push_back({1, 56, 53, {}});
    slots.push_back({2, 116, 35, {}});
    add_inventory_slots(slots, 3, 8, 84);
}

ContainerMenu* FurnaceScreen::menu() const {
    return menu_ != nullptr ? menu_ : AbstractContainerScreen::menu();
}

// `AbstractFurnaceScreen.renderBg`: the lit flame grows out of the bottom of
// its sprite as the fuel burns down, the arrow fills left to right.
void FurnaceScreen::render_bg(GuiGraphics& gfx, double, double) {
    const int x = left_pos;
    const int y = top_pos;
    gfx.blit(texture_, x, y, 0.0f, 0.0f, image_width, image_height, 256, 256);
    const World* w = world();
    const int32_t lit_time = property_of(w, 0);
    const int32_t lit_duration = property_of(w, 1);
    const int32_t cook_time = property_of(w, 2);
    const int32_t cook_total = property_of(w, 3);
    if (lit_time > 0 && lit_duration > 0) {
        // `AbstractFurnaceScreen.isLit`, `.litProgress`: thirteen pixels of
        // flame, counted from its bottom.
        const int burn = static_cast<int>(
            std::lround(static_cast<double>(lit_time) / lit_duration * 13.0));
        gfx.blit_sprite("container/furnace/lit_progress", 14, 14, 0, 14 - burn, x + 56,
                        y + 36 + 14 - burn, 14, burn);
    }
    if (cook_total > 0 && cook_time > 0) {
        // `.burnProgress`: twenty-four pixels of arrow.
        const int done = static_cast<int>(
            std::lround(static_cast<double>(cook_time) / cook_total * 24.0));
        gfx.blit_sprite("container/furnace/burn_progress", 24, 16, 0, 0, x + 79, y + 34, done,
                        16);
    }
}

BrewingStandScreen::BrewingStandScreen(Component title, ContainerMenu* menu)
    : AbstractContainerScreen(std::move(title), translatable("container.inventory")),
      menu_(menu) {
    slots.push_back({0, 56, 51, {}});
    slots.push_back({1, 79, 58, {}});
    slots.push_back({2, 102, 51, {}});
    slots.push_back({3, 79, 17, {}});
    slots.push_back({4, 17, 17, {}});
    add_inventory_slots(slots, 5, 8, 84);
}

ContainerMenu* BrewingStandScreen::menu() const {
    return menu_ != nullptr ? menu_ : AbstractContainerScreen::menu();
}

// `BrewingStandScreen.renderBg`: the fuel bar, then the bubbles that rise out
// of eight frames, then the arrow that falls as the brew finishes.
void BrewingStandScreen::render_bg(GuiGraphics& gfx, double, double) {
    // `BrewingStandScreen.BUBBLELENGTHS`.
    static constexpr std::array<int, 8> kBubbleLengths = {29, 24, 20, 16, 11, 6, 0, 0};
    const int x = left_pos;
    const int y = top_pos;
    gfx.blit("container/brewing_stand", x, y, 0.0f, 0.0f, image_width, image_height, 256, 256);
    const World* w = world();
    const int32_t brew_time = property_of(w, 0);
    const int32_t fuel = property_of(w, 1);
    const int fuel_width = std::clamp((18 * fuel + 10) / 20, 0, 18);
    if (fuel_width > 0) {
        gfx.blit_sprite("container/brewing_stand/fuel_length", 18, 4, 0, 0, x + 60, y + 44,
                        fuel_width, 4);
    }
    if (brew_time > 0) {
        const int done = static_cast<int>(28.0f * (1.0f - brew_time / 400.0f));
        if (done > 0) {
            gfx.blit_sprite("container/brewing_stand/brew_progress", 9, 28, 0, 0, x + 97, y + 16,
                            9, done);
        }
        const int bubbles = kBubbleLengths[static_cast<size_t>(brew_time / 2 % 7)];
        if (bubbles > 0) {
            gfx.blit_sprite("container/brewing_stand/bubbles", 12, 29, 0, 29 - bubbles, x + 63,
                            y + 14 + 29 - bubbles, 12, bubbles);
        }
    }
}

ItemCombinerScreen::ItemCombinerScreen(Component title, Look look, ContainerMenu* menu)
    : AbstractContainerScreen(std::move(title), translatable("container.inventory")),
      look_(look), menu_(menu) {
    image_width = look_.image_width;
    image_height = look_.image_height;
    inventory_label_y = image_height - 94;
}

ContainerMenu* ItemCombinerScreen::menu() const {
    return menu_ != nullptr ? menu_ : AbstractContainerScreen::menu();
}

void ItemCombinerScreen::render_bg(GuiGraphics& gfx, double, double) {
    gfx.blit(look_.texture, left_pos, top_pos, 0.0f, 0.0f, image_width, image_height, 256, 256);
    render_error(gfx);
}

// `ItemCombinerScreen.renderErrorIcon`: the cross over the result when the
// inputs are there and the recipe is not. The combiners that have no such
// icon - the grindstone, the cartography table - name no sprite.
void ItemCombinerScreen::render_error(GuiGraphics& gfx) {
    if (look_.error_sprite.empty()) return;
    ContainerMenu* m = menu();
    if (m == nullptr || static_cast<size_t>(look_.result_slot) >= m->size()) return;
    bool any_input = false;
    for (int slot = 0; slot < look_.result_slot; ++slot) {
        any_input = any_input || m->slots[static_cast<size_t>(slot)].count > 0;
    }
    if (!any_input || m->slots[static_cast<size_t>(look_.result_slot)].count > 0) return;
    gfx.blit_sprite(look_.error_sprite, left_pos + look_.error_x, top_pos + look_.error_y, 28, 21);
}

AnvilScreen::AnvilScreen(Component title, ContainerMenu* menu)
    : ItemCombinerScreen(std::move(title), {"container/anvil", 176, 166}, menu) {
    title_label_x = 60;
    slots.push_back({0, 27, 47, {}});
    slots.push_back({1, 76, 47, {}});
    slots.push_back({2, 134, 47, {}});
    add_inventory_slots(slots, 3, 8, 84);
}

// `AnvilScreen.subInit`: the name field over the picture's own box.
void AnvilScreen::init() {
    ItemCombinerScreen::init();
    name_ = std::make_unique<EditBox>(left_pos + 62, top_pos + 24, 103, 12,
                                      translatable("container.repair"));
    name_->bordered = false;
    name_->max_length = 50;
    name_->text_colour = 0xFFFFFFFFu;
    name_->on_change = [](const std::string&) {};
    name_->set_focused(true);
}

std::string AnvilScreen::name() const { return name_ != nullptr ? name_->value() : std::string{}; }

void AnvilScreen::render(GuiGraphics& gfx, double mx, double my) {
    ItemCombinerScreen::render(gfx, mx, my);
    if (name_ != nullptr) name_->render(gfx, mx, my);
}

bool AnvilScreen::key_pressed(const KeyEvent& event) {
    if (name_ != nullptr && name_->key_pressed(event)) return true;
    return ItemCombinerScreen::key_pressed(event);
}

bool AnvilScreen::char_typed(uint32_t code, int mods) {
    if (name_ != nullptr && name_->char_typed(code, mods)) return true;
    return ItemCombinerScreen::char_typed(code, mods);
}

// `AnvilScreen.renderLabels`: the level cost under the result, in green, or
// in red when it is forty or more and the player cannot pay it.
void AnvilScreen::render_labels(GuiGraphics& gfx) {
    AbstractContainerScreen::render_labels(gfx);
    ContainerMenu* m = menu();
    const int32_t cost = property_of(world(), 0);
    if (cost <= 0 || m == nullptr || m->size() < 3) return;
    uint32_t colour = 0xFF80FF20u;
    Component line;
    if (cost >= 40) {
        line = translatable("container.repair.expensive");
        colour = 0xFFFF6060u;
    } else if (m->slots[2].count <= 0) {
        return;
    } else {
        line = translatable("container.repair.cost", {literal(std::to_string(cost))});
    }
    const int at = image_width - 8 - gfx.font.width(line) - 2;
    gfx.fill(at - 2, 67, image_width - 8, 79, 0x4F000000u);
    gfx.draw_string(line, at, 69, colour);
}

// `AnvilScreen.renderBg`: the name field's own box over the picture's red
// placeholder, greyed out until there is something to rename.
void AnvilScreen::render_bg(GuiGraphics& gfx, double mx, double my) {
    ItemCombinerScreen::render_bg(gfx, mx, my);
    ContainerMenu* m = menu();
    const bool renaming = m != nullptr && !m->slots.empty() && m->slots[0].count > 0;
    gfx.blit_sprite(renaming ? "container/anvil/text_field" : "container/anvil/text_field_disabled",
                    left_pos + 59, top_pos + 20, 110, 16);
}

// `AnvilScreen.renderErrorIcon`: the cross with either input in and no result.
void AnvilScreen::render_error(GuiGraphics& gfx) {
    ContainerMenu* m = menu();
    if (m == nullptr || m->size() < 3) return;
    const bool inputs = m->slots[0].count > 0 || m->slots[1].count > 0;
    if (!inputs || m->slots[2].count > 0) return;
    gfx.blit_sprite("container/anvil/error", left_pos + 99, top_pos + 45, 28, 21);
}

EnchantmentScreen::EnchantmentScreen(Component title, ContainerMenu* menu)
    : AbstractContainerScreen(std::move(title), translatable("container.inventory")),
      menu_(menu) {
    slots.push_back({0, 15, 47, {}});
    slots.push_back({1, 35, 47, {}});
    add_inventory_slots(slots, 2, 8, 84);
}

ContainerMenu* EnchantmentScreen::menu() const {
    return menu_ != nullptr ? menu_ : AbstractContainerScreen::menu();
}

// `EnchantmentScreen.renderBg`: the three offers, each a button with the
// lapis it costs and the level it gives; a greyed one cannot be taken.
void EnchantmentScreen::render_bg(GuiGraphics& gfx, double mx, double my) {
    gfx.blit("container/enchanting_table", left_pos, top_pos, 0.0f, 0.0f, image_width,
             image_height, 256, 256);
    const World* w = world();
    const int32_t lapis = menu() != nullptr && menu()->size() > 1 ? menu()->slots[1].count : 0;
    for (int offer = 0; offer < 3; ++offer) {
        const int x = left_pos + 60;
        const int y = top_pos + 14 + 19 * offer;
        const int32_t level = property_of(w, offer);
        const int32_t clue = property_of(w, 4 + offer);
        const bool hovered = mx >= x && mx < x + 108 && my >= y && my < y + 19;
        if (level == 0) {
            gfx.blit_sprite("container/enchanting_table/enchantment_slot_disabled", x, y, 108,
                            19);
            continue;
        }
        const bool payable = lapis >= offer + 1 && level > 0;
        const std::string_view sprite =
            !payable ? "container/enchanting_table/enchantment_slot_disabled"
            : hovered ? "container/enchanting_table/enchantment_slot_highlighted"
                      : "container/enchanting_table/enchantment_slot";
        gfx.blit_sprite(sprite, x, y, 108, 19);
        // `EnchantmentScreen.renderBg`: the level in the corner, greyed out
        // when it cannot be paid for.
        // `EnchantmentScreen.renderBg`: the level in the corner, greyed out
        // when the player cannot pay for it. The enchantment's own name, in
        // the galactic font, needs the clue's registry entry, which this
        // client does not decode into the screen yet.
        const std::string level_text = std::to_string(offer + 1);
        gfx.draw_string(level_text, x + 108 - 2 - gfx.font.width(level_text), y + 7,
                        payable ? 0xFF80FF20u : 0xFF6E6E6Eu);
        (void)clue;
    }
}

bool EnchantmentScreen::mouse_clicked(double mx, double my, int button) {
    for (int offer = 0; offer < 3; ++offer) {
        const int x = left_pos + 60;
        const int y = top_pos + 14 + 19 * offer;
        if (mx >= x && mx < x + 108 && my >= y && my < y + 19) {
            // `EnchantmentScreen.mouseClicked`: an offer is a button press on
            // the menu, which the session sends as `container_button`.
            return true;
        }
    }
    return AbstractContainerScreen::mouse_clicked(mx, my, button);
}

BeaconScreen::BeaconScreen(Component title, ContainerMenu* menu)
    : AbstractContainerScreen(std::move(title), translatable("container.inventory")),
      menu_(menu) {
    image_width = 230;
    image_height = 219;
    inventory_label_y = image_height - 94;
    slots.push_back({0, 136, 110, {}});
    add_inventory_slots(slots, 1, 36, 137);
}

ContainerMenu* BeaconScreen::menu() const {
    return menu_ != nullptr ? menu_ : AbstractContainerScreen::menu();
}

void BeaconScreen::render_labels(GuiGraphics& gfx) {
    // `BeaconScreen.renderLabels`: the beacon has no labels of its own.
    (void)gfx;
}

// `BeaconScreen.renderBg`: the window, then the payment slot's own frame.
void BeaconScreen::render_bg(GuiGraphics& gfx, double, double) {
    gfx.blit("container/beacon", left_pos, top_pos, 0.0f, 0.0f, image_width, image_height, 256,
             256);
    const int32_t levels = property_of(world(), 0);
    // The tier the pyramid reached decides which rows of buttons are live;
    // the buttons themselves are the screen's own widgets, which this client
    // does not place yet.
    (void)levels;
}

RecipeListScreen::RecipeListScreen(Component title, Look look, ContainerMenu* menu)
    : AbstractContainerScreen(std::move(title), translatable("container.inventory")),
      look_(look), menu_(menu) {
    image_width = look_.image_width;
    image_height = look_.image_height;
    inventory_label_y = image_height - 94;
}

ContainerMenu* RecipeListScreen::menu() const {
    return menu_ != nullptr ? menu_ : AbstractContainerScreen::menu();
}

void RecipeListScreen::render_bg(GuiGraphics& gfx, double, double) {
    gfx.blit(look_.texture, left_pos, top_pos, 0.0f, 0.0f, image_width, image_height, 256, 256);
}

// `StonecutterScreen.renderScroller`, `.renderButtons`: the scroll bar, then
// one button per visible entry.
void RecipeListScreen::render(GuiGraphics& gfx, double mx, double my) {
    AbstractContainerScreen::render(gfx, mx, my);
    const int total = entries();
    const int visible = look_.columns * look_.rows;
    const bool can_scroll = total > visible;
    const int max_offset = std::max(0, (total + look_.columns - 1) / look_.columns - look_.rows);
    const float at = max_offset > 0 ? static_cast<float>(offset_) / static_cast<float>(max_offset)
                                    : 0.0f;
    gfx.blit_sprite(can_scroll ? "container/stonecutter/scroller"
                               : "container/stonecutter/scroller_disabled",
                    left_pos + look_.scroller_x, top_pos + look_.scroller_y + static_cast<int>(at * 54.0f),
                    12, 15);
    for (int index = offset_ * look_.columns; index < total && index < (offset_ + look_.rows) * look_.columns;
         ++index) {
        const int cell = index - offset_ * look_.columns;
        const int x = left_pos + look_.list_x + cell % look_.columns * look_.button_width;
        const int y = top_pos + look_.list_y + cell / look_.columns * look_.button_height;
        const bool hovered = mx >= x && mx < x + look_.button_width && my >= y &&
                             my < y + look_.button_height;
        const std::string_view sprite = index == selected_
                                            ? "container/stonecutter/recipe_selected"
                                        : hovered ? "container/stonecutter/recipe_highlighted"
                                                  : "container/stonecutter/recipe";
        gfx.blit_sprite(sprite, x, y, look_.button_width, look_.button_height);
    }
}

bool RecipeListScreen::mouse_clicked(double mx, double my, int button) {
    const int total = entries();
    for (int index = offset_ * look_.columns;
         index < total && index < (offset_ + look_.rows) * look_.columns; ++index) {
        const int cell = index - offset_ * look_.columns;
        const int x = left_pos + look_.list_x + cell % look_.columns * look_.button_width;
        const int y = top_pos + look_.list_y + cell / look_.columns * look_.button_height;
        if (mx >= x && mx < x + look_.button_width && my >= y && my < y + look_.button_height) {
            selected_ = index;
            return true;
        }
    }
    const int sx = left_pos + look_.scroller_x;
    const int sy = top_pos + look_.scroller_y;
    if (mx >= sx && mx < sx + 12 && my >= sy && my < sy + 54 + 15) {
        scrolling_ = true;
        return true;
    }
    return AbstractContainerScreen::mouse_clicked(mx, my, button);
}

bool RecipeListScreen::mouse_dragged(double mx, double my, int button, double dx, double dy) {
    if (scrolling_) {
        const int total = entries();
        const int max_offset =
            std::max(0, (total + look_.columns - 1) / look_.columns - look_.rows);
        const float top = static_cast<float>(top_pos + look_.scroller_y);
        const float at = std::clamp((static_cast<float>(my) - top - 7.5f) / 54.0f, 0.0f, 1.0f);
        offset_ = static_cast<int>(at * static_cast<float>(max_offset) + 0.5f);
        return true;
    }
    return AbstractContainerScreen::mouse_dragged(mx, my, button, dx, dy);
}

bool RecipeListScreen::mouse_scrolled(double mx, double my, double dx, double dy) {
    const int total = entries();
    const int max_offset = std::max(0, (total + look_.columns - 1) / look_.columns - look_.rows);
    offset_ = std::clamp(offset_ - static_cast<int>(dy), 0, max_offset);
    return AbstractContainerScreen::mouse_scrolled(mx, my, dx, dy) || true;
}

MerchantScreen::MerchantScreen(Component title, ContainerMenu* menu)
    : AbstractContainerScreen(std::move(title), translatable("container.inventory")),
      menu_(menu) {
    image_width = 276;
    image_height = 166;
    inventory_label_y = image_height - 94;
    slots.push_back({0, 136, 37, {}});
    slots.push_back({1, 162, 37, {}});
    slots.push_back({2, 220, 37, {}});
    add_inventory_slots(slots, 3, 108, 84);
}

ContainerMenu* MerchantScreen::menu() const {
    return menu_ != nullptr ? menu_ : AbstractContainerScreen::menu();
}

void MerchantScreen::render_bg(GuiGraphics& gfx, double, double) {
    gfx.blit("container/villager", left_pos, top_pos, 0.0f, 0.0f, image_width, image_height, 512,
             256);
}

// `MerchantScreen.render`: seven trade buttons down the left, scrolled by the
// bar beside them. The offers come from `merchant_offers`, which this client
// decodes into the session but not into the screen yet, so the buttons draw
// empty until it does.
void MerchantScreen::render(GuiGraphics& gfx, double mx, double my) {
    AbstractContainerScreen::render(gfx, mx, my);
    for (int row = 0; row < 7 && row + scroll_ < offers_; ++row) {
        const int x = left_pos + 5;
        const int y = top_pos + 16 + row * 20;
        const bool hovered = mx >= x && mx < x + 89 && my >= y && my < y + 20;
        const std::string_view sprite = row + scroll_ == selected_
                                            ? "container/villager/trade_arrow_out"
                                        : hovered ? "container/villager/trade_arrow_out"
                                                  : "container/villager/trade_arrow_out";
        gfx.blit_sprite(sprite, x + 60, y + 5, 10, 9);
    }
}

bool MerchantScreen::mouse_clicked(double mx, double my, int button) {
    for (int row = 0; row < 7 && row + scroll_ < offers_; ++row) {
        const int x = left_pos + 5;
        const int y = top_pos + 16 + row * 20;
        if (mx >= x && mx < x + 89 && my >= y && my < y + 20) {
            selected_ = row + scroll_;
            return true;
        }
    }
    return AbstractContainerScreen::mouse_clicked(mx, my, button);
}

bool MerchantScreen::mouse_scrolled(double mx, double my, double dx, double dy) {
    scroll_ = std::clamp(scroll_ - static_cast<int>(dy), 0, std::max(0, offers_ - 7));
    return AbstractContainerScreen::mouse_scrolled(mx, my, dx, dy) || true;
}

MountInventoryScreen::MountInventoryScreen(Component title, int chest_columns,
                                           ContainerMenu* menu)
    : AbstractContainerScreen(std::move(title), translatable("container.inventory")),
      chest_columns_(chest_columns), menu_(menu) {
    slots.push_back({0, 8, 18, "container/slot/saddle"});
    slots.push_back({1, 8, 36, "container/slot/horse_armor"});
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < chest_columns_; ++column) {
            slots.push_back({2 + column + row * chest_columns_, 80 + column * 18, 18 + row * 18,
                             {}});
        }
    }
    add_inventory_slots(slots, 2 + chest_columns_ * 3, 8, 84);
}

ContainerMenu* MountInventoryScreen::menu() const {
    return menu_ != nullptr ? menu_ : AbstractContainerScreen::menu();
}

// `AbstractMountInventoryScreen.renderBg`: the window, the animal's own
// window, and one slot frame per chest column.
void MountInventoryScreen::render_bg(GuiGraphics& gfx, double, double) {
    gfx.blit("container/horse", left_pos, top_pos, 0.0f, 0.0f, image_width, image_height, 256,
             256);
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < chest_columns_; ++column) {
            gfx.blit_sprite("container/slot", left_pos + 79 + column * 18,
                            top_pos + 17 + row * 18, 18, 18);
        }
    }
}

std::shared_ptr<Screen> make_container_screen(MenuKind kind, Component title,
                                              ContainerMenu* menu) {
    using Look = SimpleContainerScreen::Look;
    switch (kind) {
    case MenuKind::Generic9x1:
    case MenuKind::Generic9x2:
    case MenuKind::Generic9x3:
    case MenuKind::Generic9x4:
    case MenuKind::Generic9x5:
    case MenuKind::Generic9x6: {
        const int rows = static_cast<int>(kind) - static_cast<int>(MenuKind::Generic9x1) + 1;
        Look look{"container/generic_54", 176, 114 + rows * 18, rows, false};
        auto screen = std::make_shared<SimpleContainerScreen>(std::move(title), look, menu);
        for (int row = 0; row < rows; ++row) {
            for (int column = 0; column < 9; ++column) {
                screen->slots.push_back({column + row * 9, 8 + column * 18, 18 + row * 18, {}});
            }
        }
        add_inventory_slots(screen->slots, rows * 9, 8, 18 + rows * 18 + 13);
        return screen;
    }
    case MenuKind::Generic3x3: {
        auto screen = std::make_shared<SimpleContainerScreen>(
            std::move(title), Look{"container/dispenser", 176, 166, 0, true}, menu);
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 3; ++column) {
                screen->slots.push_back(
                    {column + row * 3, 62 + column * 18, 17 + row * 18, {}});
            }
        }
        add_inventory_slots(screen->slots, 9, 8, 84);
        return screen;
    }
    case MenuKind::ShulkerBox: {
        auto screen = std::make_shared<SimpleContainerScreen>(
            std::move(title), Look{"container/shulker_box", 176, 166, 0, false}, menu);
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 9; ++column) {
                screen->slots.push_back({column + row * 9, 8 + column * 18, 18 + row * 18, {}});
            }
        }
        add_inventory_slots(screen->slots, 27, 8, 84);
        return screen;
    }
    case MenuKind::Crafter3x3: {
        auto screen = std::make_shared<SimpleContainerScreen>(
            std::move(title), Look{"container/crafter", 176, 166, 0, false}, menu);
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 3; ++column) {
                screen->slots.push_back(
                    {column + row * 3, 26 + column * 18, 17 + row * 18, {}});
            }
        }
        add_inventory_slots(screen->slots, 9, 8, 84);
        screen->slots.push_back({45, 134, 35, {}});
        return screen;
    }
    case MenuKind::Hopper: {
        auto screen = std::make_shared<SimpleContainerScreen>(
            std::move(title), Look{"container/hopper", 176, 133, 0, false}, menu);
        for (int slot = 0; slot < 5; ++slot) {
            screen->slots.push_back({slot, 44 + slot * 18, 20, {}});
        }
        add_inventory_slots(screen->slots, 5, 8, 51);
        return screen;
    }
    case MenuKind::Furnace:
        return std::make_shared<FurnaceScreen>(std::move(title), "container/furnace", menu);
    case MenuKind::BlastFurnace:
        return std::make_shared<FurnaceScreen>(std::move(title), "container/blast_furnace",
                                               menu);
    case MenuKind::Smoker:
        return std::make_shared<FurnaceScreen>(std::move(title), "container/smoker", menu);
    case MenuKind::BrewingStand:
        return std::make_shared<BrewingStandScreen>(std::move(title), menu);
    case MenuKind::Crafting:
        return std::make_shared<CraftingScreen>(std::move(title));
    case MenuKind::Anvil: return std::make_shared<AnvilScreen>(std::move(title), menu);
    case MenuKind::Grindstone: {
        auto screen = std::make_shared<ItemCombinerScreen>(
            std::move(title), ItemCombinerScreen::Look{"container/grindstone", 176, 166}, menu);
        screen->slots.push_back({0, 49, 19, {}});
        screen->slots.push_back({1, 49, 40, {}});
        screen->slots.push_back({2, 129, 34, {}});
        add_inventory_slots(screen->slots, 3, 8, 84);
        return screen;
    }
    case MenuKind::Smithing: {
        auto screen = std::make_shared<ItemCombinerScreen>(
            std::move(title),
            ItemCombinerScreen::Look{"container/smithing", 176, 166, "container/smithing/error",
                                     65, 46, 3},
            menu);
        screen->slots.push_back({0, 8, 48, "container/slot/smithing_template"});
        screen->slots.push_back({1, 26, 48, {}});
        screen->slots.push_back({2, 44, 48, {}});
        screen->slots.push_back({3, 98, 48, {}});
        add_inventory_slots(screen->slots, 4, 8, 84);
        return screen;
    }
    case MenuKind::CartographyTable: {
        auto screen = std::make_shared<ItemCombinerScreen>(
            std::move(title), ItemCombinerScreen::Look{"container/cartography_table", 176, 166},
            menu);
        screen->slots.push_back({0, 15, 15, {}});
        screen->slots.push_back({1, 15, 52, {}});
        screen->slots.push_back({2, 145, 39, {}});
        add_inventory_slots(screen->slots, 3, 8, 84);
        return screen;
    }
    case MenuKind::Stonecutter: {
        auto screen = std::make_shared<RecipeListScreen>(
            std::move(title),
            RecipeListScreen::Look{"container/stonecutter", 176, 166, 52, 14, 4, 3, 16, 18, 119,
                                   15},
            menu);
        screen->slots.push_back({0, 20, 33, {}});
        screen->slots.push_back({1, 143, 33, {}});
        add_inventory_slots(screen->slots, 2, 8, 84);
        return screen;
    }
    case MenuKind::Loom: {
        auto screen = std::make_shared<RecipeListScreen>(
            std::move(title),
            RecipeListScreen::Look{"container/loom", 176, 166, 60, 13, 4, 4, 14, 14, 119, 13},
            menu);
        screen->slots.push_back({0, 13, 26, {}});
        screen->slots.push_back({1, 33, 26, {}});
        screen->slots.push_back({2, 23, 45, {}});
        screen->slots.push_back({3, 143, 57, {}});
        add_inventory_slots(screen->slots, 4, 8, 84);
        return screen;
    }
    case MenuKind::Enchantment:
        return std::make_shared<EnchantmentScreen>(std::move(title), menu);
    case MenuKind::Beacon: return std::make_shared<BeaconScreen>(std::move(title), menu);
    case MenuKind::Merchant: return std::make_shared<MerchantScreen>(std::move(title), menu);
    case MenuKind::Lectern: break;
    }
    return nullptr;
}

} // namespace mcpp::ui
