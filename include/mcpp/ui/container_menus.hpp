#pragma once
// Every menu the server can open with `open_screen`, and the screen the game
// opens for it. `AbstractContainerScreen` already owns the window, the slots
// and every way of clicking them; each screen here is its own picture, its own
// slot layout, the properties `container_set_data` sends it and whatever its
// own `slotClicked` does on top.
#include "mcpp/ui/container_screen.hpp"

#include <memory>
#include <optional>
#include <string_view>

namespace mcpp::ui {

// `MenuType`, in the order the minecraft:menu registry lists them, which is
// the order `open_screen`'s type number counts in.
enum class MenuKind : uint8_t {
    Generic9x1, Generic9x2, Generic9x3, Generic9x4, Generic9x5, Generic9x6, Generic3x3,
    Crafter3x3, Anvil, Beacon, BlastFurnace, BrewingStand, Crafting, Enchantment, Furnace,
    Grindstone, Hopper, Lectern, Loom, Merchant, ShulkerBox, Smithing, Smoker,
    CartographyTable, Stonecutter,
};

std::string_view menu_name(MenuKind kind);
std::optional<MenuKind> menu_kind(std::string_view registry_name);
// How many slots the menu's own container has, before the player's own
// thirty-six: what `container_set_content` fills first.
int menu_container_slots(MenuKind kind);

// `MenuType.create`: the menu's slots and the rules on them, built the way
// each `AbstractContainerMenu` subclass builds them.
ContainerMenu make_menu(MenuKind kind, const ItemTable* items);

// `MenuScreens.create`: the screen for a menu. Nothing for a menu with no
// screen of its own.
std::shared_ptr<Screen> make_container_screen(MenuKind kind, Component title,
                                              ContainerMenu* menu);

// `ContainerScreen`, `DispenserScreen`, `HopperScreen`, `ShulkerBoxScreen`,
// `CrafterScreen`: a window that is a picture and a grid of slots and nothing
// else. The four that differ do so only in the picture, the size, where the
// labels sit and whether the title is centred.
class SimpleContainerScreen : public AbstractContainerScreen {
public:
    struct Look {
        std::string_view texture;
        int image_width = 176;
        int image_height = 166;
        // `ContainerScreen.renderBg`: the generic chest picture is blitted in
        // two parts so one texture serves one to six rows. Zero rows means the
        // whole picture goes down in one blit.
        int rows = 0;
        bool centre_title = false;
    };

    SimpleContainerScreen(Component title, Look look, ContainerMenu* menu);
    void init() override;

protected:
    ContainerMenu* menu() const override;
    void render_bg(GuiGraphics& gfx, double mx, double my) override;

private:
    Look look_;
    ContainerMenu* menu_;
};

// `AbstractFurnaceScreen`: the lit flame and the arrow that fills as the
// recipe cooks, both out of `container_set_data`.
class FurnaceScreen : public AbstractContainerScreen {
public:
    FurnaceScreen(Component title, std::string_view texture, ContainerMenu* menu);

protected:
    ContainerMenu* menu() const override;
    void render_bg(GuiGraphics& gfx, double mx, double my) override;

private:
    std::string_view texture_;
    ContainerMenu* menu_;
};

// `BrewingStandScreen`: the fuel bar, the bubbles and the arrow, all three on
// their own property.
class BrewingStandScreen : public AbstractContainerScreen {
public:
    BrewingStandScreen(Component title, ContainerMenu* menu);

protected:
    ContainerMenu* menu() const override;
    void render_bg(GuiGraphics& gfx, double mx, double my) override;

private:
    ContainerMenu* menu_;
};

// `ItemCombinerScreen`: the anvil, the grindstone, the smithing table and the
// cartography table - two or three inputs, one result, and a cross over the
// result when the recipe does not work.
class ItemCombinerScreen : public AbstractContainerScreen {
public:
    struct Look {
        std::string_view texture;
        int image_width = 176;
        int image_height = 166;
        // Where the "no recipe" cross sits, and the texture's own uv for it.
        int error_x = 0;
        int error_y = 0;
        int error_u = 0;
        int error_v = 0;
        int error_w = 0;
        int error_h = 0;
    };

    ItemCombinerScreen(Component title, Look look, ContainerMenu* menu);

protected:
    ContainerMenu* menu() const override;
    void render_bg(GuiGraphics& gfx, double mx, double my) override;
    virtual void render_error(GuiGraphics& gfx);

