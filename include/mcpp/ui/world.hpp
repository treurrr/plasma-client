#pragma once
#include "mcpp/ui/container.hpp"
#include "mcpp/ui/gui_graphics.hpp"
#include "mcpp/ui/items.hpp"

#include <array>
#include <functional>
#include <string>
#include <vector>

// What the in-world screens see of the game: the player's inventory as a
// menu, how to click in it, and the hooks other areas fill in - the player
// model in the inventory's black window (entities), the recipe book's
// contents (data).
namespace mcpp::ui {

// One recipe button: what it makes, and whether the inventory has what it
// takes. `many` is a collection of more than one recipe for the same result.
struct RecipeEntry {
    int32_t result = 0;
    int32_t count = 1;
    bool craftable = false;
    bool many = false;
};

// The crafting book: its four categories after the search tab, in the game's
// order - equipment, building blocks, misc, redstone - each a list of
// collections. Empty until the server's recipe book packets are read.
struct RecipeBookView {
    bool open = false;
    bool filtering = false;
    std::array<std::vector<RecipeEntry>, 4> categories;
};

struct World {
    ContainerMenu* menu = nullptr;
    const ItemTable* items = nullptr;
    // Survival, creative, adventure, spectator.
    int32_t game_mode = 0;
    bool creative() const { return game_mode == 1; }
    // Hands a click to the game: the menu has already changed under it.
    std::function<void(int slot, int button, ClickType type,
                       const std::vector<std::pair<int16_t, ItemStack>>& changed)>
        click;
    std::function<void(int container)> close_container;
    // Creative mode: set a slot of the player's own inventory, or throw a
    // stack out of the inventory (slot -1).
    std::function<void(int16_t slot, const ItemStack& stack)> creative_set;
    // The player drawn into the box (x0, y0)-(x1, y1) at `size`, looking at the
    // pointer, the way InventoryScreen does it; empty until entities fill it.
    std::function<void(GuiGraphics&, int x0, int y0, int x1, int y1, int size, float y_offset,
                       float mouse_x, float mouse_y)>
        draw_player;
    RecipeBookView recipe_book;
    std::function<void(bool open, bool filtering)> recipe_book_changed;
    // The translated name an item shows, with its rarity colour.
    std::string item_name(const GuiGraphics& gfx, int32_t id) const;
};

} // namespace mcpp::ui
