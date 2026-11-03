#pragma once
#include "mcpp/nbt.hpp"

#include <cstdint>
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

    size_t size() const { return registries_.size(); }
    const std::map<std::string, std::vector<RegistryEntry>, std::less<>>& all() const {
        return registries_;
    }

private:
    std::map<std::string, std::vector<RegistryEntry>, std::less<>> registries_;
};

} // namespace mcpp
