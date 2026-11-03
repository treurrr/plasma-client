#pragma once
#include "mcpp/nbt.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mcpp {

// One entry the server sent in configuration, with its body when it sent one.
struct RegistryEntry {
    std::string name;
    std::optional<nbt::Value> data;
};

// Every registry the server sent, kept as it came. The order of a registry's
// entries is the order every id in the game indexes into - a biome in a chunk,
// a damage type in a death, a variant on a wolf - so it is kept exactly.
class Registries {
public:
    void set(std::string name, std::vector<RegistryEntry> entries) {
        registries_[std::move(name)] = std::move(entries);
    }

    const std::vector<RegistryEntry>* find(std::string_view registry) const {
        const auto found = registries_.find(registry);
        return found == registries_.end() ? nullptr : &found->second;
    }

    const RegistryEntry* at(std::string_view registry, size_t index) const {
        const auto* entries = find(registry);
        return entries != nullptr && index < entries->size() ? &(*entries)[index] : nullptr;
    }

    // -1 when the registry or the entry is not one the server sent.
    int32_t index_of(std::string_view registry, std::string_view entry) const {
        const auto* entries = find(registry);
        if (entries == nullptr) return -1;
        for (size_t i = 0; i < entries->size(); ++i) {
            if ((*entries)[i].name == entry) return static_cast<int32_t>(i);
        }
        return -1;
    }

    // The registries a client has built in and the server never sends:
    // attributes, effects, entity types. A registry the server did send is
    // left as the server sent it.
    bool load_builtin(const std::filesystem::path& path);
    static std::filesystem::path builtin_path(int32_t protocol);

    size_t size() const { return registries_.size(); }
    const std::map<std::string, std::vector<RegistryEntry>, std::less<>>& all() const {
        return registries_;
    }

private:
    std::map<std::string, std::vector<RegistryEntry>, std::less<>> registries_;
};

// Named sets of ids into one registry: which blocks a pickaxe mines, which
// fluids are water. Each update replaces a registry's tags whole.
class Tags {
public:
    using Set = std::map<std::string, std::vector<int32_t>, std::less<>>;

    void set(std::string registry, Set tags) { tags_[std::move(registry)] = std::move(tags); }

    const std::vector<int32_t>* find(std::string_view registry, std::string_view tag) const {
        const auto found = tags_.find(registry);
        if (found == tags_.end()) return nullptr;
        const auto entry = found->second.find(tag);
        return entry == found->second.end() ? nullptr : &entry->second;
    }

    bool contains(std::string_view registry, std::string_view tag, int32_t id) const {
        const auto* ids = find(registry, tag);
        return ids != nullptr && std::find(ids->begin(), ids->end(), id) != ids->end();
    }

    size_t size() const { return tags_.size(); }

private:
    std::map<std::string, Set, std::less<>> tags_;
};

} // namespace mcpp
