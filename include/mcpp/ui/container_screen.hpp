#pragma once
#include "mcpp/ui/screen.hpp"
#include "mcpp/ui/widgets.hpp"
#include "mcpp/ui/world.hpp"

#include <memory>
#include <string>
#include <vector>

// The screens over a container: the window, its slots, the item under the
// pointer and the one on it, and every way of clicking, dragging and pressing
// keys over them that the game has.
namespace mcpp::ui {

// Whether a key or a mouse button is the one bound to `name` right now.
bool binding_matches(const ScreenManager* manager, std::string_view name, const KeyEvent& event);
bool binding_matches_mouse(const ScreenManager* manager, std::string_view name, int button);

struct ScreenSlot {
    int index = 0;      // in the menu
    int x = 0;          // from the window's corner
    int y = 0;
    std::string empty_icon;
    bool active = true;
};

class AbstractContainerScreen : public Screen {
public:
    AbstractContainerScreen(Component name, Component inventory_name);

    void init() override;
    void render(GuiGraphics& gfx, double mx, double my) override;
    void render_background(GuiGraphics& gfx, double mx, double my) override;
    bool mouse_clicked(double mx, double my, int button) override;
    bool mouse_released(double mx, double my, int button) override;
    bool mouse_dragged(double mx, double my, int button, double dx, double dy) override;
    bool key_pressed(const KeyEvent& event) override;
    void on_close() override;
    void removed() override;
    bool is_pause_screen() const override { return false; }
    bool is_in_game_ui() const override { return true; }

    int left_pos = 0;
    int top_pos = 0;
    int image_width = 176;
    int image_height = 166;
    int title_label_x = 8;
    int title_label_y = 6;
    int inventory_label_x = 8;
    int inventory_label_y = 166 - 94;
    std::vector<ScreenSlot> slots;
    const ScreenSlot* hovered_slot = nullptr;

protected:
    World* world() const;
    ContainerMenu* menu() const;
    void render_contents(GuiGraphics& gfx, double mx, double my);
    void render_carried_item(GuiGraphics& gfx, double mx, double my);
    void render_tooltip(GuiGraphics& gfx, double mx, double my);
    virtual void render_labels(GuiGraphics& gfx);
    virtual void render_bg(GuiGraphics& gfx, double mx, double my) = 0;
    virtual void render_slots(GuiGraphics& gfx);
    void render_slot(GuiGraphics& gfx, const ScreenSlot& slot);
    const ScreenSlot* slot_at(double mx, double my) const;
    virtual bool is_hovering(int x, int y, int w, int h, double mx, double my) const;
    virtual bool has_clicked_outside(double mx, double my, int left, int top) const;
    virtual void slot_clicked(const ScreenSlot* slot, int index, int button, ClickType type);
    bool check_hotbar_key_pressed(const KeyEvent& event);
    void check_hotbar_mouse_clicked(int button);
    std::vector<std::vector<Run>> tooltip_of(const GuiGraphics& gfx, const ItemStack& stack) const;

private:
    void recalculate_quick_craft_remaining();

    Component inventory_title_;
    const ScreenSlot* last_click_slot_ = nullptr;
    bool skip_next_release_ = true;
    bool is_quick_crafting_ = false;
    int quick_craft_type_ = 0;
    int quick_craft_button_ = 0;
    int quick_craft_remainder_ = 0;
    std::vector<int> quick_craft_slots_;
    bool double_click_ = false;
    ItemStack last_quick_moved_;
};

// The crafting book beside the inventory: five tabs, a search field, the
// craftable-only toggle, twenty recipes a page and the page arrows.
class RecipeBookComponent {
public:
    static constexpr int kImageWidth = 147;
    static constexpr int kImageHeight = 166;

    void init(int width, int height, World* world, bool width_too_narrow);
    int update_screen_position(int width, int image_width) const;
    void toggle_visibility();
    bool visible() const { return visible_; }
    void render(GuiGraphics& gfx, double mx, double my);
    void render_tooltip(GuiGraphics& gfx, double mx, double my);
    bool mouse_clicked(double mx, double my, int button);
    bool key_pressed(const KeyEvent& event, const ScreenManager* manager);
    bool char_typed(uint32_t code, int mods);
    bool has_clicked_outside(double mx, double my, int left, int top, int w, int h) const;
    void tick();

private:
    struct Tab {
        int32_t primary = 0;
        int32_t secondary = 0;
        int category = -1;    // -1 is search
        int x = 0;
        int y = 0;
        bool visible = true;
        bool hovered = false;
    };
    void init_visuals();
    void update_tabs();
    void update_collections(bool reset_page);
    int x_origin() const { return (width_ - kImageWidth) / 2 - x_offset_; }
    int y_origin() const { return (height_ - kImageHeight) / 2; }
    void set_visible(bool on);

    World* world_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    int x_offset_ = 0;
    bool width_too_narrow_ = false;
    bool visible_ = false;
    std::vector<Tab> tabs_;
    size_t selected_tab_ = 0;
    std::unique_ptr<EditBox> search_;
    std::string last_search_;
    std::vector<RecipeEntry> shown_;
    int page_ = 0;
    int pages_ = 0;
    int hovered_recipe_ = -1;
    bool filter_hovered_ = false;
    bool ignore_text_input_ = false;
};

class InventoryScreen : public AbstractContainerScreen {
public:
    InventoryScreen();
    void init() override;
    void render(GuiGraphics& gfx, double mx, double my) override;
    bool mouse_clicked(double mx, double my, int button) override;
    bool mouse_released(double mx, double my, int button) override;
    bool key_pressed(const KeyEvent& event) override;
    bool char_typed(uint32_t code, int mods) override;
    void tick() override;

    RecipeBookComponent recipe_book;

protected:
    void render_labels(GuiGraphics& gfx) override;
    void render_bg(GuiGraphics& gfx, double mx, double my) override;
    bool is_hovering(int x, int y, int w, int h, double mx, double my) const override;
    bool has_clicked_outside(double mx, double my, int left, int top) const override;

private:
    void place_book_button();
    bool width_too_narrow_ = false;
    bool button_clicked_ = false;
    SpriteButton* book_button_ = nullptr;
    float x_mouse_ = 0.0f;
    float y_mouse_ = 0.0f;
};

} // namespace mcpp::ui
