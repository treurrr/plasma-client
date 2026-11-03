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

void AbstractContainerScreen::init() {
    left_pos = (width - image_width) / 2;
    top_pos = (height - image_height) / 2;
}

void AbstractContainerScreen::render_background(GuiGraphics& gfx, double mx, double my) {
    Screen::render_background(gfx, mx, my);
    render_bg(gfx, mx, my);
}

void AbstractContainerScreen::render(GuiGraphics& gfx, double mx, double my) {
    render_contents(gfx, mx, my);
    render_carried_item(gfx, mx, my);
}

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

void AbstractContainerScreen::render_labels(GuiGraphics& gfx) {
    gfx.draw_string(title, title_label_x, title_label_y, kLabelColour, false);
    gfx.draw_string(inventory_title_, inventory_label_x, inventory_label_y, kLabelColour, false);
}

void AbstractContainerScreen::render_slots(GuiGraphics& gfx) {
    for (const auto& slot : slots) {
        if (slot.active) render_slot(gfx, slot);
    }
}

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

void AbstractContainerScreen::render_tooltip(GuiGraphics& gfx, double mx, double my) {
    ContainerMenu* m = menu();
    if (m == nullptr || hovered_slot == nullptr) return;
    const ItemStack& stack = m->slots[static_cast<size_t>(hovered_slot->index)];
    if (empty(stack) || !empty(m->carried)) return;
    gfx.set_tooltip_for_next_frame(tooltip_of(gfx, stack), static_cast<int>(mx),
                                   static_cast<int>(my));
}

bool AbstractContainerScreen::is_hovering(int x, int y, int w, int h, double mx, double my) const {
    mx -= left_pos;
    my -= top_pos;
    return mx >= x - 1 && mx < x + w + 1 && my >= y - 1 && my < y + h + 1;
}

const ScreenSlot* AbstractContainerScreen::slot_at(double mx, double my) const {
    for (const auto& slot : slots) {
        if (slot.active && is_hovering(slot.x, slot.y, 16, 16, mx, my)) return &slot;
    }
    return nullptr;
}

bool AbstractContainerScreen::has_clicked_outside(double mx, double my, int left, int top) const {
    return mx < left || my < top || mx >= left + image_width || my >= top + image_height;
}

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
                        ((other.index >= 9) == (slot->index >= 9))) {
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

void AbstractContainerScreen::on_close() {
    World* w = world();
    ContainerMenu* m = menu();
    if (w != nullptr && m != nullptr && w->close_container) w->close_container(m->container_id);
    Screen::on_close();
}

void AbstractContainerScreen::removed() {}

void RecipeBookComponent::init(int width, int height, World* world, bool width_too_narrow) {
    world_ = world;
    width_ = width;
    height_ = height;
    width_too_narrow_ = width_too_narrow;
    visible_ = world_ != nullptr && world_->recipe_book.open;
    if (visible_) init_visuals();
}

void RecipeBookComponent::init_visuals() {
    x_offset_ = width_too_narrow_ ? 0 : 86;
    const int x = x_origin();
    const int y = y_origin();
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
        tabs_.push_back({id("compass"), 0, -1});
        tabs_.push_back({id("iron_axe"), id("golden_sword"), 0});
        tabs_.push_back({id("bricks"), 0, 1});
        tabs_.push_back({id("lava_bucket"), id("apple"), 2});
        tabs_.push_back({id("redstone"), 0, 3});
    }
    update_tabs();
    update_collections(false);
}

int RecipeBookComponent::update_screen_position(int width, int image_width) const {
    if (visible_ && !width_too_narrow_) return 177 + (width - image_width - 200) / 2;
    return (width - image_width) / 2;
}

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

void RecipeBookComponent::toggle_visibility() { set_visible(!visible_); }

void RecipeBookComponent::tick() {
    if (world_ != nullptr && world_->recipe_book.open != visible_) {
        set_visible(world_->recipe_book.open);
    }
}

void RecipeBookComponent::update_tabs() {
    const int x = (width_ - kImageWidth) / 2 - x_offset_ - 30;
    const int y = (height_ - kImageHeight) / 2 + 3;
    int index = 0;
    for (auto& tab : tabs_) {
        if (tab.category < 0) {
            tab.visible = true;
        } else {
            tab.visible = world_ != nullptr &&
                          !world_->recipe_book.categories[static_cast<size_t>(tab.category)].empty();
        }
        if (tab.visible) {
            tab.x = x;
            tab.y = y + 27 * index++;
        }
    }
}

