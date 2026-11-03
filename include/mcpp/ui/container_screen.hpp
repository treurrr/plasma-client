#pragma once
#include "mcpp/ui/screen.hpp"
#include "mcpp/ui/widgets.hpp"
#include "mcpp/ui/world.hpp"

#include <filesystem>
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
    virtual ContainerMenu* menu() const;
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
    // `RecipeBookComponent.getXOrigin`: the book's left edge.
    int x_origin() const { return (width_ - kImageWidth) / 2 - x_offset_; }
    // `RecipeBookComponent.getYOrigin`: the book's top edge.
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

// The creative tabs as the game builds them, baked by tools/bake_creative.py.
struct CreativeTab {
    enum class Type : uint8_t { Category, Inventory, Hotbar, Search };
    std::string key;
    std::string title;          // translation key
    std::string background;     // "container/creative_inventory/tab_items"
    Type type = Type::Category;
    bool top = true;
    int column = 0;
    bool aligned_right = false;
    bool show_title = true;
    bool can_scroll = true;
    int32_t icon = 0;
    std::vector<ItemStack> items;
    // A stack's own name where it differs from its item's: enchanted books,
    // potions, paintings.
    std::vector<std::string> names;
};

struct CreativeTabs {
    std::vector<CreativeTab> tabs;
    std::vector<ItemStack> search;
    std::vector<std::string> search_names;
    bool load(const std::filesystem::path& path, const ItemTable& items);
    static std::filesystem::path default_path(int32_t protocol);
};

class CreativeModeInventoryScreen : public AbstractContainerScreen {
public:
    CreativeModeInventoryScreen(const CreativeTabs* tabs, bool operator_tab);
    void init() override;
    void tick() override;
    void render(GuiGraphics& gfx, double mx, double my) override;
    bool mouse_clicked(double mx, double my, int button) override;
    bool mouse_released(double mx, double my, int button) override;
    bool mouse_dragged(double mx, double my, int button, double dx, double dy) override;
    bool mouse_scrolled(double mx, double my, double dx, double dy) override;
    bool key_pressed(const KeyEvent& event) override;
    bool key_released(const KeyEvent& event) override;
    bool char_typed(uint32_t code, int mods) override;

    size_t selected() const { return selected_; }
    void select_tab(size_t tab);
    float scroll_offset() const { return scroll_; }

protected:
    ContainerMenu* menu() const override;
    void render_labels(GuiGraphics& gfx) override;
    void render_bg(GuiGraphics& gfx, double mx, double my) override;
    bool has_clicked_outside(double mx, double my, int left, int top) const override;
    void slot_clicked(const ScreenSlot* slot, int index, int button, ClickType type) override;

private:
    const CreativeTab& tab() const { return tabs_->tabs[selected_]; }
    bool shown(const CreativeTab& tab) const;
    int tab_x(const CreativeTab& tab) const;
    int tab_y(const CreativeTab& tab) const;
    bool tab_clicked(const CreativeTab& tab, double x, double y) const;
    void render_tab_button(GuiGraphics& gfx, const CreativeTab& tab);
    bool inside_scrollbar(double mx, double my) const;
    bool can_scroll() const;
    int row_count() const;
    int row_for_scroll(float scroll) const;
    void scroll_to(float scroll);
    void refresh_search_results();
    void sync_creative_slots(const std::vector<std::pair<int16_t, ItemStack>>& changed);
    std::vector<std::vector<Run>> creative_tooltip(const GuiGraphics& gfx, const ItemStack& stack,
                                                   int picker_index) const;

    const CreativeTabs* tabs_;
    bool operator_tab_;
    size_t selected_ = 0;
    float scroll_ = 0.0f;
    bool scrolling_ = false;
    bool ignore_text_input_ = false;
    mutable bool has_clicked_outside_ = false;
    std::unique_ptr<EditBox> search_;
    std::vector<ItemStack> items_;
    std::vector<std::string> item_names_;
    // The picker's own menu: 45 slots of the tab, then the hotbar.
    mutable ContainerMenu picker_;
    std::vector<ScreenSlot> picker_slots_;
    std::vector<ScreenSlot> inventory_slots_;
};

} // namespace mcpp::ui
