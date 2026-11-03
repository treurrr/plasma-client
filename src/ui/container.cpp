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
    return ContainerMenu(std::move(rules), items);
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

// `Slot.safeTake`: takes up to a count out of a slot.
ItemStack ContainerMenu::safe_take(size_t slot, int count, int decrement) {
    return split(slots[slot], std::min(count, decrement));
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
        if (rules_[static_cast<size_t>(slot)].result) return;
        if (type == ClickType::QuickMove) {
            ItemStack moved = quick_move_stack(slot);
            while (!empty(moved) && same_item(at(slot), moved)) moved = quick_move_stack(slot);
            return;
        }
        ItemStack& here = at(slot);
        if (empty(here)) {
            if (!empty(carried)) {
                carried = safe_insert(static_cast<size_t>(slot), carried,
                                      primary ? carried.count : 1);
            }
        } else if (empty(carried)) {
            const int count = primary ? here.count : (here.count + 1) / 2;
            carried = split(here, count);
        } else if (may_place(static_cast<size_t>(slot), carried)) {
            if (same_item(here, carried)) {
                carried = safe_insert(static_cast<size_t>(slot), carried,
                                      primary ? carried.count : 1);
            } else if (carried.count <= slot_max(static_cast<size_t>(slot), carried)) {
                std::swap(here, carried);
            }
        } else if (same_item(here, carried)) {
            const ItemStack taken =
                safe_take(static_cast<size_t>(slot), here.count, max_stack_of(carried) - carried.count);
            carried.count += taken.count;
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
        if (rules_[static_cast<size_t>(slot)].result) return;
        const int count = button == 0 ? 1 : at(slot).count;
        safe_take(static_cast<size_t>(slot), count, 0x7FFFFFFF);
        return;
    }

    if (type == ClickType::PickupAll && slot >= 0 && static_cast<size_t>(slot) < slots.size()) {
        if (empty(carried) || (!empty(at(slot)) && !rules_[static_cast<size_t>(slot)].result)) return;
        const int size = static_cast<int>(slots.size());
        const int first = button == 0 ? 0 : size - 1;
        const int step = button == 0 ? 1 : -1;
        for (int pass = 0; pass < 2; ++pass) {
            for (int i = first; i >= 0 && i < size && carried.count < max_stack_of(carried);
                 i += step) {
                if (empty(at(i)) || rules_[static_cast<size_t>(i)].result) continue;
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
