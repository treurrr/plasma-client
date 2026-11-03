#include "mcpp/ui/container_screen.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace mcpp::ui {
namespace {

bool empty(const ItemStack& stack) { return stack.id <= 0 || stack.count <= 0; }

constexpr uint32_t kLabelColour = 0xFF404040u;

std::vector<SlotRule> picker_rules() {
    std::vector<SlotRule> rules(54);
    for (int i = 0; i < 45; ++i) rules[static_cast<size_t>(i)].accepts = SlotRule::Accepts::Nothing;
    for (int i = 0; i < 9; ++i) rules[static_cast<size_t>(45 + i)].inventory_index = i;
    return rules;
}

std::string lower(std::string text) {
    std::ranges::transform(text, text.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

} // namespace

std::filesystem::path CreativeTabs::default_path(int32_t protocol) {
    return ItemTable::default_path(protocol).parent_path() /
           ("creative-" + std::to_string(protocol) + ".txt");
}

bool CreativeTabs::load(const std::filesystem::path& path, const ItemTable& items) {
    std::ifstream in(path);
    if (!in) return false;
    tabs.clear();
    search.clear();
    std::string line;
    while (std::getline(in, line)) {
        std::vector<std::string> f;
        std::istringstream fields(line);
        for (std::string part; std::getline(fields, part, '\t');) f.push_back(part);
        if (f.empty()) continue;
        if (f[0] == "tab" && f.size() >= 12) {
            CreativeTab tab;
            tab.key = f[1];
            tab.type = f[2] == "INVENTORY" ? CreativeTab::Type::Inventory
                       : f[2] == "HOTBAR"  ? CreativeTab::Type::Hotbar
                       : f[2] == "SEARCH"  ? CreativeTab::Type::Search
                                           : CreativeTab::Type::Category;
            tab.top = f[3] == "TOP";
            tab.column = std::atoi(f[4].c_str());
            tab.aligned_right = f[5] == "true";
            tab.show_title = f[6] == "true";
            tab.can_scroll = f[7] == "true";
            std::string background = f[9];
            if (background.starts_with("textures/gui/")) background = background.substr(13);
            if (background.ends_with(".png")) background.resize(background.size() - 4);
            tab.background = background;
            tab.icon = items.find(f[10]);
            tab.title = f[11];
            tabs.push_back(std::move(tab));
        } else if ((f[0] == "item" || f[0] == "search") && f.size() >= 4) {
            ItemStack stack;
            stack.id = items.find(f[1]);
            stack.count = std::atoi(f[2].c_str());
            if (stack.id <= 0) continue;
            const std::string name = f[3] == "-" ? std::string{} : f[3];
            if (f[0] == "item" && !tabs.empty()) {
                tabs.back().items.push_back(stack);
                tabs.back().names.push_back(name);
            } else if (f[0] == "search") {
                search.push_back(stack);
                search_names.push_back(name);
            }
        }
    }
    return !tabs.empty();
}

CreativeModeInventoryScreen::CreativeModeInventoryScreen(const CreativeTabs* tabs,
                                                         bool operator_tab)
    : AbstractContainerScreen(Component{}, Component{}), tabs_(tabs), operator_tab_(operator_tab),
      picker_(picker_rules(), nullptr) {
    image_height = 136;
    image_width = 195;
    for (int row = 0; row < 5; ++row) {
        for (int column = 0; column < 9; ++column) {
            picker_slots_.push_back({row * 9 + column, 9 + column * 18, 18 + row * 18, {}});
        }
    }
    for (int i = 0; i < 9; ++i) picker_slots_.push_back({45 + i, 9 + i * 18, 112, {}});
    constexpr const char* armour[4] = {"container/slot/helmet", "container/slot/chestplate",
                                       "container/slot/leggings", "container/slot/boots"};
    for (int i = 5; i < 46; ++i) {
        ScreenSlot slot{i, 0, 0, {}};
        if (i < 9) {
            slot.x = 54 + (i - 5) / 2 * 54;
            slot.y = 6 + (i - 5) % 2 * 27;
            slot.empty_icon = armour[i - 5];
        } else if (i == 45) {
            slot.x = 35;
            slot.y = 20;
            slot.empty_icon = "container/slot/shield";
        } else {
            slot.x = 9 + (i - 9) % 9 * 18;
            slot.y = i >= 36 ? 112 : 54 + (i - 9) / 9 * 18;
        }
        inventory_slots_.push_back(slot);
    }
    // The bin: index 46 is past the inventory menu, and only this screen knows it.
    inventory_slots_.push_back({46, 173, 112, {}});
}

ContainerMenu* CreativeModeInventoryScreen::menu() const {
    if (tabs_ != nullptr && !tabs_->tabs.empty() && tab().type == CreativeTab::Type::Inventory) {
        return AbstractContainerScreen::menu();
    }
    if (ContainerMenu* inventory = AbstractContainerScreen::menu()) {
        for (int i = 0; i < 9; ++i) {
            picker_.slots[static_cast<size_t>(45 + i)] = inventory->slots[static_cast<size_t>(36 + i)];
        }
        picker_.carried = inventory->carried;
    }
    return &picker_;
}

bool CreativeModeInventoryScreen::shown(const CreativeTab& t) const {
    return t.key != "op_blocks" || operator_tab_;
}

// `CreativeModeInventoryScreen.init`: the borderless search box, then the remembered tab.
void CreativeModeInventoryScreen::init() {
    AbstractContainerScreen::init();
    if (World* w = world()) picker_ = ContainerMenu(picker_rules(), w->items);
    search_ = std::make_unique<EditBox>(left_pos + 82, top_pos + 6, 80, 9,
                                        translatable("itemGroup.search"));
    search_->max_length = 50;
    search_->bordered = false;
    search_->visible = false;
    search_->text_colour = 0xFFFFFFFFu;
    if (tabs_ == nullptr || tabs_->tabs.empty()) return;
    const size_t keep = selected_;
    selected_ = 0;
    select_tab(keep);
}

// `CreativeModeInventoryScreen.containerTick`: back to the survival inventory without creative.
void CreativeModeInventoryScreen::tick() {
    AbstractContainerScreen::tick();
    World* w = world();
    if (w != nullptr && !w->creative() && manager != nullptr) {
        manager->set_screen(std::make_shared<InventoryScreen>());
    }
}

// `CreativeModeInventoryScreen.ItemPickerMenu.calculateRowCount`: rows past the five shown.
int CreativeModeInventoryScreen::row_count() const {
    return (static_cast<int>(items_.size()) + 8) / 9 - 5;
}

// `CreativeModeInventoryScreen.ItemPickerMenu.getRowIndexForScroll`: the first row for a scroll.
int CreativeModeInventoryScreen::row_for_scroll(float scroll) const {
    return std::max(static_cast<int>(scroll * static_cast<float>(row_count()) + 0.5), 0);
}

// `CreativeModeInventoryScreen.ItemPickerMenu.scrollTo`: fills the picker from that row.
void CreativeModeInventoryScreen::scroll_to(float scroll) {
    const int row = row_for_scroll(scroll);
    for (int r = 0; r < 5; ++r) {
        for (int c = 0; c < 9; ++c) {
            const int at = c + (r + row) * 9;
            picker_.slots[static_cast<size_t>(c + r * 9)] =
                at >= 0 && at < static_cast<int>(items_.size()) ? items_[static_cast<size_t>(at)]
                                                               : ItemStack{};
        }
    }
}

// `CreativeModeInventoryScreen.canScroll`, `CreativeModeInventoryScreen.ItemPickerMenu.canScroll`: more than 45 items in a scrolling tab.
bool CreativeModeInventoryScreen::can_scroll() const {
    return tab().can_scroll && items_.size() > 45;
}

// `CreativeModeInventoryScreen.selectTab`: the tab's items and slots, the search box, scroll to the top.
void CreativeModeInventoryScreen::select_tab(size_t index) {
    const size_t previous = selected_;
    selected_ = index;
    items_.clear();
    item_names_.clear();
    const auto& t = tab();
    if (t.type == CreativeTab::Type::Category) {
        items_ = t.items;
        item_names_ = t.names;
    }
    slots = t.type == CreativeTab::Type::Inventory ? inventory_slots_ : picker_slots_;
    if (t.type == CreativeTab::Type::Search) {
        search_->visible = true;
        search_->set_focused(true);
        if (previous != index) search_->set_value("");
        refresh_search_results();
    } else {
        search_->visible = false;
        search_->set_focused(false);
        search_->set_value("");
    }
    scroll_ = 0.0f;
    scroll_to(0.0f);
}

void CreativeModeInventoryScreen::refresh_search_results() {
    items_.clear();
    item_names_.clear();
    const std::string query = lower(search_->value());
    World* w = world();
    const auto& source = tabs_->search;
    for (size_t i = 0; i < source.size(); ++i) {
        if (!query.empty() && w != nullptr && w->items != nullptr) {
            const std::string& own = tabs_->search_names[i];
            const std::string name = lower(own.empty() ? std::string(w->items->name(source[i].id)) : own);
            std::string spaced = name;
            std::ranges::replace(spaced, '_', ' ');
            std::string shown = lower(w->items->name_key(source[i].id).empty()
                                          ? std::string{}
                                          : std::string(w->items->name_key(source[i].id)));
            if (manager != nullptr && manager->lang != nullptr && own.empty()) {
                if (const auto* text = manager->lang->find(w->items->name_key(source[i].id))) {
                    shown = lower(*text);
                }
            }
            if (spaced.find(query) == std::string::npos && shown.find(query) == std::string::npos &&
                name.find(query) == std::string::npos) {
                continue;
            }
        }
        items_.push_back(source[i]);
        item_names_.push_back(tabs_->search_names[i]);
    }
    scroll_ = 0.0f;
    scroll_to(0.0f);
}

// `CreativeModeInventoryScreen.getTabX`: a tab's column.
int CreativeModeInventoryScreen::tab_x(const CreativeTab& t) const {
    return t.aligned_right ? image_width - 27 * (7 - t.column) + 1 : 27 * t.column;
}

// `CreativeModeInventoryScreen.getTabY`: above or below the image.
int CreativeModeInventoryScreen::tab_y(const CreativeTab& t) const {
    return t.top ? -32 : image_height;
}

// `CreativeModeInventoryScreen.checkTabClicked`: inside a tab.
bool CreativeModeInventoryScreen::tab_clicked(const CreativeTab& t, double x, double y) const {
    const int tx = tab_x(t);
    const int ty = tab_y(t);
    return x >= tx && x <= tx + 26 && y >= ty && y <= ty + 32;
}

// `CreativeModeInventoryScreen.insideScrollbar`: inside the scrollbar track.
bool CreativeModeInventoryScreen::inside_scrollbar(double mx, double my) const {
    const int x0 = left_pos + 175;
    const int y0 = top_pos + 18;
    return mx >= x0 && my >= y0 && mx < x0 + 14 && my < y0 + 112;
}

// `CreativeModeInventoryScreen.renderTabButton`: a tab sprite and its icon.
void CreativeModeInventoryScreen::render_tab_button(GuiGraphics& gfx, const CreativeTab& t) {
    const bool selected = &t == &tab();
    const int x = left_pos + tab_x(t);
    const int y = top_pos - (t.top ? 28 : -(image_height - 4));
    const int column = std::clamp(t.column, 0, 6) + 1;
    const std::string sprite = std::string("container/creative_inventory/tab_") +
                               (t.top ? "top_" : "bottom_") +
                               (selected ? "selected_" : "unselected_") + std::to_string(column);
    gfx.blit_sprite(sprite, x, y, 26, 32);
    gfx.render_item(t.icon, x + 13 - 8, y + 16 - 8 + (t.top ? 1 : -1));
}

// `CreativeModeInventoryScreen.renderBg`: other tabs, background, search box, scroller, the selected tab and the player.
void CreativeModeInventoryScreen::render_bg(GuiGraphics& gfx, double mx, double my) {
    for (const auto& t : tabs_->tabs) {
        if (&t != &tab() && shown(t)) render_tab_button(gfx, t);
    }
    gfx.blit(tab().background, left_pos, top_pos, 0.0f, 0.0f, image_width, image_height, 256, 256);
    search_->render(gfx, mx, my);
    const int x0 = left_pos + 175;
    const int y0 = top_pos + 18;
    const int y1 = y0 + 112;
    if (tab().can_scroll) {
        gfx.blit_sprite(can_scroll() ? "container/creative_inventory/scroller"
                                     : "container/creative_inventory/scroller_disabled",
                        x0, y0 + static_cast<int>(static_cast<float>(y1 - y0 - 17) * scroll_), 12,
                        15);
    }
    render_tab_button(gfx, tab());
    World* w = world();
    if (tab().type == CreativeTab::Type::Inventory && w != nullptr && w->draw_player) {
        w->draw_player(gfx, left_pos + 73, top_pos + 6, left_pos + 105, top_pos + 49, 20, 0.0625f,
                       static_cast<float>(mx), static_cast<float>(my));
    }
}

// `CreativeModeInventoryScreen.renderLabels`: the tab title when it shows one.
void CreativeModeInventoryScreen::render_labels(GuiGraphics& gfx) {
    if (tab().show_title) gfx.draw_string(translatable(tab().title), 8, 6, kLabelColour, false);
}

// `CreativeModeInventoryScreen.getTooltipFromContainerItem`: the item tooltip plus the tabs that hold it.
std::vector<std::vector<Run>> CreativeModeInventoryScreen::creative_tooltip(
    const GuiGraphics& gfx, const ItemStack& stack, int picker_index) const {
    auto lines = tooltip_of(gfx, stack);
    if (picker_index >= 0) {
        const int row = row_for_scroll(scroll_);
        const size_t at = static_cast<size_t>(picker_index + row * 9);
        if (at < item_names_.size() && !item_names_[at].empty() && !lines.empty()) {
            Style style = lines[0].empty() ? Style{} : lines[0][0].style;
            lines[0] = gfx.runs(literal(item_names_[at], style));
        }
    }
    if (lines.empty()) return lines;
    const bool picker = picker_index >= 0;
    if (tab().type == CreativeTab::Type::Category && picker) return lines;
    size_t insert = 1;
    for (const auto& t : tabs_->tabs) {
        if (t.type != CreativeTab::Type::Category || !shown(t)) continue;
        const bool contains = std::ranges::any_of(t.items, [&](const ItemStack& s) { return s.id == stack.id; });
        if (!contains) continue;
        Style blue;
        blue.colour = 0x5555FFu;
        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(insert++),
                     gfx.runs(translatable(t.title, {}, blue)));
    }
    return lines;
}

// `CreativeModeInventoryScreen.render`, `CreativeModeInventoryScreen.checkTabHovering`: contents, then tab, bin and item tooltips.
void CreativeModeInventoryScreen::render(GuiGraphics& gfx, double mx, double my) {
    render_contents(gfx, mx, my);
    render_carried_item(gfx, mx, my);
    for (const auto& t : tabs_->tabs) {
        if (!shown(t)) continue;
        const int tx = tab_x(t);
        const int ty = tab_y(t);
        if (is_hovering(tx + 3, ty + 3, 21, 27, mx, my)) {
            gfx.set_tooltip_for_next_frame(translatable(t.title), static_cast<int>(mx),
                                           static_cast<int>(my));
            break;
        }
    }
    if (tab().type == CreativeTab::Type::Inventory && is_hovering(173, 112, 16, 16, mx, my)) {
        gfx.set_tooltip_for_next_frame(translatable("inventory.binSlot"), static_cast<int>(mx),
                                       static_cast<int>(my));
    }
    ContainerMenu* m = menu();
    if (m != nullptr && hovered_slot != nullptr && hovered_slot->index < static_cast<int>(m->size()) &&
        empty(m->carried)) {
        const ItemStack& stack = m->slots[static_cast<size_t>(hovered_slot->index)];
        if (!empty(stack)) {
            const bool picker = tab().type != CreativeTab::Type::Inventory && hovered_slot->index < 45;
            gfx.set_tooltip_for_next_frame(
                creative_tooltip(gfx, stack, picker ? hovered_slot->index : -1),
                static_cast<int>(mx), static_cast<int>(my));
        }
    }
}

// `CreativeModeInventoryScreen.mouseClicked`: tabs and the scrollbar take left clicks first.
bool CreativeModeInventoryScreen::mouse_clicked(double mx, double my, int button) {
    if (button == 0) {
        for (const auto& t : tabs_->tabs) {
            if (shown(t) && tab_clicked(t, mx - left_pos, my - top_pos)) return true;
        }
        if (tab().type != CreativeTab::Type::Inventory && inside_scrollbar(mx, my)) {
            scrolling_ = can_scroll();
            return true;
        }
    }
    return AbstractContainerScreen::mouse_clicked(mx, my, button);
}

// `CreativeModeInventoryScreen.mouseReleased`: releasing on a tab selects it.
bool CreativeModeInventoryScreen::mouse_released(double mx, double my, int button) {
    if (button == 0) {
        scrolling_ = false;
        for (size_t i = 0; i < tabs_->tabs.size(); ++i) {
            if (shown(tabs_->tabs[i]) && tab_clicked(tabs_->tabs[i], mx - left_pos, my - top_pos)) {
                select_tab(i);
                return true;
            }
        }
    }
    return AbstractContainerScreen::mouse_released(mx, my, button);
}

// `CreativeModeInventoryScreen.mouseDragged`: dragging the scroller.
bool CreativeModeInventoryScreen::mouse_dragged(double mx, double my, int button, double dx,
                                                double dy) {
    if (scrolling_) {
        const int y0 = top_pos + 18;
        const int y1 = y0 + 112;
        scroll_ = (static_cast<float>(my) - static_cast<float>(y0) - 7.5f) /
                  (static_cast<float>(y1 - y0) - 15.0f);
        scroll_ = std::clamp(scroll_, 0.0f, 1.0f);
        scroll_to(scroll_);
        return true;
    }
    return AbstractContainerScreen::mouse_dragged(mx, my, button, dx, dy);
}

// `CreativeModeInventoryScreen.mouseScrolled`, `CreativeModeInventoryScreen.ItemPickerMenu.subtractInputFromScroll`: scrolls a row per notch.
bool CreativeModeInventoryScreen::mouse_scrolled(double, double, double, double dy) {
    if (!can_scroll()) return false;
    scroll_ = std::clamp(scroll_ - static_cast<float>(dy / row_count()), 0.0f, 1.0f);
    scroll_to(scroll_);
    return true;
}

// `CreativeModeInventoryScreen.hasClickedOutside`: outside the image and not on the selected tab.
bool CreativeModeInventoryScreen::has_clicked_outside(double mx, double my, int left, int top) const {
    const bool outside =
        mx < left || my < top || mx >= left + image_width || my >= top + image_height;
    has_clicked_outside_ = outside && !tab_clicked(tab(), mx, my);
    return has_clicked_outside_;
}

// `CreativeModeInventoryScreen.charTyped`: typing into the search tab.
bool CreativeModeInventoryScreen::char_typed(uint32_t code, int mods) {
    if (ignore_text_input_ || tab().type != CreativeTab::Type::Search) return false;
    const std::string before = search_->value();
    if (search_->char_typed(code, mods)) {
        if (before != search_->value()) refresh_search_results();
        return true;
    }
    return false;
}

// `CreativeModeInventoryScreen.keyPressed`: the chat key opens search; hotbar keys and the search box in it.
bool CreativeModeInventoryScreen::key_pressed(const KeyEvent& event) {
    ignore_text_input_ = false;
    if (tab().type != CreativeTab::Type::Search) {
        if (binding_matches(manager, "key.chat", event)) {
            ignore_text_input_ = true;
            for (size_t i = 0; i < tabs_->tabs.size(); ++i) {
                if (tabs_->tabs[i].type == CreativeTab::Type::Search) select_tab(i);
            }
            return true;
        }
        return AbstractContainerScreen::key_pressed(event);
    }
    const bool numeric = event.key >= '0' && event.key <= '9';
    ContainerMenu* m = menu();
    const bool has_item = hovered_slot == nullptr || hovered_slot->index >= 45 ||
                          (m != nullptr && !empty(m->slots[static_cast<size_t>(hovered_slot->index)]));
    if (has_item && numeric && check_hotbar_key_pressed(event)) {
        ignore_text_input_ = true;
        return true;
    }
    const std::string before = search_->value();
    if (search_->key_pressed(event)) {
        if (before != search_->value()) refresh_search_results();
        return true;
    }
    if (search_->focused && search_->visible && event.key != key::Escape) return true;
    return AbstractContainerScreen::key_pressed(event);
}

// `CreativeModeInventoryScreen.keyReleased`: typed text counts again.
bool CreativeModeInventoryScreen::key_released(const KeyEvent& event) {
    ignore_text_input_ = false;
    return AbstractContainerScreen::key_released(event);
}

void CreativeModeInventoryScreen::sync_creative_slots(
    const std::vector<std::pair<int16_t, ItemStack>>& changed) {
    World* w = world();
    if (w == nullptr) return;
    for (const auto& [slot, stack] : changed) {
        if (w->creative_set) w->creative_set(slot, stack);
    }
    if (w->commit) w->commit();
}

// `CreativeModeInventoryScreen.slotClicked`: drops outside, the bin, the inventory tab, picker slots and the hotbar row.
void CreativeModeInventoryScreen::slot_clicked(const ScreenSlot* slot, int index, int button,
                                               ClickType type) {
    World* w = world();
    ContainerMenu* inventory = AbstractContainerScreen::menu();
    if (w == nullptr || inventory == nullptr) return;
    if (slot != nullptr) index = slot->index;
    const bool quick = type == ClickType::QuickMove;
    if (index == -999 && type == ClickType::Pickup) type = ClickType::Throw;
    ItemStack& carried = inventory->carried;
    const bool inventory_tab = tab().type == CreativeTab::Type::Inventory;
    const bool picker_slot = !inventory_tab && slot != nullptr && slot->index < 45;

    if (slot == nullptr && !inventory_tab && type != ClickType::QuickCraft) {
        if (!empty(carried) && has_clicked_outside_) {
            if (button == 0) {
                if (w->creative_set) w->creative_set(-1, carried);
                carried = ItemStack{};
            } else if (button == 1) {
                ItemStack one = carried;
                one.count = 1;
                if (w->creative_set) w->creative_set(-1, one);
                if (--carried.count <= 0) carried = ItemStack{};
            }
            if (w->commit) w->commit();
        }
        return;
    }
    if (inventory_tab) {
        if (slot != nullptr && slot->index == 46) {
            if (quick) {
                std::vector<std::pair<int16_t, ItemStack>> cleared;
                for (size_t i = 0; i < inventory->size(); ++i) {
                    inventory->slots[i] = ItemStack{};
                    cleared.emplace_back(static_cast<int16_t>(i), ItemStack{});
                }
                sync_creative_slots(cleared);
            } else {
                carried = ItemStack{};
                if (w->commit) w->commit();
            }
            return;
        }
        if (type == ClickType::Throw && slot != nullptr &&
            !empty(inventory->slots[static_cast<size_t>(slot->index)])) {
            ItemStack& here = inventory->slots[static_cast<size_t>(slot->index)];
            ItemStack thrown = here;
            thrown.count = std::min(here.count, button == 0 ? 1 : inventory->max_stack_of(here));
            here.count -= thrown.count;
            if (here.count <= 0) here = ItemStack{};
            if (w->creative_set) w->creative_set(-1, thrown);
            sync_creative_slots({{static_cast<int16_t>(slot->index), here}});
            return;
        }
        if (type == ClickType::Throw && index == -999 && !empty(carried)) {
            if (w->creative_set) w->creative_set(-1, carried);
            carried = ItemStack{};
            if (w->commit) w->commit();
            return;
        }
        const auto before = inventory->slots;
        inventory->clicked(index, button, type);
        sync_creative_slots(inventory->changed_since(before));
        return;
    }
    if (picker_slot && type != ClickType::QuickCraft) {
        const ItemStack here = picker_.slots[static_cast<size_t>(slot->index)];
        const int max = inventory->max_stack_of(here);
        if (type == ClickType::Swap) {
            if (!empty(here) && button >= 0 && button < 9) {
                ItemStack full = here;
                full.count = max;
                inventory->slots[static_cast<size_t>(36 + button)] = full;
                sync_creative_slots({{static_cast<int16_t>(36 + button), full}});
            }
            return;
        }
        if (type == ClickType::Clone) {
            if (empty(carried) && !empty(here)) {
                carried = here;
                carried.count = max;
                if (w->commit) w->commit();
            }
            return;
        }
        if (type == ClickType::Throw) {
            if (!empty(here)) {
                ItemStack thrown = here;
                thrown.count = button == 0 ? 1 : max;
                if (w->creative_set) w->creative_set(-1, thrown);
            }
            return;
        }
        if (!empty(carried) && !empty(here) && carried.id == here.id) {
            if (button == 0) {
                if (quick) {
                    carried.count = inventory->max_stack_of(carried);
                } else if (carried.count < inventory->max_stack_of(carried)) {
                    ++carried.count;
                }
            } else if (--carried.count <= 0) {
                carried = ItemStack{};
            }
        } else if (!empty(here) && empty(carried)) {
            carried = here;
            carried.count = quick ? max : here.count;
        } else if (button == 0) {
            carried = ItemStack{};
        } else if (!empty(carried) && --carried.count <= 0) {
            carried = ItemStack{};
        }
        if (w->commit) w->commit();
        return;
    }
    // The hotbar row under the picker: the picker menu's own click, then the
    // hotbar written back slot by slot.
    menu();
    const auto before = picker_.slots;
    picker_.carried = carried;
    picker_.clicked(index, button, type);
    carried = picker_.carried;
    std::vector<std::pair<int16_t, ItemStack>> changed;
    for (int i = 0; i < 9; ++i) {
        const ItemStack& now = picker_.slots[static_cast<size_t>(45 + i)];
        const ItemStack& was = before[static_cast<size_t>(45 + i)];
        if (now.id != was.id || now.count != was.count) {
            inventory->slots[static_cast<size_t>(36 + i)] = now;
            changed.emplace_back(static_cast<int16_t>(36 + i), now);
        }
    }
    sync_creative_slots(changed);
}

} // namespace mcpp::ui
