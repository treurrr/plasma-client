#pragma once
#include "mcpp/ui/container.hpp"
#include "mcpp/ui/gui_graphics.hpp"
#include "mcpp/ui/items.hpp"
#include "mcpp/ui/recipe_book.hpp"

#include <array>
#include <functional>
#include <string>
#include <vector>

// What the in-world screens see of the game: the player's inventory as a
// menu, how to click in it, and the hooks other areas fill in - the player
// model in the inventory's black window (entities), the recipe book's
// contents (data).
namespace mcpp::ui {

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
    // Creative: the cursor and the slots are the client's to say; this hands
    // the menu's current state to the game without a click packet.
    std::function<void()> commit;
    // The recipe book the server filled, and its crafting switches.
    ClientRecipeBook recipe_book;
    std::function<void(bool open, bool filtering)> recipe_book_changed;
    // C: asks the server to fill the open grid from a recipe; the book has
    // shown a recipe it highlighted.
    std::function<void(int32_t container, int32_t recipe, bool use_max)> place_recipe;
    std::function<void(int32_t recipe)> recipe_seen;
    // `container_set_data`: an open menu's own properties by index - a
    // furnace's progress, an enchanting table's three offers, an anvil's cost,
    // a beacon's payment. Empty until the session area fills it in, and then
    // every screen that reads one sees zero.
    std::function<int32_t(int index)> container_property;
    // The translated name an item shows, with its rarity colour.
    std::string item_name(const GuiGraphics& gfx, int32_t id) const;
};

} // namespace mcpp::ui
