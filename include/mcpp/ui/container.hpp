#pragma once
#include "mcpp/session.hpp"
#include "mcpp/ui/items.hpp"

#include <cstdint>
#include <set>
#include <utility>
#include <vector>

// The client's half of a container: the slots as the screen shows them and
// what a click does to them, worked out the game's way before the server
// answers, so the stack moves under the pointer at once. The server checks
// every click and puts back whatever it disagrees with.
namespace mcpp::ui {

enum class ClickType : int32_t { Pickup, QuickMove, Swap, Clone, Throw, QuickCraft, PickupAll };

struct SlotRule {
    enum class Accepts : uint8_t { Anything, Nothing, Head, Chest, Legs, Feet };
    Accepts accepts = Accepts::Anything;
    int max_stack = 99;
    // Index in the player's own 41-slot inventory, or -1 for a container slot:
    // what a number key swaps with.
    int inventory_index = -1;
    // A crafting result: taking from it is the server's business entirely.
    bool result = false;
};

class ContainerMenu {
public:
    ContainerMenu(std::vector<SlotRule> rules, const ItemTable* items);
    // The player's own inventory menu: result, grid, armour, main, hotbar, offhand.
    static ContainerMenu player_inventory(const ItemTable* items);

    int32_t container_id = 0;
    int32_t state_id = 0;
    std::vector<ItemStack> slots;
    ItemStack carried;
    bool creative = false;

    size_t size() const { return slots.size(); }
    const SlotRule& rule(size_t slot) const { return rules_[slot]; }
    bool may_place(size_t slot, const ItemStack& stack) const;
    int max_stack_of(const ItemStack& stack) const;
    int slot_max(size_t slot, const ItemStack& stack) const;
    // The menu slot holding inventory index `index` (0-8 hotbar, 40 offhand).
    int slot_of_inventory(int index) const;

    void clicked(int slot, int button, ClickType type);
    // The slots that differ from `before`, the way the click packet lists them.
    std::vector<std::pair<int16_t, ItemStack>> changed_since(const std::vector<ItemStack>& before) const;

    static bool same_item(const ItemStack& a, const ItemStack& b);
    // `AbstractContainerMenu.getQuickcraftType`: the drag type in a quick craft button.
    static int quick_craft_type(int mask) { return (mask >> 2) & 3; }
    // `AbstractContainerMenu.getQuickcraftHeader`: the drag stage in a quick craft button.
    static int quick_craft_header(int mask) { return mask & 3; }
    // `AbstractContainerMenu.getQuickcraftMask`: packs a drag stage and type into a button.
    static int quick_craft_mask(int header, int type) { return (header & 3) | ((type & 3) << 2); }
    static int quick_craft_place_count(size_t slots, int type, const ItemStack& carried,
                                       int max_stack);
    bool can_quick_replace(int slot, const ItemStack& stack, bool stack_on_top) const;

private:
    void do_click(int slot, int button, ClickType type);
    ItemStack quick_move_stack(int slot);
    bool move_item_stack_to(ItemStack& stack, int from, int to, bool backwards);
    ItemStack safe_insert(size_t slot, ItemStack stack, int increment);
    ItemStack safe_take(size_t slot, int count, int decrement);
    void reset_quick_craft();

    std::vector<SlotRule> rules_;
    const ItemTable* items_;
    int quick_craft_status_ = 0;
    int quick_craft_type_ = -1;
    std::vector<int> quick_craft_slots_;
};

} // namespace mcpp::ui
