#include "mcpp/ui/recipe_book.hpp"

#include "mcpp/protocol_versions.hpp"

#include <algorithm>
#include <cmath>
#include <functional>

namespace mcpp::ui {
namespace {

constexpr size_t kFirstCapacity = 16;
constexpr size_t kTreeifyThreshold = 8;
constexpr size_t kMinTreeifyCapacity = 64;

size_t bucket_of(int32_t id, size_t capacity) {
    const auto h = static_cast<uint32_t>(id);
    return static_cast<size_t>(h ^ (h >> 16)) & (capacity - 1);
}

std::string_view without_namespace(std::string_view name) {
    const auto colon = name.find(':');
    return colon == std::string_view::npos ? name : name.substr(colon + 1);
}

int64_t int_of(const proto::Value* value, int64_t fallback = 0) {
    return value != nullptr ? value->as_int().value_or(fallback) : fallback;
}

// `SlotDisplay.resolveForStacks`: the stacks a slot display cycles through.
void resolve(const proto::Value& display, const Tags& tags, std::vector<ItemStack>& out,
             int depth = 0) {
    if (depth > 8) return;
    const auto* type = display.field("type");
    if (type == nullptr) return;
    const std::string_view kind = without_namespace(type->label());
    if (kind == "item") {
        out.push_back(ItemStack{static_cast<int32_t>(int_of(display.field("item"))), 1});
    } else if (kind == "item_stack") {
        const auto* stack = display.field("stack");
        if (stack != nullptr && stack->as_stack() != nullptr && stack->as_stack()->count > 0) {
            out.push_back(*stack->as_stack());
        }
    } else if (kind == "tag") {
        const auto* tag = display.field("tag");
        if (tag == nullptr || tag->as_string() == nullptr) return;
        if (const auto* ids = tags.find("minecraft:item", *tag->as_string())) {
            for (const int32_t id : *ids) out.push_back(ItemStack{id, 1});
        }
    } else if (kind == "with_remainder") {
        if (const auto* input = display.field("input")) resolve(*input, tags, out, depth + 1);
    } else if (kind == "composite") {
        const auto* options = display.field("options");
        if (options == nullptr || options->as_list() == nullptr) return;
        for (const auto& option : *options->as_list()) resolve(option, tags, out, depth + 1);
    }
}

std::vector<ItemStack> resolved(const proto::Value* display, const Tags& tags) {
    std::vector<ItemStack> out;
    if (display != nullptr) resolve(*display, tags, out);
    return out;
}

std::vector<int32_t> id_set(const proto::Value& set, const Tags& tags) {
    std::vector<int32_t> out;
    if (const auto* name = set.as_string()) {
        const std::string_view tag = std::string_view(*name).substr(name->starts_with("#") ? 1 : 0);
        if (const auto* ids = tags.find("minecraft:item", tag)) out = *ids;
        return out;
    }
    if (const auto* list = set.as_list()) {
        for (const auto& id : *list) out.push_back(static_cast<int32_t>(id.as_int().value_or(0)));
    }
    return out;
}

} // namespace

// `Inventory.isUsableForCrafting`, `StackedItemContents.accountSimpleStack`: undamaged, unenchanted and unnamed stacks only.
void StackedItemContents::account_simple_stack(const ItemStack& stack) {
    constexpr int32_t protocol = protocol::kTargetProtocol;
    const auto& components = stack.components;
    if (const auto* damage = components.find("minecraft:damage", protocol);
        damage != nullptr && damage->as_int().value_or(0) > 0) {
        return;
    }
    if (const auto* enchantments = components.find("minecraft:enchantments", protocol)) {
        const auto* levels = enchantments->field("levels");
        const auto* list = levels != nullptr ? levels->as_list() : enchantments->as_list();
        if (list == nullptr || !list->empty()) return;
    }
    if (components.find("minecraft:custom_name", protocol) != nullptr) return;
    account_stack(stack, 99);
}

// `StackedItemContents.accountStack`: counts a stack, at most a full stack of it.
void StackedItemContents::account_stack(const ItemStack& stack, int max_stack) {
    if (stack.id <= 0 || stack.count <= 0) return;
    counts_[stack.id] += std::min(max_stack, stack.count);
}

// Whether every ingredient can be given an item of its own. The game finds
// the same answer with its own matching; ours is a plain augmenting path.
bool StackedItemContents::can_craft(const std::vector<std::vector<int32_t>>& ingredients) const {
    std::map<int32_t, std::vector<size_t>> taken;
    std::function<bool(size_t, std::set<int32_t>&)> give = [&](size_t i, std::set<int32_t>& seen) {
        for (const int32_t item : ingredients[i]) {
            const auto have = counts_.find(item);
            if (have == counts_.end() || !seen.insert(item).second) continue;
            auto& users = taken[item];
            if (static_cast<int>(users.size()) < have->second) {
                users.push_back(i);
                return true;
            }
            for (auto& user : users) {
                if (give(user, seen)) {
                    user = i;
                    return true;
                }
            }
        }
        return false;
    };
    for (size_t i = 0; i < ingredients.size(); ++i) {
        std::set<int32_t> seen;
        if (!give(i, seen)) return false;
    }
    return true;
}

// `RecipeCollection.selectRecipes`: which recipes fit the grid, and which of those can be made.
void RecipeCollection::select_recipes(const StackedItemContents& contents,
                                      const std::function<bool(const RecipeDisplayEntry&)>& fits) {
    for (const auto* entry : entries_) {
        const bool fit = fits(*entry);
        if (fit) {
            selected_.insert(entry->id);
        } else {
            selected_.erase(entry->id);
        }
        if (fit && entry->requirements && contents.can_craft(*entry->requirements)) {
            craftable_.insert(entry->id);
        } else {
            craftable_.erase(entry->id);
        }
    }
}

// `RecipeCollection.getSelectedRecipes`: the recipes of one craftable status, in order.
std::vector<const RecipeDisplayEntry*> RecipeCollection::selected_recipes(Status status) const {
    std::vector<const RecipeDisplayEntry*> out;
    for (const auto* entry : entries_) {
        const int32_t id = entry->id;
        const bool take = status == Status::Any         ? selected_.contains(id)
                          : status == Status::Craftable ? craftable_.contains(id)
                                                        : selected_.contains(id) &&
                                                              !craftable_.contains(id);
        if (take) out.push_back(entry);
    }
    return out;
}

void ClientRecipeBook::resize() {
    const size_t capacity = buckets_.empty() ? kFirstCapacity : buckets_.size() * 2;
    std::vector<std::vector<int32_t>> next(capacity);
    for (const auto& bucket : buckets_) {
        for (const int32_t id : bucket) next[bucket_of(id, capacity)].push_back(id);
    }
    buckets_ = std::move(next);
}

// `ClientRecipeBook.add`: a recipe joins the book, or replaces the one with its id.
void ClientRecipeBook::add(RecipeDisplayEntry entry) {
    const int32_t id = entry.id;
    if (buckets_.empty()) resize();
    const auto [it, inserted] = known_.insert_or_assign(id, std::move(entry));
    (void)it;
    if (!inserted) return;
    auto& bucket = buckets_[bucket_of(id, buckets_.size())];
    bucket.push_back(id);
    if (bucket.size() > kTreeifyThreshold && buckets_.size() < kMinTreeifyCapacity) resize();
    if (static_cast<double>(known_.size()) > static_cast<double>(buckets_.size()) * 0.75) resize();
}

// `ClientPacketListener.handleRecipeBookAdd`: a batch of recipes, some to highlight.
void ClientRecipeBook::add_batch(const proto::Value& added, const Registries& registries,
                                 const Tags& tags) {
    const auto* replace = added.field("replace");
    if (replace != nullptr && replace->as_bool().value_or(false)) clear();
    const auto* entries = added.field("entries");
    if (entries == nullptr || entries->as_list() == nullptr) return;
    for (const auto& entry : *entries->as_list()) {
        RecipeDisplayEntry read = read_recipe_entry(entry, registries, tags);
        const int32_t id = read.id;
        add(std::move(read));
        if ((int_of(entry.field("flags")) & 2) != 0) add_highlight(id);
    }
}

// `ClientRecipeBook.remove`: a recipe leaves the book.
void ClientRecipeBook::remove(int32_t id) {
    if (known_.erase(id) == 0) return;
    auto& bucket = buckets_[bucket_of(id, buckets_.size())];
    std::erase(bucket, id);
    highlight_.erase(id);
}

// `ClientRecipeBook.clear`: empties the book; the table keeps its size.
void ClientRecipeBook::clear() {
    known_.clear();
    for (auto& bucket : buckets_) bucket.clear();
    highlight_.clear();
}

std::vector<int32_t> ClientRecipeBook::known_order() const {
    std::vector<int32_t> out;
    out.reserve(known_.size());
    for (const auto& bucket : buckets_) out.insert(out.end(), bucket.begin(), bucket.end());
    return out;
}

// `ClientRecipeBook.rebuildCollections`, `ClientRecipeBook.categorizeAndGroupRecipes`: one collection per group, or per ungrouped recipe, in each category.
void ClientRecipeBook::rebuild_collections() {
    std::array<std::vector<std::vector<const RecipeDisplayEntry*>>, 4> categories;
    std::array<std::map<int32_t, size_t>, 4> groups;
    for (const int32_t id : known_order()) {
        const RecipeDisplayEntry& entry = known_.at(id);
        if (entry.category < 0 || entry.category >= 4) continue;
        auto& lists = categories[static_cast<size_t>(entry.category)];
        if (!entry.group) {
            lists.push_back({&entry});
            continue;
        }
        auto& index = groups[static_cast<size_t>(entry.category)];
        const auto found = index.find(*entry.group);
        if (found == index.end()) {
            index.emplace(*entry.group, lists.size());
            lists.push_back({&entry});
        } else {
            lists[found->second].push_back(&entry);
        }
    }
    all_.clear();
    for (auto& tab : tabs_) tab.clear();
    for (size_t c = 0; c < categories.size(); ++c) {
        for (auto& list : categories[c]) {
            all_.push_back(std::make_unique<RecipeCollection>(std::move(list)));
            tabs_[c + 1].push_back(all_.back().get());
        }
    }
    for (size_t c = 1; c < tabs_.size(); ++c) {
        tabs_[0].insert(tabs_[0].end(), tabs_[c].begin(), tabs_[c].end());
    }
}

RecipeDisplayEntry read_recipe_display(const proto::Value& display, const Tags& tags) {
    RecipeDisplayEntry out;
    const auto* type = display.field("type");
    const std::string_view kind = type != nullptr ? without_namespace(type->label()) : "";
    if (kind == "crafting_shaped") {
        out.kind = RecipeDisplayEntry::Kind::Shaped;
        out.width = static_cast<int>(int_of(display.field("width")));
        out.height = static_cast<int>(int_of(display.field("height")));
    } else if (kind == "crafting_shapeless") {
        out.kind = RecipeDisplayEntry::Kind::Shapeless;
    }
    if (const auto* ingredients = display.field("ingredients");
        ingredients != nullptr && ingredients->as_list() != nullptr) {
        for (const auto& slot : *ingredients->as_list()) out.ingredients.push_back(resolved(&slot, tags));
    }
    out.result = resolved(display.field("result"), tags);
    return out;
}

RecipeDisplayEntry read_recipe_entry(const proto::Value& entry, const Registries& registries,
                                     const Tags& tags) {
    RecipeDisplayEntry out;
    if (const auto* display = entry.field("display")) out = read_recipe_display(*display, tags);
    out.id = static_cast<int32_t>(int_of(entry.field("id"), -1));
    if (const auto* group = entry.field("group"); group != nullptr && group->as_int()) {
        out.group = static_cast<int32_t>(*group->as_int());
    }
    const auto* category = registries.at("minecraft:recipe_book_category",
                                         static_cast<size_t>(int_of(entry.field("category"))));
    if (category != nullptr) {
        constexpr std::string_view order[4] = {"crafting_equipment", "crafting_building_blocks",
                                               "crafting_misc", "crafting_redstone"};
        for (int i = 0; i < 4; ++i) {
            if (without_namespace(category->name) == order[i]) out.category = i;
        }
    }
    if (const auto* ingredients = entry.field("ingredients");
        ingredients != nullptr && ingredients->as_list() != nullptr) {
        std::vector<std::vector<int32_t>> requirements;
        for (const auto& set : *ingredients->as_list()) requirements.push_back(id_set(set, tags));
        out.requirements = std::move(requirements);
    }
    return out;
}

std::vector<std::pair<size_t, int>> place_recipe(int grid_width, int grid_height,
                                                 int recipe_width, int recipe_height,
                                                 size_t ingredients) {
    std::vector<std::pair<size_t, int>> out;
    size_t next = 0;
    int slot = 0;
    for (int y = 0; y < grid_height; ++y) {
        bool centred = static_cast<float>(recipe_height) < static_cast<float>(grid_height) / 2.0f;
        int start = static_cast<int>(std::floor(static_cast<float>(grid_height) / 2.0f -
                                                static_cast<float>(recipe_height) / 2.0f));
        if (centred && start > y) {
            slot += grid_width;
            ++y;
        }
        for (int x = 0; x < grid_width; ++x) {
            if (next >= ingredients) return out;
            centred = static_cast<float>(recipe_width) < static_cast<float>(grid_width) / 2.0f;
            start = static_cast<int>(std::floor(static_cast<float>(grid_width) / 2.0f -
                                                static_cast<float>(recipe_width) / 2.0f));
            int end = recipe_width;
            bool inside = x < recipe_width;
            if (centred) {
                end = start + recipe_width;
                inside = start <= x && x < start + recipe_width;
            }
            if (inside) {
                out.emplace_back(next++, slot);
            } else if (end == x) {
                slot += grid_width - x;
                break;
            }
            ++slot;
        }
    }
    return out;
}

} // namespace mcpp::ui