    Look look_;
    ContainerMenu* menu_;
};

// `AnvilScreen`: the combiner, plus the name field and the level cost under
// the result.
class AnvilScreen : public ItemCombinerScreen {
public:
    AnvilScreen(Component title, ContainerMenu* menu);
    void init() override;
    void render(GuiGraphics& gfx, double mx, double my) override;
    bool key_pressed(const KeyEvent& event) override;
    bool char_typed(uint32_t code, int mods) override;

    // What the field holds, for a test that types into it.
    std::string name() const;

protected:
    void render_labels(GuiGraphics& gfx) override;
    void render_error(GuiGraphics& gfx) override;

private:
    std::unique_ptr<EditBox> name_;
};

// `EnchantmentScreen`: the three offers, each with its level, its cost in
// lapis and the galactic name of the clue.
class EnchantmentScreen : public AbstractContainerScreen {
public:
    EnchantmentScreen(Component title, ContainerMenu* menu);
    bool mouse_clicked(double mx, double my, int button) override;

protected:
    ContainerMenu* menu() const override;
    void render_bg(GuiGraphics& gfx, double mx, double my) override;

private:
    ContainerMenu* menu_;
};

// `BeaconScreen`: the payment slot and the effect buttons over the tier the
// pyramid has reached.
class BeaconScreen : public AbstractContainerScreen {
public:
    BeaconScreen(Component title, ContainerMenu* menu);

protected:
    ContainerMenu* menu() const override;
    void render_labels(GuiGraphics& gfx) override;
    void render_bg(GuiGraphics& gfx, double mx, double my) override;

private:
    ContainerMenu* menu_;
};

// `StonecutterScreen`, `LoomScreen`: a list of recipes or patterns down the
// right of the window, scrolled by its own bar.
class RecipeListScreen : public AbstractContainerScreen {
public:
    struct Look {
        std::string_view texture;
        int image_width = 176;
        int image_height = 166;
        // Where the list starts, and how many buttons fit across and down.
        int list_x = 52;
        int list_y = 14;
        int columns = 4;
        int rows = 3;
        int button_width = 16;
        int button_height = 18;
        // The scroller's sprite, and where the bar runs.
        int scroller_x = 119;
        int scroller_y = 15;
    };

    RecipeListScreen(Component title, Look look, ContainerMenu* menu);
    void render(GuiGraphics& gfx, double mx, double my) override;
    bool mouse_clicked(double mx, double my, int button) override;
    bool mouse_dragged(double mx, double my, int button, double dx, double dy) override;
    bool mouse_scrolled(double mx, double my, double dx, double dy) override;

    int selected() const { return selected_; }
    int offset() const { return offset_; }

protected:
    ContainerMenu* menu() const override;
    void render_bg(GuiGraphics& gfx, double mx, double my) override;
    // How many entries the list holds; the server sends them, which this
    // client does not decode yet, so a test sets it.
    virtual int entries() const { return entries_; }

    Look look_;
    ContainerMenu* menu_;
    int selected_ = -1;
    int offset_ = 0;
    int entries_ = 0;
    bool scrolling_ = false;
};

// `MerchantScreen`: the trades down the left, the two inputs and the result,
// and the experience bar over them.
class MerchantScreen : public AbstractContainerScreen {
public:
    MerchantScreen(Component title, ContainerMenu* menu);
    void render(GuiGraphics& gfx, double mx, double my) override;
    bool mouse_clicked(double mx, double my, int button) override;
    bool mouse_scrolled(double mx, double my, double dx, double dy) override;

    int selected() const { return selected_; }
    int scroll() const { return scroll_; }

protected:
    ContainerMenu* menu() const override;
    void render_bg(GuiGraphics& gfx, double mx, double my) override;

private:
    ContainerMenu* menu_;
    int selected_ = 0;
    int scroll_ = 0;
    int offers_ = 0;
};

// `AbstractMountInventoryScreen`: the saddle and body slots beside the
// animal, with its chest slots when it has them.
class MountInventoryScreen : public AbstractContainerScreen {
public:
    MountInventoryScreen(Component title, int chest_columns, ContainerMenu* menu);

protected:
    ContainerMenu* menu() const override;
    void render_bg(GuiGraphics& gfx, double mx, double my) override;

private:
    int chest_columns_;
    ContainerMenu* menu_;
};

} // namespace mcpp::ui
