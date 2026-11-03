#include "mcpp/proto/metadata.hpp"

#include <algorithm>

namespace mcpp::proto {
namespace {

auto position_of(std::vector<Metadata::Entry>& entries, uint8_t index) {
    return std::lower_bound(entries.begin(), entries.end(), index,
                            [](const Metadata::Entry& e, uint8_t i) { return e.index < i; });
}

} // namespace

const Metadata::Entry* Metadata::entry(uint8_t index) const {
    const auto found = std::lower_bound(entries_.begin(), entries_.end(), index,
                                        [](const Entry& e, uint8_t i) { return e.index < i; });
    return found != entries_.end() && found->index == index ? &*found : nullptr;
}

const Value* Metadata::get(uint8_t index) const {
    const Entry* found = entry(index);
    return found != nullptr ? &found->value : nullptr;
}

void Metadata::set(uint8_t index, MetaType type, Value value) {
    const auto found = position_of(entries_, index);
    if (found != entries_.end() && found->index == index) {
        found->type = type;
        found->value = std::move(value);
        return;
    }
    entries_.insert(found, Entry{index, type, std::move(value)});
}

void Metadata::merge(const Metadata& update) {
    for (const auto& e : update.entries_) set(e.index, e.type, e.value);
}

int64_t Metadata::int_at(uint8_t index, int64_t fallback) const {
    const Value* value = get(index);
    return value != nullptr ? value->as_int().value_or(fallback) : fallback;
}

double Metadata::float_at(uint8_t index, double fallback) const {
    const Value* value = get(index);
    return value != nullptr ? value->as_double().value_or(fallback) : fallback;
}

bool Metadata::bool_at(uint8_t index, bool fallback) const {
    const Value* value = get(index);
    return value != nullptr ? value->as_bool().value_or(fallback) : fallback;
}

const nbt::Value* Metadata::component_at(uint8_t index) const {
    const Value* value = get(index);
    return value != nullptr ? value->as_nbt() : nullptr;
}

const ItemStack* Metadata::stack_at(uint8_t index) const {
    const Value* value = get(index);
    return value != nullptr ? value->as_stack() : nullptr;
}

std::optional<BlockPos> Metadata::pos_at(uint8_t index) const {
    const Value* value = get(index);
    const BlockPos* pos = value != nullptr ? value->as_pos() : nullptr;
    return pos != nullptr ? std::optional<BlockPos>(*pos) : std::nullopt;
}

const std::string* Metadata::string_at(uint8_t index) const {
    const Value* value = get(index);
    return value != nullptr ? value->as_string() : nullptr;
}

} // namespace mcpp::proto
