#include "mcpp/ui/container_screen.hpp"

#include "mcpp/keybinds.hpp"

#include <algorithm>
#include <cmath>

namespace mcpp::ui {
namespace {

bool empty(const ItemStack& stack) { return stack.id <= 0 || stack.count <= 0; }

constexpr uint32_t kLabelColour = 0xFF404040u;

} // namespace

bool binding_matches(const ScreenManager* manager, std::string_view name, const KeyEvent& event) {
    if (manager == nullptr || manager->keybinds == nullptr) return false;
    const Keybind* bind = manager->keybinds->find(name);
    return bind != nullptr && bind->current.type == InputKey::Type::Keyboard &&
           bind->current.code == event.key;
}

bool binding_matches_mouse(const ScreenManager* manager, std::string_view name, int button) {
    if (manager == nullptr || manager->keybinds == nullptr) return false;
    const Keybind* bind = manager->keybinds->find(name);
    return bind != nullptr && bind->current.type == InputKey::Type::Mouse &&
           bind->current.code == button;
}

std::string World::item_name(const GuiGraphics& gfx, int32_t id) const {
    if (items == nullptr) return {};
    const auto key = items->name_key(id);
    return key.empty() ? std::string(items->name(id)) : gfx.tr(key);
}

AbstractContainerScreen::AbstractContainerScreen(Component name, Component inventory_name)
    : Screen(std::move(name)), inventory_title_(std::move(inventory_name)) {}

World* AbstractContainerScreen::world() const {
    return manager != nullptr ? manager->world : nullptr;
}

ContainerMenu* AbstractContainerScreen::menu() const {
    World* w = world();
    return w != nullptr ? w->menu : nullptr;
}

// `AbstractContainerScreen.init`: centres the container image.
void AbstractContainerScreen::init() {
    left_pos = (width - image_width) / 2;
    top_pos = (height - image_height) / 2;
}

// `AbstractContainerScreen.renderBackground`: the screen background, then the container image.
void AbstractContainerScreen::render_background(GuiGraphics& gfx, double mx, double my) {
    Screen::render_background(gfx, mx, my);
    render_bg(gfx, mx, my);
}

// `AbstractContainerScreen.render`: contents, then the carried item.
void AbstractContainerScreen::render(GuiGraphics& gfx, double mx, double my) {
    render_contents(gfx, mx, my);
    render_carried_item(gfx, mx, my);
}

// `AbstractContainerScreen.renderContents`, `AbstractContainerScreen.renderSlotHighlightBack`, `AbstractContainerScreen.renderSlotHighlightFront`: widgets, labels, slots and the hover highlight.
void AbstractContainerScreen::render_contents(GuiGraphics& gfx, double mx, double my) {
    Screen::render(gfx, mx, my);
    gfx.push_pose();
    gfx.translate(static_cast<float>(left_pos), static_cast<float>(top_pos));
    render_labels(gfx);
    hovered_slot = slot_at(mx, my);
    if (hovered_slot != nullptr) {
        gfx.blit_sprite("container/slot_highlight_back", hovered_slot->x - 4, hovered_slot->y - 4,
                        24, 24);
    }
    render_slots(gfx);
    if (hovered_slot != nullptr) {
        gfx.blit_sprite("container/slot_highlight_front", hovered_slot->x - 4,
                        hovered_slot->y - 4, 24, 24);
    }
    gfx.pop_pose();
}

// `AbstractContainerScreen.renderLabels`: the title and inventory labels.
void AbstractContainerScreen::render_labels(GuiGraphics& gfx) {
    gfx.draw_string(title, title_label_x, title_label_y, kLabelColour, false);
    gfx.draw_string(inventory_title_, inventory_label_x, inventory_label_y, kLabelColour, false);
}

// `AbstractContainerScreen.renderSlots`: every active slot.
void AbstractContainerScreen::render_slots(GuiGraphics& gfx) {
    for (const auto& slot : slots) {
        if (slot.active) render_slot(gfx, slot);
    }
}

// `AbstractContainerScreen.renderSlot`: a slot's item, the drag preview, or its empty icon.
void AbstractContainerScreen::render_slot(GuiGraphics& gfx, const ScreenSlot& slot) {
    ContainerMenu* m = menu();
    if (m == nullptr || static_cast<size_t>(slot.index) >= m->size()) return;
    ItemStack stack = m->slots[static_cast<size_t>(slot.index)];
    const ItemStack& carried = m->carried;
    bool dragging_over = false;
    std::string text;
    bool has_text = false;
    if (is_quick_crafting_ &&
        std::find(quick_craft_slots_.begin(), quick_craft_slots_.end(), slot.index) !=
            quick_craft_slots_.end() &&
        !empty(carried)) {
        if (quick_craft_slots_.size() == 1) return;
        if (m->can_quick_replace(slot.index, carried, true)) {
            dragging_over = true;
            const int cap = std::min(m->max_stack_of(carried),
                                     m->slot_max(static_cast<size_t>(slot.index), carried));
            const int had = empty(stack) ? 0 : stack.count;
            int now = ContainerMenu::quick_craft_place_count(quick_craft_slots_.size(),
                                                             quick_craft_type_, carried,
                                                             m->max_stack_of(carried)) +
                      had;
            if (now > cap) {
                now = cap;
                text = "\xc2\xa7" "e" + std::to_string(cap);
                has_text = true;
            }
            stack = carried;
            stack.count = now;
        }
    }
    if (empty(stack) && !slot.empty_icon.empty()) {
        gfx.blit_sprite(slot.empty_icon, slot.x, slot.y, 16, 16);
        return;
    }
    if (dragging_over) gfx.fill(slot.x, slot.y, slot.x + 16, slot.y + 16, 0x80FFFFFFu);
    gfx.render_item(stack.id, slot.x, slot.y);
    gfx.render_item_decorations(stack.id, stack.count, slot.x, slot.y, has_text ? &text : nullptr);
}

// `AbstractContainerScreen.renderCarriedItem`, `AbstractContainerScreen.renderFloatingItem`: the item on the cursor, showing what a drag leaves.
void AbstractContainerScreen::render_carried_item(GuiGraphics& gfx, double mx, double my) {
    ContainerMenu* m = menu();
    if (m == nullptr || empty(m->carried)) return;
    ItemStack stack = m->carried;
    std::string text;
    bool has_text = false;
    if (is_quick_crafting_ && quick_craft_slots_.size() > 1) {
        stack.count = quick_craft_remainder_;
        if (stack.count <= 0) {
            text = "\xc2\xa7" "e0";
            has_text = true;
            stack.count = 1;
        }
    }
    const int x = static_cast<int>(mx) - 8;
    const int y = static_cast<int>(my) - 8;
    gfx.render_item(stack.id, x, y);
    gfx.render_item_decorations(stack.id, has_text ? 1 : stack.count, x, y,
                                has_text ? &text : nullptr);
}

std::vector<std::vector<Run>> AbstractContainerScreen::tooltip_of(const GuiGraphics& gfx,
                                                                 const ItemStack& stack) const {
    World* w = world();
    std::vector<std::vector<Run>> lines;
    if (w == nullptr || w->items == nullptr) return lines;
    Style style;
    style.colour = ItemTable::rarity_colour(w->items->rarity(stack.id));
    lines.push_back(gfx.runs(literal(w->item_name(gfx, stack.id), style)));
    return lines;
}

// `AbstractContainerScreen.renderTooltip`: the hovered item's tooltip when the hand is empty.
void AbstractContainerScreen::render_tooltip(GuiGraphics& gfx, double mx, double my) {
    ContainerMenu* m = menu();
    if (m == nullptr || hovered_slot == nullptr) return;
    const ItemStack& stack = m->slots[static_cast<size_t>(hovered_slot->index)];
    if (empty(stack) || !empty(m->carried)) return;
    gfx.set_tooltip_for_next_frame(tooltip_of(gfx, stack), static_cast<int>(mx),
                                   static_cast<int>(my));
}

// `AbstractContainerScreen.isHovering`: a rectangle with a one-pixel margin.
bool AbstractContainerScreen::is_hovering(int x, int y, int w, int h, double mx, double my) const {
    mx -= left_pos;
    my -= top_pos;
    return mx >= x - 1 && mx < x + w + 1 && my >= y - 1 && my < y + h + 1;
}

// `AbstractContainerScreen.getHoveredSlot`: the active slot under the mouse.
const ScreenSlot* AbstractContainerScreen::slot_at(double mx, double my) const {
    for (const auto& slot : slots) {
        if (slot.active && is_hovering(slot.x, slot.y, 16, 16, mx, my)) return &slot;
    }
    return nullptr;
}

// `AbstractContainerScreen.hasClickedOutside`: outside the container image.
bool AbstractContainerScreen::has_clicked_outside(double mx, double my, int left, int top) const {
    return mx < left || my < top || mx >= left + image_width || my >= top + image_height;
}

// `AbstractContainerScreen.slotClicked`, `MultiPlayerGameMode.handleInventoryMouseClick`: clicks the menu and sends what changed.
void AbstractContainerScreen::slot_clicked(const ScreenSlot* slot, int index, int button,
                                           ClickType type) {
    if (slot != nullptr) index = slot->index;
    World* w = world();
    ContainerMenu* m = menu();
    if (w == nullptr || m == nullptr) return;
    const auto before = m->slots;
    m->clicked(index, button, type);
    if (w->click) w->click(index, button, type, m->changed_since(before));
}

// `AbstractContainerScreen.checkHotbarMouseClicked`: hotbar and offhand swaps bound to mouse buttons.
void AbstractContainerScreen::check_hotbar_mouse_clicked(int button) {
    ContainerMenu* m = menu();
    if (hovered_slot == nullptr || m == nullptr || !empty(m->carried)) return;
    if (binding_matches_mouse(manager, "key.swapOffhand", button)) {
        slot_clicked(hovered_slot, hovered_slot->index, 40, ClickType::Swap);
        return;
    }
    for (int i = 0; i < 9; ++i) {
        if (binding_matches_mouse(manager, "key.hotbar." + std::to_string(i + 1), button)) {
            slot_clicked(hovered_slot, hovered_slot->index, i, ClickType::Swap);
        }
    }
}

// `AbstractContainerScreen.mouseClicked`: pickup, quick move, clone and throw, or starts a drag.
bool AbstractContainerScreen::mouse_clicked(double mx, double my, int button) {
    if (Screen::mouse_clicked(mx, my, button)) return true;
    ContainerMenu* m = menu();
    World* w = world();
    if (m == nullptr || w == nullptr) return true;
    const bool clone = binding_matches_mouse(manager, "key.pickItem", button) && w->creative();
    const ScreenSlot* slot = slot_at(mx, my);
    double_click_ = last_click_slot_ == slot && double_click;
    skip_next_release_ = false;
    if (button != 0 && button != 1 && !clone) {
        check_hotbar_mouse_clicked(button);
    } else {
        const bool outside = has_clicked_outside(mx, my, left_pos, top_pos);
        int index = slot != nullptr ? slot->index : -1;
        if (outside) index = -999;
        if (index != -1 && !is_quick_crafting_) {
            if (empty(m->carried)) {
                if (clone) {
                    slot_clicked(slot, index, button, ClickType::Clone);
                } else {
                    const bool shift = manager != nullptr && manager->shift_down;
                    ClickType type = ClickType::Pickup;
                    if (index != -999 && shift) {
                        last_quick_moved_ = slot != nullptr ? m->slots[static_cast<size_t>(slot->index)]
                                                            : ItemStack{};
                        type = ClickType::QuickMove;
                    } else if (index == -999) {
                        type = ClickType::Throw;
                    }
                    slot_clicked(slot, index, button, type);
                }
                skip_next_release_ = true;
            } else {
                is_quick_crafting_ = true;
                quick_craft_button_ = button;
                quick_craft_slots_.clear();
                if (button == 0) {
                    quick_craft_type_ = 0;
                } else if (button == 1) {
                    quick_craft_type_ = 1;
                } else if (clone) {
                    quick_craft_type_ = 2;
                }
            }
        }
    }
    last_click_slot_ = slot;
    return true;
}

// `AbstractContainerScreen.mouseDragged`: adds slots to a drag.
bool AbstractContainerScreen::mouse_dragged(double mx, double my, int button, double dx,
                                            double dy) {
    ContainerMenu* m = menu();
    if (m == nullptr) return true;
    const ScreenSlot* slot = slot_at(mx, my);
    const ItemStack& carried = m->carried;
    if (is_quick_crafting_ && slot != nullptr && !empty(carried) &&
        (carried.count > static_cast<int>(quick_craft_slots_.size()) || quick_craft_type_ == 2) &&
        m->can_quick_replace(slot->index, carried, true) &&
        m->may_place(static_cast<size_t>(slot->index), carried)) {
        if (std::find(quick_craft_slots_.begin(), quick_craft_slots_.end(), slot->index) ==
            quick_craft_slots_.end()) {
            quick_craft_slots_.push_back(slot->index);
        }
        recalculate_quick_craft_remaining();
        return true;
    }
    if (slot == nullptr && empty(carried)) return Screen::mouse_dragged(mx, my, button, dx, dy);
    return true;
}

// `AbstractContainerScreen.recalculateQuickCraftRemaining`: what a drag leaves on the cursor.
void AbstractContainerScreen::recalculate_quick_craft_remaining() {
    ContainerMenu* m = menu();
    if (m == nullptr || empty(m->carried) || !is_quick_crafting_) return;
    const ItemStack& carried = m->carried;
    if (quick_craft_type_ == 2) {
        quick_craft_remainder_ = m->max_stack_of(carried);
        return;
    }
    quick_craft_remainder_ = carried.count;
    for (const int index : quick_craft_slots_) {
        const ItemStack& here = m->slots[static_cast<size_t>(index)];
        const int had = empty(here) ? 0 : here.count;
        const int cap = std::min(m->max_stack_of(carried),
                                 m->slot_max(static_cast<size_t>(index), carried));
        const int now = std::min(ContainerMenu::quick_craft_place_count(
                                     quick_craft_slots_.size(), quick_craft_type_, carried,
                                     m->max_stack_of(carried)) +
                                     had,
                                 cap);
        quick_craft_remainder_ -= now - had;
    }
}

// `AbstractContainerScreen.mouseReleased`: double clicks, finishes a drag, or places the carried stack.
bool AbstractContainerScreen::mouse_released(double mx, double my, int button) {
    Screen::mouse_released(mx, my, button);
    ContainerMenu* m = menu();
    if (m == nullptr) return true;
    const ScreenSlot* slot = slot_at(mx, my);
    const bool outside = has_clicked_outside(mx, my, left_pos, top_pos);
    int index = slot != nullptr ? slot->index : -1;
    if (outside) index = -999;
    const bool shift = manager != nullptr && manager->shift_down;

    if (double_click_ && slot != nullptr && button == 0 && !m->rule(static_cast<size_t>(slot->index)).result) {
        if (shift) {
            if (!empty(last_quick_moved_)) {
                for (const auto& other : slots) {
                    const ItemStack& here = m->slots[static_cast<size_t>(other.index)];
                    if (!empty(here) && m->can_quick_replace(other.index, last_quick_moved_, true) &&
                        m->rule(static_cast<size_t>(other.index)).container ==
                            m->rule(static_cast<size_t>(slot->index)).container) {
                        slot_clicked(&other, other.index, button, ClickType::QuickMove);
                    }
                }
            }
        } else {
            slot_clicked(slot, index, button, ClickType::PickupAll);
        }
        double_click_ = false;
    } else {
        if (is_quick_crafting_ && quick_craft_button_ != button) {
            is_quick_crafting_ = false;
            quick_craft_slots_.clear();
            skip_next_release_ = true;
            return true;
        }
        if (skip_next_release_) {
            skip_next_release_ = false;
            return true;
        }
        if (is_quick_crafting_ && !quick_craft_slots_.empty()) {
            slot_clicked(nullptr, -999, ContainerMenu::quick_craft_mask(0, quick_craft_type_),
                         ClickType::QuickCraft);
            const auto chosen = quick_craft_slots_;
            for (const int target : chosen) {
                const ScreenSlot* screen_slot = nullptr;
                for (const auto& candidate : slots) {
                    if (candidate.index == target) screen_slot = &candidate;
                }
                slot_clicked(screen_slot, target,
                             ContainerMenu::quick_craft_mask(1, quick_craft_type_),
                             ClickType::QuickCraft);
            }
            slot_clicked(nullptr, -999, ContainerMenu::quick_craft_mask(2, quick_craft_type_),
                         ClickType::QuickCraft);
        } else if (!empty(m->carried)) {
            if (binding_matches_mouse(manager, "key.pickItem", button)) {
                slot_clicked(slot, index, button, ClickType::Clone);
            } else {
                const bool quick = index != -999 && shift;
                if (quick) {
                    last_quick_moved_ = slot != nullptr ? m->slots[static_cast<size_t>(slot->index)]
                                                        : ItemStack{};
                }
                slot_clicked(slot, index, button, quick ? ClickType::QuickMove : ClickType::Pickup);
            }
        }
    }
    is_quick_crafting_ = false;
    return true;
}

// `AbstractContainerScreen.checkHotbarKeyPressed`: hotbar and offhand swap keys on the hovered slot.
bool AbstractContainerScreen::check_hotbar_key_pressed(const KeyEvent& event) {
    ContainerMenu* m = menu();
    if (m == nullptr || !empty(m->carried) || hovered_slot == nullptr) return false;
    if (binding_matches(manager, "key.swapOffhand", event)) {
        slot_clicked(hovered_slot, hovered_slot->index, 40, ClickType::Swap);
        return true;
    }
    for (int i = 0; i < 9; ++i) {
        if (binding_matches(manager, "key.hotbar." + std::to_string(i + 1), event)) {
            slot_clicked(hovered_slot, hovered_slot->index, i, ClickType::Swap);
            return true;
        }
    }
    return false;
}

// `AbstractContainerScreen.keyPressed`: inventory key closes, swaps, pick and drop on the hovered slot.
bool AbstractContainerScreen::key_pressed(const KeyEvent& event) {
    if (Screen::key_pressed(event)) return true;
    if (binding_matches(manager, "key.inventory", event)) {
        on_close();
        return true;
    }
    check_hotbar_key_pressed(event);
    ContainerMenu* m = menu();
    if (m != nullptr && hovered_slot != nullptr &&
        !empty(m->slots[static_cast<size_t>(hovered_slot->index)])) {
        if (binding_matches(manager, "key.pickItem", event)) {
            slot_clicked(hovered_slot, hovered_slot->index, 0, ClickType::Clone);
        } else if (binding_matches(manager, "key.drop", event)) {
            slot_clicked(hovered_slot, hovered_slot->index, event.control() ? 1 : 0,
                         ClickType::Throw);
        }
    }
    return false;
}

// `AbstractContainerScreen.onClose`: closes the container on the server, then the screen.
void AbstractContainerScreen::on_close() {
    World* w = world();
    ContainerMenu* m = menu();
    if (w != nullptr && m != nullptr && w->close_container) w->close_container(m->container_id);
    Screen::on_close();
}

void AbstractContainerScreen::removed() {}

// `RecipeBookComponent.init`: opens as the recipe book data says.
void RecipeBookComponent::init(int width, int height, World* world, const ScreenManager* manager,
                               bool width_too_narrow) {
    world_ = world;
    manager_ = manager;
    width_ = width;
    height_ = height;
    width_too_narrow_ = width_too_narrow;
    inventory_seen_ = inventory_snapshot();
    visible_ = world_ != nullptr && world_->recipe_book.open;
    if (visible_) init_visuals();
}

std::vector<ItemStack> RecipeBookComponent::inventory_snapshot() const {
    std::vector<ItemStack> out;
    const ContainerMenu* menu = world_ != nullptr ? world_->menu : nullptr;
    if (menu == nullptr) return out;
    for (size_t i = 0; i < menu->size(); ++i) {
        const int index = menu->rule(i).inventory_index;
        if (index >= 0 && index < 36) out.push_back(menu->slots[i]);
    }
    return out;
}

// `RecipeBookComponent.initVisuals`: the stacked contents, the search box, the page and the tabs.
void RecipeBookComponent::init_visuals() {
    const bool filtering = world_ != nullptr && world_->recipe_book.filtering;
    x_offset_ = width_too_narrow_ ? 0 : 86;
    const int x = x_origin();
    const int y = y_origin();
    stacked_.clear();
    const ContainerMenu* menu = world_ != nullptr ? world_->menu : nullptr;
    if (menu != nullptr) {
        for (const auto& stack : inventory_snapshot()) stacked_.account_simple_stack(stack);
        for (int i = 0; i < menu->grid_width * menu->grid_height; ++i) {
            const ItemStack& stack = menu->slots[static_cast<size_t>(menu->grid_first + i)];
            stacked_.account_stack(stack, menu->max_stack_of(stack));
        }
    }
    const std::string kept = search_ ? search_->value() : std::string{};
    search_ = std::make_unique<EditBox>(x + 25, y + 13, 81, 9 + 5,
                                        translatable("itemGroup.search"));
    search_->max_length = 50;
    search_->text_colour = 0xFFFFFFFFu;
    search_->set_value(kept);
    Style hint;
    hint.colour = 0xAAAAAAu;
    hint.italic = true;
    search_->hint = translatable("gui.recipebook.search_hint", {}, hint);

    if (tabs_.empty() && world_ != nullptr && world_->items != nullptr) {
        const auto id = [&](std::string_view name) { return world_->items->find(name); };
        tabs_.push_back({id("compass"), 0, 0});
        tabs_.push_back({id("iron_axe"), id("golden_sword"), 1});
        tabs_.push_back({id("bricks"), 0, 2});
        tabs_.push_back({id("lava_bucket"), id("apple"), 3});
        tabs_.push_back({id("redstone"), 0, 4});
    }
    select_matching_recipes();
    update_tabs(filtering);
    update_collections(false, filtering);
}

// `RecipeBookComponent.updateScreenPosition`: where the container goes beside the book.
int RecipeBookComponent::update_screen_position(int width, int image_width) const {
    if (visible_ && !width_too_narrow_) return 177 + (width - image_width - 200) / 2;
    return (width - image_width) / 2;
}

// `RecipeBookComponent.setVisible`, `RecipeBookComponent.sendUpdateSettings`: opens or closes the book and tells the server.
void RecipeBookComponent::set_visible(bool on) {
    if (on) init_visuals();
    visible_ = on;
    if (world_ != nullptr) {
        world_->recipe_book.open = on;
        if (world_->recipe_book_changed) {
            world_->recipe_book_changed(on, world_->recipe_book.filtering);
        }
    }
}

// `RecipeBookComponent.toggleVisibility`: flips the book.
void RecipeBookComponent::toggle_visibility() { set_visible(!visible_); }

// `CraftingRecipeBookComponent.isCraftingSlot`: the result or a grid slot.
bool RecipeBookComponent::is_crafting_slot(int index) const {
    const ContainerMenu* menu = world_ != nullptr ? world_->menu : nullptr;
    if (menu == nullptr) return false;
    return index == 0 ||
           (index >= menu->grid_first && index < menu->grid_first + menu->grid_width * menu->grid_height);
}

// `RecipeBookComponent.slotClicked`: a click in the grid drops the ghost and recounts.
void RecipeBookComponent::slot_clicked(const ScreenSlot* slot) {
    if (slot == nullptr || !is_crafting_slot(slot->index)) return;
    last_placed_.reset();
    ghosts_.clear();
    if (visible_) update_stacked_contents();
}

// `CraftingRecipeBookComponent.canDisplay`: whether a recipe fits this grid.
bool RecipeBookComponent::fits(const RecipeDisplayEntry& entry) const {
    const ContainerMenu* menu = world_ != nullptr ? world_->menu : nullptr;
    const int w = menu != nullptr ? menu->grid_width : 2;
    const int h = menu != nullptr ? menu->grid_height : 2;
    switch (entry.kind) {
    case RecipeDisplayEntry::Kind::Shaped: return w >= entry.width && h >= entry.height;
    case RecipeDisplayEntry::Kind::Shapeless:
        return static_cast<size_t>(w * h) >= entry.ingredients.size();
    default: return false;
    }
}

// `RecipeBookComponent.selectMatchingRecipes`, `CraftingRecipeBookComponent.selectMatchingRecipes`: every tab's collections against the inventory.
void RecipeBookComponent::select_matching_recipes() {
    if (world_ == nullptr) return;
    for (const auto& tab : tabs_) {
        for (RecipeCollection* collection : world_->recipe_book.collection(tab.tab)) {
            collection->select_recipes(stacked_, [this](const RecipeDisplayEntry& e) { return fits(e); });
        }
    }
}

bool RecipeBookComponent::matches_search(const RecipeCollection& collection,
                                         const std::string& query) const {
    if (world_->items == nullptr) return false;
    for (const auto* entry : collection.recipes()) {
        for (const auto& stack : entry->result) {
            std::string name(world_->items->name(stack.id));
            if (name.find(query) != std::string::npos) return true;
            std::string shown = name;
            const auto key = world_->items->name_key(stack.id);
            if (manager_ != nullptr && manager_->lang != nullptr && !key.empty()) {
                if (const auto* text = manager_->lang->find(key)) shown = *text;
            }
            std::ranges::transform(shown, shown.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            if (shown.find(query) != std::string::npos) return true;
        }
    }
    return false;
}

// `RecipeBookComponent.updateCollections`: the tab's collections that fit, match the search and, filtering, can be made.
void RecipeBookComponent::update_collections(bool reset_page, bool filtering) {
    collections_.clear();
    if (world_ == nullptr || tabs_.empty()) return;
    for (const RecipeCollection* collection :
         world_->recipe_book.collection(tabs_[selected_tab_].tab)) {
        if (collection->has_any_selected()) collections_.push_back(collection);
    }
    std::string search = search_ ? search_->value() : std::string{};
    std::ranges::transform(search, search.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (!search.empty()) {
        std::erase_if(collections_, [&](const RecipeCollection* c) { return !matches_search(*c, search); });
    }
    if (filtering) {
        std::erase_if(collections_, [](const RecipeCollection* c) { return !c->has_craftable(); });
    }
    // `RecipeBookPage.updateCollections`: the page count, back to the first page when asked.
    pages_ = static_cast<int>(std::ceil(static_cast<double>(collections_.size()) / 20.0));
    if (pages_ <= page_ || reset_page) page_ = 0;
    update_buttons_for_page();
}

// `RecipeBookPage.updateButtonsForPage`, `RecipeButton.init`: what each of the twenty buttons shows.
void RecipeBookComponent::update_buttons_for_page() {
    const bool filtering = world_ != nullptr && world_->recipe_book.filtering;
    for (size_t i = 0; i < buttons_.size(); ++i) {
        Button& button = buttons_[i];
        const size_t at = static_cast<size_t>(page_) * 20 + i;
        button.visible = at < collections_.size();
        if (!button.visible) continue;
        button.collection = collections_[at];
        button.entries.clear();
        for (const auto* entry : button.collection->selected_recipes(
                 filtering ? RecipeCollection::Status::Craftable : RecipeCollection::Status::Any)) {
            button.entries.emplace_back(entry->id, entry->result);
        }
        // `RecipeButton.allRecipesHaveSameResultDisplay`: one item drawn twice.
        button.same_display = true;
        const ItemStack* first = nullptr;
        for (const auto& [id, items] : button.entries) {
            for (const auto& stack : items) {
                if (first == nullptr) {
                    first = &stack;
                } else if (stack.id != first->id || stack.count != first->count) {
                    button.same_display = false;
                }
            }
        }
        bool shown = false;
        for (const auto& [id, items] : button.entries) {
            if (world_->recipe_book.will_highlight(id)) {
                world_->recipe_book.remove_highlight(id);
                if (world_->recipe_seen) world_->recipe_seen(id);
                shown = true;
            }
        }
        if (shown) button.animation = 15.0f;
    }
}

// `RecipeBookComponent.updateTabs`, `RecipeBookTabButton.updateVisibility`, `RecipeBookTabButton.startAnimation`: stacks the visible tabs down the side.
void RecipeBookComponent::update_tabs(bool filtering) {
    const int x = (width_ - kImageWidth) / 2 - x_offset_ - 30;
    const int y = (height_ - kImageHeight) / 2 + 3;
    int index = 0;
    for (auto& tab : tabs_) {
        const auto& collections = world_->recipe_book.collection(tab.tab);
        if (tab.tab == 0) {
            tab.visible = true;
        } else {
            tab.visible = std::ranges::any_of(
                collections, [](const RecipeCollection* c) { return c->has_any_selected(); });
            if (!tab.visible) continue;
        }
        tab.x = x;
        tab.y = y + 27 * index++;
        if (tab.tab == 0) continue;
        for (const RecipeCollection* collection : collections) {
            for (const auto* entry : collection->selected_recipes(
                     filtering ? RecipeCollection::Status::Craftable : RecipeCollection::Status::Any)) {
                if (world_->recipe_book.will_highlight(entry->id)) tab.animation = 15.0f;
            }
        }
    }
}

// `RecipeBookComponent.tick`: follows the book state, and recounts when the inventory changed.
void RecipeBookComponent::tick() {
    if (world_ == nullptr) return;
    if (world_->recipe_book.open != visible_) set_visible(world_->recipe_book.open);
    if (!visible_) return;
    auto now = inventory_snapshot();
    const bool same = std::ranges::equal(now, inventory_seen_, [](const ItemStack& a, const ItemStack& b) {
        return (a.count <= 0 && b.count <= 0) || (a.id == b.id && a.count == b.count);
    });
    if (!same) {
        inventory_seen_ = std::move(now);
        update_stacked_contents();
    }
}

// `RecipeBookComponent.updateStackedContents`: recounts the inventory and the grid.
void RecipeBookComponent::update_stacked_contents() {
    stacked_.clear();
    const ContainerMenu* menu = world_ != nullptr ? world_->menu : nullptr;
    if (menu == nullptr) return;
    for (const auto& stack : inventory_snapshot()) stacked_.account_simple_stack(stack);
    for (int i = 0; i < menu->grid_width * menu->grid_height; ++i) {
        const ItemStack& stack = menu->slots[static_cast<size_t>(menu->grid_first + i)];
        stacked_.account_stack(stack, menu->max_stack_of(stack));
    }
    select_matching_recipes();
    update_collections(false, world_->recipe_book.filtering);
}

// `RecipeBookComponent.recipesUpdated`: the server changed the book.
void RecipeBookComponent::recipes_updated() {
    if (world_ == nullptr) return;
    select_matching_recipes();
    update_tabs(world_->recipe_book.filtering);
    if (visible_) update_collections(false, world_->recipe_book.filtering);
}

// `RecipeBookComponent.render`, `RecipeBookPage.render`, `RecipeBookTabButton.renderContents`, `RecipeButton.renderWidget`: the book, its tabs, filter, page and buttons.
void RecipeBookComponent::render(GuiGraphics& gfx, double mx, double my) {
    if (!visible_) return;
    const auto now = std::chrono::steady_clock::now();
    float delta = 0.0f;
    if (last_frame_.time_since_epoch().count() != 0) {
        delta = std::chrono::duration<float>(now - last_frame_).count() * 20.0f;
    }
    last_frame_ = now;
    if (manager_ == nullptr || !manager_->control_down) time_ += delta;

    const int x = x_origin();
    const int y = y_origin();
    gfx.blit("gui/recipe_book", x, y, 1.0f, 1.0f, kImageWidth, kImageHeight, 256, 256);
    search_->hovered = search_->over(mx, my);
    search_->render(gfx, mx, my);

    for (size_t i = 0; i < tabs_.size(); ++i) {
        auto& tab = tabs_[i];
        if (!tab.visible) continue;
        const bool selected = i == selected_tab_;
        tab.hovered = mx >= tab.x && my >= tab.y && mx < tab.x + 35 && my < tab.y + 27;
        const bool animating = tab.animation > 0.0f;
        if (animating) {
            const float s = 1.0f + 0.1f * static_cast<float>(std::sin(tab.animation / 15.0f * 3.1415927f));
            gfx.push_pose();
            gfx.translate(static_cast<float>(tab.x + 8), static_cast<float>(tab.y + 12));
            gfx.scale_by(1.0f, s);
            gfx.translate(static_cast<float>(-(tab.x + 8)), static_cast<float>(-(tab.y + 12)));
        }
        gfx.blit_sprite(selected ? "recipe_book/tab_selected" : "recipe_book/tab",
                        tab.x - (selected ? 2 : 0), tab.y, 35, 27);
        const int shift = selected ? -2 : 0;
        if (tab.secondary > 0) {
            gfx.render_item(tab.primary, tab.x + 3 + shift, tab.y + 5);
            gfx.render_item(tab.secondary, tab.x + 14 + shift, tab.y + 5);
        } else {
            gfx.render_item(tab.primary, tab.x + 9 + shift, tab.y + 5);
        }
        if (animating) {
            gfx.pop_pose();
            tab.animation -= delta;
        }
    }

    const bool filtering = world_ != nullptr && world_->recipe_book.filtering;
    filter_hovered_ = mx >= x + 110 && my >= y + 12 && mx < x + 110 + 26 && my < y + 12 + 16;
    std::string filter = filtering ? "recipe_book/filter_enabled" : "recipe_book/filter_disabled";
    if (filter_hovered_) filter += "_highlighted";
    gfx.blit_sprite(filter, x + 110, y + 12, 26, 16);

    if (pages_ > 1) {
        const auto label = gfx.runs(translatable(
            "gui.recipebook.page", {literal(std::to_string(page_ + 1)), literal(std::to_string(pages_))}));
        gfx.draw_string(label, x - gfx.width_of(label) / 2 + 73, y + 141, 0xFFFFFFFFu);
    }
    hovered_button_ = -1;
    for (int i = 0; i < 20; ++i) {
        Button& button = buttons_[static_cast<size_t>(i)];
        if (!button.visible || button.entries.empty()) continue;
        const auto [bx, by] = button_position(i);
        const bool many = button.entries.size() > 1;
        const char* sprite = button.collection->has_craftable()
                                 ? (many ? "recipe_book/slot_many_craftable" : "recipe_book/slot_craftable")
                                 : (many ? "recipe_book/slot_many_uncraftable"
                                         : "recipe_book/slot_uncraftable");
        const bool animating = button.animation > 0.0f;
        if (animating) {
            const float s = 1.0f + 0.1f * static_cast<float>(std::sin(button.animation / 15.0f * 3.1415927f));
            gfx.push_pose();
            gfx.translate(static_cast<float>(bx + 8), static_cast<float>(by + 12));
            gfx.scale_by(s, s);
            gfx.translate(static_cast<float>(-(bx + 8)), static_cast<float>(-(by + 12)));
            button.animation -= delta;
        }
        gfx.blit_sprite(sprite, bx, by, 25, 25);
        // `RecipeButton.getDisplayStack`: the recipes take turns, then each one's results.
        const int index = current_index();
        const int n = static_cast<int>(button.entries.size());
        const int outer = index / n;
        const auto& items = button.entries[static_cast<size_t>(index - n * outer)].second;
        const int32_t shown = items.empty() ? 0 : items[static_cast<size_t>(outer) % items.size()].id;
        int offset = 4;
        if (many && button.same_display) {
            gfx.render_item(shown, bx + offset + 1, by + offset + 1);
            --offset;
        }
        gfx.render_item(shown, bx + offset, by + offset);
        if (animating) gfx.pop_pose();
        if (mx >= bx && my >= by && mx < bx + 25 && my < by + 25) hovered_button_ = i;
    }
    const bool forward = pages_ > 1 && page_ < pages_ - 1;
    const bool back = pages_ > 1 && page_ > 0;
    const auto arrow = [&](bool shown, int ax, const char* base) {
        if (!shown) return;
        const bool over = mx >= ax && my >= y + 137 && mx < ax + 12 && my < y + 137 + 17;
        gfx.blit_sprite(std::string(base) + (over ? "_highlighted" : ""), ax, y + 137, 12, 17);
    };
    arrow(forward, x + 93, "recipe_book/page_forward");
    arrow(back, x + 38, "recipe_book/page_backward");
}

// `RecipeBookComponent.RecipeBookComponent`: which of a cycle's items is shown, a new one every 30 ticks.
int RecipeBookComponent::current_index() const { return static_cast<int>(std::floor(time_ / 30.0f)); }

std::pair<int, int> RecipeBookComponent::button_position(int i) const {
    return {x_origin() + 11 + 25 * (i % 5), y_origin() + 31 + 25 * (i / 5)};
}

std::vector<RecipeBookComponent::Shown> RecipeBookComponent::page() const {
    std::vector<Shown> out;
    for (const auto& button : buttons_) {
        if (!button.visible || button.entries.empty()) continue;
        const auto& items = button.entries.front().second;
        out.push_back({items.empty() ? 0 : items.front().id, button.collection->has_craftable(),
                       button.entries.size() > 1});
    }
    return out;
}

// `RecipeBookComponent.renderTooltip`, `RecipeBookPage.renderTooltip`, `GhostSlots.renderTooltip`: the hovered recipe, or the ghost under the pointer.
void RecipeBookComponent::render_tooltip(GuiGraphics& gfx, double mx, double my,
                                         const ScreenSlot* hovered) {
    if (!visible_ || world_ == nullptr) return;
    const auto name_lines = [&](int32_t id) {
        Style style;
        style.colour = world_->items != nullptr ? ItemTable::rarity_colour(world_->items->rarity(id))
                                                : 0xFFFFFFu;
        std::vector<std::vector<Run>> lines;
        lines.push_back(gfx.runs(literal(world_->item_name(gfx, id), style)));
        return lines;
    };
    if (hovered_button_ >= 0) {
        const Button& button = buttons_[static_cast<size_t>(hovered_button_)];
        const int index = current_index();
        const int n = static_cast<int>(button.entries.size());
        const int outer = index / n;
        const auto& items = button.entries[static_cast<size_t>(index - n * outer)].second;
        const int32_t shown = items.empty() ? 0 : items[static_cast<size_t>(outer) % items.size()].id;
        auto lines = name_lines(shown);
        if (n > 1) {
            Style hint;
            hint.colour = 0xFFFF55u;
            lines.push_back(gfx.runs(translatable("gui.recipebook.moreRecipes", {}, hint)));
        }
        gfx.set_tooltip_for_next_frame(lines, static_cast<int>(mx), static_cast<int>(my));
    } else if (filter_hovered_) {
        gfx.set_tooltip_for_next_frame(
            translatable(world_->recipe_book.filtering ? "gui.recipebook.toggleRecipes.craftable"
                                                       : "gui.recipebook.toggleRecipes.all"),
            static_cast<int>(mx), static_cast<int>(my));
    }
    if (hovered == nullptr) return;
    for (const auto& ghost : ghosts_) {
        if (ghost.slot != hovered->index) continue;
        const auto& stack = ghost.items[static_cast<size_t>(current_index()) % ghost.items.size()];
        gfx.set_tooltip_for_next_frame(name_lines(stack.id), static_cast<int>(mx),
                                       static_cast<int>(my));
    }
}

// `GhostSlots.render`: the recipe's items, faded, in the slots the player has yet to fill.
void RecipeBookComponent::render_ghost_recipe(GuiGraphics& gfx, const std::vector<ScreenSlot>& slots,
                                              bool bigger_result) {
    for (const auto& ghost : ghosts_) {
        const ScreenSlot* slot = nullptr;
        for (const auto& candidate : slots) {
            if (candidate.index == ghost.slot) slot = &candidate;
        }
        if (slot == nullptr) continue;
        const int x = slot->x;
        const int y = slot->y;
        if (ghost.result && bigger_result) {
            gfx.fill(x - 4, y - 4, x + 20, y + 20, 0x30FF0000u);
        } else {
            gfx.fill(x, y, x + 16, y + 16, 0x30FF0000u);
        }
        const auto& stack = ghost.items[static_cast<size_t>(current_index()) % ghost.items.size()];
        gfx.render_item(stack.id, x, y);
        gfx.fill(x, y, x + 16, y + 16, 0x30FFFFFFu);
        if (ghost.result) gfx.render_item_decorations(stack.id, stack.count, x, y, nullptr);
    }
}

// `GhostSlots.setSlot`: a slot's ghost, when its display shows anything.
void RecipeBookComponent::set_ghost(int slot, const std::vector<ItemStack>& items, bool result) {
    if (items.empty()) return;
    for (auto& ghost : ghosts_) {
        if (ghost.slot == slot) {
            ghost.items = items;
            ghost.result = result;
            return;
        }
    }
    ghosts_.push_back({slot, items, result});
}

// `RecipeBookComponent.fillGhostRecipe`, `CraftingRecipeBookComponent.fillGhostRecipe`: the result and the ingredients where the recipe puts them.
void RecipeBookComponent::fill_ghost_recipe(const RecipeDisplayEntry& display) {
    ghosts_.clear();
    const ContainerMenu* menu = world_ != nullptr ? world_->menu : nullptr;
    if (menu == nullptr) return;
    set_ghost(0, display.result, true);
    if (display.kind == RecipeDisplayEntry::Kind::Shaped) {
        for (const auto& [ingredient, slot] :
             place_recipe(menu->grid_width, menu->grid_height, display.width, display.height,
                          display.ingredients.size())) {
            set_ghost(menu->grid_first + slot, display.ingredients[ingredient], false);
        }
    } else if (display.kind == RecipeDisplayEntry::Kind::Shapeless) {
        const size_t n = std::min(display.ingredients.size(),
                                  static_cast<size_t>(menu->grid_width * menu->grid_height));
        for (size_t i = 0; i < n; ++i) {
            set_ghost(menu->grid_first + static_cast<int>(i), display.ingredients[i], false);
        }
    }
}

// `RecipeBookComponent.tryPlaceRecipe`, `MultiPlayerGameMode.handlePlaceRecipe`: asks the server to fill the grid.
bool RecipeBookComponent::try_place_recipe(const RecipeCollection* collection, int32_t recipe,
                                           bool shift) {
    if (!collection->is_craftable(recipe) && last_placed_ == recipe) return false;
    last_placed_ = recipe;
    ghosts_.clear();
    if (world_->place_recipe && world_->menu != nullptr) {
        world_->place_recipe(world_->menu->container_id, recipe, shift);
    }
    return true;
}

// `RecipeBookComponent.mouseClicked`, `RecipeBookPage.mouseClicked`: page arrows, a recipe, the search box, the filter, the tabs.
bool RecipeBookComponent::mouse_clicked(double mx, double my, int button, bool shift) {
    if (!visible_ || world_ == nullptr || world_->game_mode == 3) return false;
    const int x = x_origin();
    const int y = y_origin();
    const bool forward = pages_ > 1 && page_ < pages_ - 1;
    const bool back = pages_ > 1 && page_ > 0;
    if (button == 0 && forward && mx >= x + 93 && my >= y + 137 && mx < x + 105 && my < y + 154) {
        play_sound("minecraft:ui.button.click");
        ++page_;
        update_buttons_for_page();
        return true;
    }
    if (button == 0 && back && mx >= x + 38 && my >= y + 137 && mx < x + 50 && my < y + 154) {
        play_sound("minecraft:ui.button.click");
        --page_;
        update_buttons_for_page();
        return true;
    }
    for (int i = 0; i < 20; ++i) {
        const Button& b = buttons_[static_cast<size_t>(i)];
        if (!b.visible || b.entries.empty() || (button != 0 && button != 1)) continue;
        const auto [bx, by] = button_position(i);
        if (mx < bx || my < by || mx >= bx + 25 || my >= by + 25) continue;
        play_sound("minecraft:ui.button.click");
        if (button != 0) return true;
        // `RecipeButton.getCurrentRecipe`: the recipe showing right now.
        const int32_t recipe =
            b.entries[static_cast<size_t>(current_index()) % b.entries.size()].first;
        if (!try_place_recipe(b.collection, recipe, shift)) return false;
        last_collection_ = b.collection;
        last_recipe_ = recipe;
        if (x_offset_ != 86) set_visible(false);
        return true;
    }
    const bool magnifier = mx >= x + 8 && mx < x + 25 && my >= y + 13 && my < y + 13 + 14;
    if (magnifier || search_->mouse_clicked(mx, my, button)) {
        search_->set_focused(true);
        return true;
    }
    search_->set_focused(false);
    if (button == 0 && mx >= x + 110 && my >= y + 12 && mx < x + 136 && my < y + 28) {
        play_sound("minecraft:ui.button.click");
        world_->recipe_book.filtering = !world_->recipe_book.filtering;
        if (world_->recipe_book_changed) {
            world_->recipe_book_changed(visible_, world_->recipe_book.filtering);
        }
        update_collections(false, world_->recipe_book.filtering);
        return true;
    }
    for (size_t i = 0; i < tabs_.size(); ++i) {
        const auto& tab = tabs_[i];
        if (!tab.visible || button != 0) continue;
        if (mx >= tab.x && my >= tab.y && mx < tab.x + 35 && my < tab.y + 27) {
            play_sound("minecraft:ui.button.click");
            if (selected_tab_ != i) {
                selected_tab_ = i;
                update_collections(true, world_->recipe_book.filtering);
            }
            return true;
        }
    }
    return false;
}

// `RecipeBookComponent.keyPressed`, `RecipeBookComponent.checkSearchStringUpdate`: escape, the search box, the chat key and placing the last recipe again.
bool RecipeBookComponent::key_pressed(const KeyEvent& event, const ScreenManager* manager) {
    ignore_text_input_ = false;
    if (!visible_ || world_ == nullptr || world_->game_mode == 3) return false;
    if (event.key == key::Escape && x_offset_ != 86) {
        set_visible(false);
        return true;
    }
    if (search_->key_pressed(event)) {
        std::string lower = search_->value();
        std::ranges::transform(lower, lower.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (lower != last_search_) {
            update_collections(false, world_->recipe_book.filtering);
            last_search_ = lower;
        }
        return true;
    }
    if (search_->focused && event.key != key::Escape) return true;
    if (binding_matches(manager, "key.chat", event) && !search_->focused) {
        ignore_text_input_ = true;
        search_->set_focused(true);
        return true;
    }
    const bool selection = event.key == key::Enter || event.key == key::Space ||
                           event.key == key::KeypadEnter;
    if (selection && last_collection_ != nullptr && last_recipe_) {
        play_sound("minecraft:ui.button.click");
        return try_place_recipe(last_collection_, *last_recipe_, event.shift());
    }
    return false;
}

// `RecipeBookComponent.charTyped`: typing into the search box.
bool RecipeBookComponent::char_typed(uint32_t code, int mods) {
    if (ignore_text_input_ || !visible_ || world_ == nullptr || world_->game_mode == 3) {
        return false;
    }
    if (search_->char_typed(code, mods)) {
        std::string lower = search_->value();
        std::ranges::transform(lower, lower.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (lower != last_search_) {
            update_collections(false, world_->recipe_book.filtering);
            last_search_ = lower;
        }
        return true;
    }
    return false;
}

// `RecipeBookComponent.hasClickedOutside`: outside both the container and the book.
bool RecipeBookComponent::has_clicked_outside(double mx, double my, int left, int top, int w,
                                              int h) const {
    if (!visible_) return true;
    const bool outside = mx < left || my < top || mx >= left + w || my >= top + h;
    const bool beside = left - kImageWidth < mx && mx < left && top < my && my < top + h;
    const bool on_tab = !tabs_.empty() && tabs_[selected_tab_].hovered;
    return outside && !beside && !on_tab;
}

// `AbstractRecipeBookScreen.init`, `AbstractRecipeBookScreen.initButton`: the book, the container beside it and its button.
void AbstractRecipeBookScreen::init() {
    AbstractContainerScreen::init();
    width_too_narrow_ = width < 379;
    recipe_book.init(width, height, world(), manager, width_too_narrow_);
    left_pos = recipe_book.update_screen_position(width, image_width);
    const auto [bx, by] = book_button_position();
    add<SpriteButton>(bx, by, 20, 18, "recipe_book/button", "recipe_book/button_highlighted",
                      Component{}, [this](Button& self) {
                          recipe_book.toggle_visibility();
                          left_pos = recipe_book.update_screen_position(width, image_width);
                          const auto [x, y] = book_button_position();
                          self.x = x;
                          self.y = y;
                          on_recipe_book_button_click();
                      });
}

// `AbstractRecipeBookScreen.containerTick`: ticks the book.
void AbstractRecipeBookScreen::tick() {
    AbstractContainerScreen::tick();
    recipe_book.tick();
}

// `AbstractRecipeBookScreen.render`: contents or book, carried item, then tooltips.
void AbstractRecipeBookScreen::render(GuiGraphics& gfx, double mx, double my) {
    if (recipe_book.visible() && width_too_narrow_) {
        // The book covers the container on a narrow window; only it is drawn.
    } else {
        render_contents(gfx, mx, my);
    }
    recipe_book.render(gfx, mx, my);
    render_carried_item(gfx, mx, my);
    render_tooltip(gfx, mx, my);
    recipe_book.render_tooltip(gfx, mx, my, hovered_slot);
}

// `AbstractRecipeBookScreen.renderSlots`: the slots, then the ghost recipe over them.
void AbstractRecipeBookScreen::render_slots(GuiGraphics& gfx) {
    AbstractContainerScreen::render_slots(gfx);
    recipe_book.render_ghost_recipe(gfx, slots, is_bigger_result_slot());
}

// `AbstractRecipeBookScreen.mouseClicked`: the book first; a narrow open book takes every click.
bool AbstractRecipeBookScreen::mouse_clicked(double mx, double my, int button) {
    const bool shift = manager != nullptr && manager->shift_down;
    if (recipe_book.mouse_clicked(mx, my, button, shift)) return true;
    if (width_too_narrow_ && recipe_book.visible()) return true;
    return AbstractContainerScreen::mouse_clicked(mx, my, button);
}

// `AbstractRecipeBookScreen.keyPressed`: the book first.
bool AbstractRecipeBookScreen::key_pressed(const KeyEvent& event) {
    if (recipe_book.key_pressed(event, manager)) return true;
    return AbstractContainerScreen::key_pressed(event);
}

// `AbstractRecipeBookScreen.charTyped`: the book first.
bool AbstractRecipeBookScreen::char_typed(uint32_t code, int mods) {
    if (recipe_book.char_typed(code, mods)) return true;
    return AbstractContainerScreen::char_typed(code, mods);
}

// `AbstractRecipeBookScreen.isHovering`: nothing hovers under a narrow open book.
bool AbstractRecipeBookScreen::is_hovering(int x, int y, int w, int h, double mx, double my) const {
    return (!width_too_narrow_ || !recipe_book.visible()) &&
           AbstractContainerScreen::is_hovering(x, y, w, h, mx, my);
}

// `AbstractRecipeBookScreen.hasClickedOutside`: outside the container and the book.
bool AbstractRecipeBookScreen::has_clicked_outside(double mx, double my, int left, int top) const {
    const bool outside =
        mx < left || my < top || mx >= left + image_width || my >= top + image_height;
    return recipe_book.has_clicked_outside(mx, my, left_pos, top_pos, image_width, image_height) &&
           outside;
}

// `AbstractRecipeBookScreen.slotClicked`: the click, then the book hears of it.
void AbstractRecipeBookScreen::slot_clicked(const ScreenSlot* slot, int index, int button,
                                            ClickType type) {
    AbstractContainerScreen::slot_clicked(slot, index, button, type);
    recipe_book.slot_clicked(slot);
}

InventoryScreen::InventoryScreen()
    : AbstractRecipeBookScreen(translatable("container.crafting"), translatable("container.inventory")) {
    title_label_x = 97;
    slots.push_back({0, 154, 28, {}});
    for (int row = 0; row < 2; ++row) {
        for (int column = 0; column < 2; ++column) {
            slots.push_back({1 + column + row * 2, 98 + column * 18, 18 + row * 18, {}});
        }
    }
    constexpr const char* armour[4] = {"container/slot/helmet", "container/slot/chestplate",
                                       "container/slot/leggings", "container/slot/boots"};
    for (int i = 0; i < 4; ++i) slots.push_back({5 + i, 8, 8 + i * 18, armour[i]});
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 9; ++column) {
            slots.push_back({9 + column + row * 9, 8 + column * 18, 84 + row * 18, {}});
        }
    }
    for (int column = 0; column < 9; ++column) {
        slots.push_back({36 + column, 8 + column * 18, 84 + 58, {}});
    }
    slots.push_back({45, 77, 62, "container/slot/shield"});
}

// `InventoryScreen.getRecipeBookButtonPosition`: beside the crafting grid.
std::pair<int, int> InventoryScreen::book_button_position() const {
    return {left_pos + 104, height / 2 - 22};
}

// `InventoryScreen.renderLabels`: only the crafting title.
void InventoryScreen::render_labels(GuiGraphics& gfx) {
    gfx.draw_string(title, title_label_x, title_label_y, kLabelColour, false);
}

// `InventoryScreen.renderBg`: the inventory image and the player following the mouse.
void InventoryScreen::render_bg(GuiGraphics& gfx, double, double) {
    gfx.blit("container/inventory", left_pos, top_pos, 0.0f, 0.0f, image_width, image_height, 256,
             256);
    World* w = world();
    if (w != nullptr && w->draw_player) {
        w->draw_player(gfx, left_pos + 26, top_pos + 8, left_pos + 75, top_pos + 78, 30, 0.0625f,
                       x_mouse_, y_mouse_);
    }
}

// `InventoryScreen.render`: the book screen, remembering the mouse for the player model.
void InventoryScreen::render(GuiGraphics& gfx, double mx, double my) {
    AbstractRecipeBookScreen::render(gfx, mx, my);
    x_mouse_ = static_cast<float>(mx);
    y_mouse_ = static_cast<float>(my);
}

// `InventoryScreen.mouseReleased`: swallows the release after the book button.
bool InventoryScreen::mouse_released(double mx, double my, int button) {
    if (button_clicked_) {
        button_clicked_ = false;
        Screen::mouse_released(mx, my, button);
        return true;
    }
    return AbstractContainerScreen::mouse_released(mx, my, button);
}

// `CraftingMenu.CraftingMenu`, `AbstractCraftingMenu.addResultSlot`, `AbstractCraftingMenu.addCraftingGridSlots`, `AbstractContainerMenu.addStandardInventorySlots`: the result, the grid and the player's slots.
CraftingScreen::CraftingScreen(Component title)
    : AbstractRecipeBookScreen(std::move(title), translatable("container.inventory")) {
    slots.push_back({0, 124, 35, {}});
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            slots.push_back({1 + column + row * 3, 30 + column * 18, 17 + row * 18, {}});
        }
    }
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 9; ++column) {
            slots.push_back({10 + column + row * 9, 8 + column * 18, 84 + row * 18, {}});
        }
    }
    for (int column = 0; column < 9; ++column) {
        slots.push_back({37 + column, 8 + column * 18, 84 + 58, {}});
    }
}

// `CraftingScreen.init`: the title moves right of the book button.
void CraftingScreen::init() {
    AbstractRecipeBookScreen::init();
    title_label_x = 29;
}

// `CraftingScreen.getRecipeBookButtonPosition`: left of the grid.
std::pair<int, int> CraftingScreen::book_button_position() const {
    return {left_pos + 5, height / 2 - 49};
}

// `CraftingScreen.renderBg`: the crafting table image.
void CraftingScreen::render_bg(GuiGraphics& gfx, double, double) {
    gfx.blit("container/crafting_table", left_pos, (height - image_height) / 2, 0.0f, 0.0f,
             image_width, image_height, 256, 256);
}

} // namespace mcpp::ui
