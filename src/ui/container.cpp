#include "mcpp/ui/container.hpp"

#include <algorithm>
#include <cmath>

namespace mcpp::ui {
namespace {

bool empty(const ItemStack& stack) { return stack.id <= 0 || stack.count <= 0; }

ItemStack with_count(ItemStack stack, int count) {
    stack.count = count;
    if (count <= 0) stack = ItemStack{};
    return stack;
}

ItemStack split(ItemStack& stack, int count) {
    const int taken = std::min(count, stack.count);
    ItemStack out = with_count(stack, taken);
    stack.count -= taken;
    if (stack.count <= 0) stack = ItemStack{};
    return out;
}

} // namespace

ContainerMenu::ContainerMenu(std::vector<SlotRule> rules, const ItemTable* items)
    : slots(rules.size()), rules_(std::move(rules)), items_(items) {}

ContainerMenu ContainerMenu::player_inventory(const ItemTable* items) {
    std::vector<SlotRule> rules(46);
    rules[0].result = true;
    rules[0].accepts = SlotRule::Accepts::Nothing;
    constexpr SlotRule::Accepts armour[4] = {SlotRule::Accepts::Head, SlotRule::Accepts::Chest,
                                             SlotRule::Accepts::Legs, SlotRule::Accepts::Feet};
    for (int i = 0; i < 4; ++i) {
        rules[5 + i].accepts = armour[i];
        rules[5 + i].max_stack = 1;
        rules[5 + i].inventory_index = 39 - i;
    }
    for (int i = 9; i < 36; ++i) rules[i].inventory_index = i;
    for (int i = 0; i < 9; ++i) rules[36 + i].inventory_index = i;
    rules[45].inventory_index = 40;
    for (int i = 1; i < 5; ++i) rules[static_cast<size_t>(i)].container = 1;
    for (int i = 5; i < 46; ++i) rules[static_cast<size_t>(i)].container = 2;
    return ContainerMenu(std::move(rules), items);
}

// `CraftingMenu.CraftingMenu`: the result, the 3x3 grid, then the player's main slots and hotbar.
ContainerMenu ContainerMenu::crafting_table(const ItemTable* items) {
    std::vector<SlotRule> rules(46);
    rules[0].result = true;
    rules[0].accepts = SlotRule::Accepts::Nothing;
    for (int i = 1; i < 10; ++i) rules[static_cast<size_t>(i)].container = 1;
    for (int i = 10; i < 37; ++i) {
        rules[static_cast<size_t>(i)].inventory_index = i - 1;
        rules[static_cast<size_t>(i)].container = 2;
    }
    for (int i = 0; i < 9; ++i) {
        rules[static_cast<size_t>(37 + i)].inventory_index = i;
        rules[static_cast<size_t>(37 + i)].container = 2;
    }
    ContainerMenu menu(std::move(rules), items);
    menu.kind = Kind::Crafting;
    menu.grid_width = 3;
    menu.grid_height = 3;
    return menu;
}

bool ContainerMenu::same_item(const ItemStack& a, const ItemStack& b) {
    if (empty(a) && empty(b)) return true;
    return !empty(a) && !empty(b) && a.id == b.id;
}

int ContainerMenu::max_stack_of(const ItemStack& stack) const {
    return items_ != nullptr ? items_->max_stack(stack.id) : 64;
}

int ContainerMenu::slot_max(size_t slot, const ItemStack& stack) const {
    return std::min(rules_[slot].max_stack, max_stack_of(stack));
}

bool ContainerMenu::may_place(size_t slot, const ItemStack& stack) const {
    using A = SlotRule::Accepts;
    const A accepts = rules_[slot].accepts;
    if (accepts == A::Anything) return true;
    if (accepts == A::Nothing || items_ == nullptr) return false;
    const auto worn = items_->slot(stack.id);
    switch (accepts) {
    case A::Head: return worn == ItemTable::Slot::Head;
    case A::Chest: return worn == ItemTable::Slot::Chest;
    case A::Legs: return worn == ItemTable::Slot::Legs;
    case A::Feet: return worn == ItemTable::Slot::Feet;
    default: return false;
    }
}

int ContainerMenu::slot_of_inventory(int index) const {
    for (size_t i = 0; i < rules_.size(); ++i) {
        if (rules_[i].inventory_index == index) return static_cast<int>(i);
    }
    return -1;
}

// `AbstractContainerMenu.getQuickCraftPlaceCount`: how many items a drag puts in each slot.
int ContainerMenu::quick_craft_place_count(size_t count, int type, const ItemStack& carried,
                                           int max_stack) {
    switch (type) {
    case 0: return static_cast<int>(std::floor(static_cast<float>(carried.count) /
                                               static_cast<float>(count)));
    case 1: return 1;
    case 2: return max_stack;
    default: return carried.count;
    }
}

// `AbstractContainerMenu.canItemQuickReplace`: whether a slot can take the stack in a drag.
bool ContainerMenu::can_quick_replace(int slot, const ItemStack& stack, bool stack_on_top) const {
    const bool free = slot < 0 || empty(slots[static_cast<size_t>(slot)]);
    if (!free && same_item(stack, slots[static_cast<size_t>(slot)])) {
        return slots[static_cast<size_t>(slot)].count + (stack_on_top ? 0 : stack.count) <=
               max_stack_of(stack);
    }
    return free;
}

// `AbstractContainerMenu.resetQuickCraft`: ends a drag.
void ContainerMenu::reset_quick_craft() {
    quick_craft_status_ = 0;
    quick_craft_slots_.clear();
}

// `AbstractContainerMenu.clicked`: runs a click on the menu.
void ContainerMenu::clicked(int slot, int button, ClickType type) { do_click(slot, button, type); }

// `Slot.safeInsert`: puts up to a count of the stack into a slot.
ItemStack ContainerMenu::safe_insert(size_t slot, ItemStack stack, int increment) {
    if (empty(stack) || !may_place(slot, stack)) return stack;
    ItemStack& here = slots[slot];
    const int moved = std::min(std::min(increment, stack.count), slot_max(slot, stack) - here.count);
    if (moved <= 0) return stack;
    if (empty(here)) {
        here = split(stack, moved);
    } else if (same_item(here, stack)) {
        stack.count -= moved;
        here.count += moved;
        if (stack.count <= 0) stack = ItemStack{};
    }
    return stack;
}

// `Slot.tryRemove`, `ResultSlot.remove`: takes up to a count out of a slot; a result goes whole or not at all.
bool ContainerMenu::try_remove(size_t slot, int count, int decrement, ItemStack& out) {
    ItemStack& here = slots[slot];
    if (empty(here)) return false;
    const bool result = rules_[slot].result;
    if (result && decrement < here.count) return false;
    count = std::min(count, decrement);
    out = result ? std::exchange(here, ItemStack{}) : split(here, count);
    return !empty(out);
}

// `Slot.onTake`: taking from a crafting result uses up the grid.
void ContainerMenu::on_take(size_t slot) {
    if (rules_[slot].result) take_crafted();
}

// `ResultSlot.onTake`, `CraftingInput.ofPositioned`, `CraftingRecipe.defaultCraftingReminder`: one of each ingredient goes, and what it leaves behind stays.
void ContainerMenu::take_crafted() {
    const auto at = [&](int x, int y) -> ItemStack& {
        return slots[static_cast<size_t>(grid_first + x + y * grid_width)];
    };
    int left = grid_width - 1;
    int right = 0;
    int top = grid_height - 1;
    int bottom = 0;
    for (int y = 0; y < grid_height; ++y) {
        bool row_empty = true;
        for (int x = 0; x < grid_width; ++x) {
            if (!empty(at(x, y))) {
                left = std::min(left, x);
                right = std::max(right, x);
                row_empty = false;
            }
        }
        if (!row_empty) {
            top = std::min(top, y);
            bottom = std::max(bottom, y);
        }
    }
    const int width = right - left + 1;
    const int height = bottom - top + 1;
    if (width <= 0 || height <= 0) return;
    for (int y = top; y < top + height; ++y) {
        for (int x = left; x < left + width; ++x) {
            ItemStack& here = at(x, y);
            ItemStack remainder;
            if (!empty(here) && items_ != nullptr) {
                const std::string_view name = items_->name(here.id);
                std::string_view leaves;
                if (name == "water_bucket" || name == "lava_bucket" || name == "milk_bucket") {
                    leaves = "bucket";
                } else if (name == "dragon_breath" || name == "honey_bottle") {
                    leaves = "glass_bottle";
                }
                if (!leaves.empty()) {
                    remainder.id = items_->find(leaves);
                    remainder.count = 1;
                }
            }
            if (!empty(here)) split(here, 1);
            if (empty(remainder)) continue;
            if (empty(here)) {
                here = remainder;
            } else if (same_item(here, remainder)) {
                here.count += remainder.count;
            }
            // Anywhere else it goes into the inventory or on the ground, which
            // the server says.
        }
    }
}

// `Slot.safeTake`: takes up to a count out of a slot, and what taking it does.
ItemStack ContainerMenu::safe_take(size_t slot, int count, int decrement) {
    ItemStack taken;
    if (!try_remove(slot, count, decrement, taken)) return ItemStack{};
    on_take(slot);
    return taken;
}

// `AbstractContainerMenu.doClick`: drags, pickups, swaps, clones, throws and double-click collects.
void ContainerMenu::do_click(int slot, int button, ClickType type) {
    const auto at = [&](int index) -> ItemStack& { return slots[static_cast<size_t>(index)]; };
    if (type == ClickType::QuickCraft) {
        const int previous = quick_craft_status_;
        quick_craft_status_ = quick_craft_header(button);
        if ((previous != 1 || quick_craft_status_ != 2) && previous != quick_craft_status_) {
            reset_quick_craft();
        } else if (empty(carried)) {
            reset_quick_craft();
        } else if (quick_craft_status_ == 0) {
            quick_craft_type_ = quick_craft_type(button);
            if (quick_craft_type_ == 0 || quick_craft_type_ == 1 ||
                (quick_craft_type_ == 2 && creative)) {
                quick_craft_status_ = 1;
                quick_craft_slots_.clear();
            } else {
                reset_quick_craft();
            }
        } else if (quick_craft_status_ == 1) {
            if (slot >= 0 && static_cast<size_t>(slot) < slots.size() &&
                can_quick_replace(slot, carried, true) && may_place(static_cast<size_t>(slot), carried) &&
                (quick_craft_type_ == 2 ||
                 carried.count > static_cast<int>(quick_craft_slots_.size())) &&
                std::find(quick_craft_slots_.begin(), quick_craft_slots_.end(), slot) ==
                    quick_craft_slots_.end()) {
                quick_craft_slots_.push_back(slot);
            }
        } else if (quick_craft_status_ == 2) {
            if (!quick_craft_slots_.empty()) {
                if (quick_craft_slots_.size() == 1) {
                    const int only = quick_craft_slots_.front();
                    const int kind = quick_craft_type_;
                    reset_quick_craft();
                    do_click(only, kind, ClickType::Pickup);
                    return;
                }
                ItemStack source = carried;
                if (empty(source)) {
                    reset_quick_craft();
                    return;
                }
                int left = carried.count;
                for (const int target : quick_craft_slots_) {
                    if (can_quick_replace(target, carried, true) &&
                        may_place(static_cast<size_t>(target), carried) &&
                        (quick_craft_type_ == 2 ||
                         carried.count >= static_cast<int>(quick_craft_slots_.size()))) {
                        const int had = empty(at(target)) ? 0 : at(target).count;
                        const int cap = std::min(max_stack_of(source),
                                                 slot_max(static_cast<size_t>(target), source));
                        const int now = std::min(
                            quick_craft_place_count(quick_craft_slots_.size(), quick_craft_type_,
                                                    source, max_stack_of(source)) +
                                had,
                            cap);
                        left -= now - had;
                        at(target) = with_count(source, now);
                    }
                }
                carried = with_count(source, left);
            }
            reset_quick_craft();
        } else {
            reset_quick_craft();
        }
        return;
    }
    if (quick_craft_status_ != 0) {
        reset_quick_craft();
        return;
    }

    if ((type == ClickType::Pickup || type == ClickType::QuickMove) && (button == 0 || button == 1)) {
        const bool primary = button == 0;
        if (slot == -999) {
            if (!empty(carried)) {
                if (primary) {
                    carried = ItemStack{};
                } else {
                    split(carried, 1);
                }
            }
            return;
        }
        if (slot < 0 || static_cast<size_t>(slot) >= slots.size()) return;
        const auto index = static_cast<size_t>(slot);
        if (type == ClickType::QuickMove) {
            const auto quick = [&] {
                return kind == Kind::Crafting ? crafting_quick_move_stack(slot)
                                              : quick_move_stack(slot);
            };
            ItemStack moved = quick();
            while (!empty(moved) && same_item(at(slot), moved)) moved = quick();
            return;
        }
        ItemStack& here = at(slot);
        if (empty(here)) {
            if (!empty(carried)) {
                carried = safe_insert(index, carried, primary ? carried.count : 1);
            }
        } else if (empty(carried)) {
            const int count = primary ? here.count : (here.count + 1) / 2;
            ItemStack taken;
            if (try_remove(index, count, 0x7FFFFFFF, taken)) {
                carried = taken;
                on_take(index);
            }
        } else if (may_place(index, carried)) {
            if (same_item(here, carried)) {
                carried = safe_insert(index, carried, primary ? carried.count : 1);
            } else if (carried.count <= slot_max(index, carried)) {
                std::swap(here, carried);
            }
        } else if (same_item(here, carried)) {
            ItemStack taken;
            if (try_remove(index, here.count, max_stack_of(carried) - carried.count, taken)) {
                carried.count += taken.count;
                on_take(index);
            }
        }
        return;
    }

    if (type == ClickType::Swap && ((button >= 0 && button < 9) || button == 40)) {
        if (slot < 0 || static_cast<size_t>(slot) >= slots.size()) return;
        const int source_slot = slot_of_inventory(button);
        if (source_slot < 0) return;
        ItemStack source = at(source_slot);
        ItemStack target = at(slot);
        if (empty(source) && empty(target)) return;
        if (empty(source)) {
            at(source_slot) = target;
            at(slot) = ItemStack{};
            on_take(static_cast<size_t>(slot));
        } else if (empty(target)) {
            if (may_place(static_cast<size_t>(slot), source)) {
                const int cap = slot_max(static_cast<size_t>(slot), source);
                if (source.count > cap) {
                    at(slot) = split(at(source_slot), cap);
                } else {
                    at(source_slot) = ItemStack{};
                    at(slot) = source;
                }
            }
        } else if (may_place(static_cast<size_t>(slot), source)) {
            const int cap = slot_max(static_cast<size_t>(slot), source);
            if (source.count > cap) {
                // The rest goes back into the inventory, or on the ground: the
                // server knows where, so the slot is left for it to say.
                at(slot) = split(at(source_slot), cap);
            } else {
                at(source_slot) = target;
                at(slot) = source;
            }
        }
        return;
    }

    if (type == ClickType::Clone && creative && empty(carried) && slot >= 0 &&
        static_cast<size_t>(slot) < slots.size()) {
        if (!empty(at(slot))) carried = with_count(at(slot), max_stack_of(at(slot)));
        return;
    }

    if (type == ClickType::Throw && empty(carried) && slot >= 0 &&
        static_cast<size_t>(slot) < slots.size()) {
        const int count = button == 0 ? 1 : at(slot).count;
        ItemStack thrown = safe_take(static_cast<size_t>(slot), count, 0x7FFFFFFF);
        if (button == 1) {
            while (!empty(thrown) && same_item(at(slot), thrown)) {
                thrown = safe_take(static_cast<size_t>(slot), count, 0x7FFFFFFF);
            }
        }
        return;
    }

    if (type == ClickType::PickupAll && slot >= 0 && static_cast<size_t>(slot) < slots.size()) {
        if (empty(carried) || !empty(at(slot))) return;
        const int size = static_cast<int>(slots.size());
        const int first = button == 0 ? 0 : size - 1;
        const int step = button == 0 ? 1 : -1;
        for (int pass = 0; pass < 2; ++pass) {
            for (int i = first; i >= 0 && i < size && carried.count < max_stack_of(carried);
                 i += step) {
                if (empty(at(i)) || !can_take_for_pick_all(static_cast<size_t>(i))) continue;
                if (!can_quick_replace(i, carried, true)) continue;
                if (pass == 0 && at(i).count == max_stack_of(at(i))) continue;
                const ItemStack taken = safe_take(static_cast<size_t>(i), at(i).count,
                                                  max_stack_of(carried) - carried.count);
                carried.count += taken.count;
            }
        }
    }
}

// `AbstractContainerMenu.moveItemStackTo`: merges into matching stacks, then the first free slot.
bool ContainerMenu::move_item_stack_to(ItemStack& stack, int from, int to, bool backwards) {
    bool moved = false;
    int i = backwards ? to - 1 : from;
    if (max_stack_of(stack) > 1) {
        while (!empty(stack) && (backwards ? i >= from : i < to)) {
            ItemStack& here = slots[static_cast<size_t>(i)];
            if (!empty(here) && same_item(stack, here)) {
                const int total = here.count + stack.count;
                const int cap = slot_max(static_cast<size_t>(i), here);
                if (total <= cap) {
                    stack = ItemStack{};
                    here.count = total;
                    moved = true;
                } else if (here.count < cap) {
                    stack.count -= cap - here.count;
                    here.count = cap;
                    moved = true;
                }
            }
            i += backwards ? -1 : 1;
        }
    }
    if (!empty(stack)) {
        i = backwards ? to - 1 : from;
        while (backwards ? i >= from : i < to) {
            ItemStack& here = slots[static_cast<size_t>(i)];
            if (empty(here) && may_place(static_cast<size_t>(i), stack)) {
                here = split(stack, std::min(stack.count, slot_max(static_cast<size_t>(i), stack)));
                moved = true;
                break;
            }
            i += backwards ? -1 : 1;
        }
    }
    return moved;
}

// `InventoryMenu.quickMoveStack`: shift-click routing between inventory sections.
ItemStack ContainerMenu::quick_move_stack(int slot) {
    ItemStack& here = slots[static_cast<size_t>(slot)];
    if (empty(here)) return ItemStack{};
    const ItemStack original = here;
    ItemStack moving = here;
    const auto worn = items_ != nullptr ? items_->slot(moving.id) : ItemTable::Slot::None;
    int armour_slot = -1;
    if (worn == ItemTable::Slot::Head) armour_slot = 5;
    if (worn == ItemTable::Slot::Chest) armour_slot = 6;
    if (worn == ItemTable::Slot::Legs) armour_slot = 7;
    if (worn == ItemTable::Slot::Feet) armour_slot = 8;

    bool ok = true;
    if (slot == 0) {
        if (!move_item_stack_to(moving, 9, 45, true)) return ItemStack{};
        here = moving;
        if (moving.count == original.count) return ItemStack{};
        on_take(0);
        return original;
    }
    if (slot >= 1 && slot < 9) {
        ok = move_item_stack_to(moving, 9, 45, false);
    } else if (armour_slot >= 0 && empty(slots[static_cast<size_t>(armour_slot)])) {
        ok = move_item_stack_to(moving, armour_slot, armour_slot + 1, false);
    } else if (worn == ItemTable::Slot::Offhand && empty(slots[45])) {
        ok = move_item_stack_to(moving, 45, 46, false);
    } else if (slot >= 9 && slot < 36) {
        ok = move_item_stack_to(moving, 36, 45, false);
    } else if (slot >= 36 && slot < 45) {
        ok = move_item_stack_to(moving, 9, 36, false);
    } else {
        ok = move_item_stack_to(moving, 9, 45, false);
    }
    if (!ok) return ItemStack{};
    here = moving;
    if (moving.count == original.count) return ItemStack{};
    return original;
}

// `CraftingMenu.quickMoveStack`: shift-click routing in a crafting table.
ItemStack ContainerMenu::crafting_quick_move_stack(int slot) {
    ItemStack& here = slots[static_cast<size_t>(slot)];
    if (empty(here)) return ItemStack{};
    const ItemStack original = here;
    ItemStack moving = here;
    bool ok = true;
    if (slot == 0) {
        ok = move_item_stack_to(moving, 10, 46, true);
    } else if (slot >= 10 && slot < 46) {
        if (!move_item_stack_to(moving, 1, 10, false)) {
            ok = slot < 37 ? move_item_stack_to(moving, 37, 46, false)
                           : move_item_stack_to(moving, 10, 37, false);
        }
    } else {
        ok = move_item_stack_to(moving, 10, 46, false);
    }
    if (!ok) return ItemStack{};
    here = moving;
    if (moving.count == original.count) return ItemStack{};
    on_take(static_cast<size_t>(slot));
    return original;
}

std::vector<std::pair<int16_t, ItemStack>> ContainerMenu::changed_since(
    const std::vector<ItemStack>& before) const {
    std::vector<std::pair<int16_t, ItemStack>> out;
    for (size_t i = 0; i < slots.size() && i < before.size(); ++i) {
        const ItemStack& a = before[i];
        const ItemStack& b = slots[i];
        const bool same = (empty(a) && empty(b)) || (a.id == b.id && a.count == b.count);
        if (!same) out.emplace_back(static_cast<int16_t>(i), b);
    }
    return out;
}

} // namespace mcpp::ui
