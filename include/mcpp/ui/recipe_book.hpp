#pragma once
#include "mcpp/session.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

// The client's recipe book: every recipe the server has unlocked for the
// player, grouped into the collections a book button shows, and which of them
// the inventory can make right now.
namespace mcpp::ui {

// One recipe as the book knows it, its slot displays already resolved to the
// stacks they cycle through.
struct RecipeDisplayEntry {
    enum class Kind : uint8_t { Shapeless, Shaped, Other };
    int32_t id = -1;
    Kind kind = Kind::Other;
    int width = 0;
    int height = 0;
    std::vector<std::vector<ItemStack>> ingredients;
    std::vector<ItemStack> result;
    std::optional<int32_t> group;
    // The crafting categories in tab order (equipment, building blocks, misc,
    // redstone), -1 for every other book.
    int category = -1;
    // The items each ingredient accepts; none means the book never calls it craftable.
    std::optional<std::vector<std::vector<int32_t>>> requirements;
};

// What the inventory holds, item by item, for asking whether a recipe can be made.
class StackedItemContents {
public:
    void clear() { counts_.clear(); }
    void account_simple_stack(const ItemStack& stack);
    void account_stack(const ItemStack& stack, int max_stack);
    bool can_craft(const std::vector<std::vector<int32_t>>& ingredients) const;

private:
    std::map<int32_t, int> counts_;
};

class RecipeCollection {
public:
    enum class Status : uint8_t { Any, Craftable, NotCraftable };
    explicit RecipeCollection(std::vector<const RecipeDisplayEntry*> entries)
        : entries_(std::move(entries)) {}
    void select_recipes(const StackedItemContents& contents,
                        const std::function<bool(const RecipeDisplayEntry&)>& fits);
    bool is_craftable(int32_t id) const { return craftable_.contains(id); }
    bool has_craftable() const { return !craftable_.empty(); }
    bool has_any_selected() const { return !selected_.empty(); }
    const std::vector<const RecipeDisplayEntry*>& recipes() const { return entries_; }
    std::vector<const RecipeDisplayEntry*> selected_recipes(Status status) const;

private:
    std::vector<const RecipeDisplayEntry*> entries_;
    std::set<int32_t> craftable_;
    std::set<int32_t> selected_;
};

class ClientRecipeBook {
public:
    // The crafting tabs: search, then the four categories.
    static constexpr int kTabs = 5;

    void add(RecipeDisplayEntry entry);
    // One recipe_book_add packet: a replacing batch empties the book first.
    void add_batch(const proto::Value& added, const Registries& registries, const Tags& tags);
    void remove(int32_t id);
    void clear();
    bool will_highlight(int32_t id) const { return highlight_.contains(id); }
    void remove_highlight(int32_t id) { highlight_.erase(id); }
    void add_highlight(int32_t id) { highlight_.insert(id); }
    void rebuild_collections();
    const std::vector<RecipeCollection*>& collection(int tab) const {
        return tabs_[static_cast<size_t>(tab)];
    }
    // The ids in the order the game walks its own table of them.
    std::vector<int32_t> known_order() const;
    size_t size() const { return known_.size(); }

    bool open = false;
    bool filtering = false;

private:
    void resize();
    std::map<int32_t, RecipeDisplayEntry> known_;
    // The game keeps the book in a hash table and groups the recipes in the
    // order it walks it, which is the order a page shows them: its buckets,
    // each in the order its recipes came, are kept here.
    std::vector<std::vector<int32_t>> buckets_;
    std::set<int32_t> highlight_;
    std::vector<std::unique_ptr<RecipeCollection>> all_;
    std::array<std::vector<RecipeCollection*>, kTabs> tabs_;
};

// A recipe display off the wire (recipe_book_add's entries, place_ghost_recipe)
// with its slot displays resolved through the server's tags.
RecipeDisplayEntry read_recipe_display(const proto::Value& display, const Tags& tags);
// One recipe_book_add entry: its id, display, group, category and requirements.
RecipeDisplayEntry read_recipe_entry(const proto::Value& entry, const Registries& registries,
                                     const Tags& tags);

// `PlaceRecipeHelper.placeRecipe`: where each of a recipe's ingredients goes in a grid, as
// (ingredient, grid slot) pairs.
std::vector<std::pair<size_t, int>> place_recipe(int grid_width, int grid_height,
                                                 int recipe_width, int recipe_height,
                                                 size_t ingredients);

} // namespace mcpp::ui