void RecipeBookComponent::update_collections(bool reset_page) {
    shown_.clear();
    if (world_ == nullptr) return;
    const auto& book = world_->recipe_book;
    const int category = tabs_.empty() ? -1 : tabs_[selected_tab_].category;
    for (size_t c = 0; c < book.categories.size(); ++c) {
        if (category >= 0 && static_cast<int>(c) != category) continue;
        for (const auto& entry : book.categories[c]) shown_.push_back(entry);
    }
    const std::string search = search_ ? search_->value() : std::string{};
    if (!search.empty() && world_->items != nullptr) {
        std::string needle = search;
        std::ranges::transform(needle, needle.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        std::erase_if(shown_, [&](const RecipeEntry& entry) {
            std::string name(world_->items->name(entry.result));
            std::ranges::replace(name, '_', ' ');
            return name.find(needle) == std::string::npos;
        });
    }
    if (book.filtering) std::erase_if(shown_, [](const RecipeEntry& e) { return !e.craftable; });
    pages_ = static_cast<int>(std::ceil(static_cast<double>(shown_.size()) / 20.0));
    if (pages_ <= page_ || reset_page) page_ = 0;
}

void RecipeBookComponent::render(GuiGraphics& gfx, double mx, double my) {
    if (!visible_) return;
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
        gfx.blit_sprite(selected ? "recipe_book/tab_selected" : "recipe_book/tab",
                        tab.x - (selected ? 2 : 0), tab.y, 35, 27);
        const int shift = selected ? -2 : 0;
        if (tab.secondary > 0) {
            gfx.render_item(tab.primary, tab.x + 3 + shift, tab.y + 5);
            gfx.render_item(tab.secondary, tab.x + 14 + shift, tab.y + 5);
        } else {
            gfx.render_item(tab.primary, tab.x + 9 + shift, tab.y + 5);
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
    hovered_recipe_ = -1;
    for (int i = 0; i < 20; ++i) {
        const size_t at = static_cast<size_t>(page_ * 20 + i);
        if (at >= shown_.size()) break;
        const RecipeEntry& entry = shown_[at];
        const int bx = x + 11 + 25 * (i % 5);
        const int by = y + 31 + 25 * (i / 5);
        const char* sprite = entry.craftable
                                 ? (entry.many ? "recipe_book/slot_many_craftable"
                                               : "recipe_book/slot_craftable")
                                 : (entry.many ? "recipe_book/slot_many_uncraftable"
                                               : "recipe_book/slot_uncraftable");
        gfx.blit_sprite(sprite, bx, by, 25, 25);
        const int offset = 4;
        gfx.render_item(entry.result, bx + offset, by + offset);
        if (mx >= bx && my >= by && mx < bx + 25 && my < by + 25) hovered_recipe_ = i;
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

void RecipeBookComponent::render_tooltip(GuiGraphics& gfx, double mx, double my) {
    if (!visible_ || world_ == nullptr) return;
    if (hovered_recipe_ >= 0) {
        const RecipeEntry& entry = shown_[static_cast<size_t>(page_ * 20 + hovered_recipe_)];
        Style style;
        style.colour = world_->items != nullptr
                           ? ItemTable::rarity_colour(world_->items->rarity(entry.result))
                           : 0xFFFFFFu;
        std::vector<std::vector<Run>> lines;
        lines.push_back(gfx.runs(literal(world_->item_name(gfx, entry.result), style)));
        if (entry.many) {
            Style hint;
            hint.colour = 0xFFFF55u;
            lines.push_back(gfx.runs(translatable("gui.recipebook.moreRecipes", {}, hint)));
        }
        gfx.set_tooltip_for_next_frame(lines, static_cast<int>(mx), static_cast<int>(my));
        return;
    }
    if (filter_hovered_) {
        const bool filtering = world_->recipe_book.filtering;
        gfx.set_tooltip_for_next_frame(
            translatable(filtering ? "gui.recipebook.toggleRecipes.craftable"
                                   : "gui.recipebook.toggleRecipes.all"),
            static_cast<int>(mx), static_cast<int>(my));
    }
}

bool RecipeBookComponent::mouse_clicked(double mx, double my, int button) {
    if (!visible_ || world_ == nullptr || world_->game_mode == 3) return false;
    const int x = x_origin();
    const int y = y_origin();
    const bool forward = pages_ > 1 && page_ < pages_ - 1;
    const bool back = pages_ > 1 && page_ > 0;
    if (button == 0 && forward && mx >= x + 93 && my >= y + 137 && mx < x + 105 && my < y + 154) {
        play_sound("minecraft:ui.button.click");
        ++page_;
        return true;
    }
    if (button == 0 && back && mx >= x + 38 && my >= y + 137 && mx < x + 50 && my < y + 154) {
        play_sound("minecraft:ui.button.click");
        --page_;
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
        update_collections(false);
        return true;
    }
    for (size_t i = 0; i < tabs_.size(); ++i) {
        const auto& tab = tabs_[i];
        if (!tab.visible || button != 0) continue;
        if (mx >= tab.x && my >= tab.y && mx < tab.x + 35 && my < tab.y + 27) {
            play_sound("minecraft:ui.button.click");
            if (selected_tab_ != i) {
                selected_tab_ = i;
                update_collections(true);
            }
            return true;
        }
    }
    return false;
}

bool RecipeBookComponent::key_pressed(const KeyEvent& event, const ScreenManager* manager) {
    ignore_text_input_ = false;
    if (!visible_ || world_ == nullptr || world_->game_mode == 3) return false;
    if (event.key == key::Escape && x_offset_ != 86) {
        set_visible(false);
        return true;
    }
    if (search_->key_pressed(event)) {
        if (search_->value() != last_search_) {
            last_search_ = search_->value();
            update_collections(false);
        }
        return true;
    }
    if (search_->focused && event.key != key::Escape) return true;
    if (binding_matches(manager, "key.chat", event) && !search_->focused) {
        ignore_text_input_ = true;
        search_->set_focused(true);
        return true;
    }
    return false;
}

bool RecipeBookComponent::char_typed(uint32_t code, int mods) {
    if (ignore_text_input_ || !visible_ || world_ == nullptr || world_->game_mode == 3) {
        return false;
    }
    if (search_->char_typed(code, mods)) {
        if (search_->value() != last_search_) {
            last_search_ = search_->value();
            update_collections(false);
        }
        return true;
    }
    return false;
}

bool RecipeBookComponent::has_clicked_outside(double mx, double my, int left, int top, int w,
                                              int h) const {
    if (!visible_) return true;
    const bool outside = mx < left || my < top || mx >= left + w || my >= top + h;
    const bool beside = left - kImageWidth < mx && mx < left && top < my && my < top + h;
    const bool on_tab = !tabs_.empty() && tabs_[selected_tab_].hovered;
    return outside && !beside && !on_tab;
}

InventoryScreen::InventoryScreen()
    : AbstractContainerScreen(translatable("container.crafting"), translatable("container.inventory")) {
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

void InventoryScreen::init() {
    AbstractContainerScreen::init();
    width_too_narrow_ = width < 379;
    recipe_book.init(width, height, world(), width_too_narrow_);
    left_pos = recipe_book.update_screen_position(width, image_width);
    book_button_ = &add<SpriteButton>(
        left_pos + 104, height / 2 - 22, 20, 18, "recipe_book/button",
        "recipe_book/button_highlighted", Component{}, [this](Button& self) {
            recipe_book.toggle_visibility();
            left_pos = recipe_book.update_screen_position(width, image_width);
            self.x = left_pos + 104;
            self.y = height / 2 - 22;
            button_clicked_ = true;
        });
}

void InventoryScreen::tick() {
    AbstractContainerScreen::tick();
    recipe_book.tick();
}

void InventoryScreen::render_labels(GuiGraphics& gfx) {
    gfx.draw_string(title, title_label_x, title_label_y, kLabelColour, false);
}

void InventoryScreen::render_bg(GuiGraphics& gfx, double, double) {
    gfx.blit("container/inventory", left_pos, top_pos, 0.0f, 0.0f, image_width, image_height, 256,
             256);
    World* w = world();
    if (w != nullptr && w->draw_player) {
        w->draw_player(gfx, left_pos + 26, top_pos + 8, left_pos + 75, top_pos + 78, 30, 0.0625f,
                       x_mouse_, y_mouse_);
    }
}

void InventoryScreen::render(GuiGraphics& gfx, double mx, double my) {
    if (recipe_book.visible() && width_too_narrow_) {
        // The book covers the inventory on a narrow window; only it is drawn.
    } else {
        render_contents(gfx, mx, my);
    }
    recipe_book.render(gfx, mx, my);
    render_carried_item(gfx, mx, my);
    render_tooltip(gfx, mx, my);
    recipe_book.render_tooltip(gfx, mx, my);
    x_mouse_ = static_cast<float>(mx);
    y_mouse_ = static_cast<float>(my);
}

bool InventoryScreen::mouse_clicked(double mx, double my, int button) {
    if (recipe_book.mouse_clicked(mx, my, button)) return true;
    if (width_too_narrow_ && recipe_book.visible()) return true;
    return AbstractContainerScreen::mouse_clicked(mx, my, button);
}

bool InventoryScreen::mouse_released(double mx, double my, int button) {
    if (button_clicked_) {
        button_clicked_ = false;
        Screen::mouse_released(mx, my, button);
        return true;
    }
    return AbstractContainerScreen::mouse_released(mx, my, button);
}

bool InventoryScreen::key_pressed(const KeyEvent& event) {
    if (recipe_book.key_pressed(event, manager)) return true;
    return AbstractContainerScreen::key_pressed(event);
}

bool InventoryScreen::char_typed(uint32_t code, int mods) {
    if (recipe_book.char_typed(code, mods)) return true;
    return AbstractContainerScreen::char_typed(code, mods);
}

bool InventoryScreen::is_hovering(int x, int y, int w, int h, double mx, double my) const {
    return (!width_too_narrow_ || !recipe_book.visible()) &&
           AbstractContainerScreen::is_hovering(x, y, w, h, mx, my);
}

bool InventoryScreen::has_clicked_outside(double mx, double my, int left, int top) const {
    const bool outside =
        mx < left || my < top || mx >= left + image_width || my >= top + image_height;
    return recipe_book.has_clicked_outside(mx, my, left_pos, top_pos, image_width, image_height) &&
           outside;
}

} // namespace mcpp::ui
