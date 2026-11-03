#include "mcpp/proto/metadata.hpp"
#include "metadata_gen.hpp"

#include <algorithm>
#include <array>
#include <format>

namespace mcpp::proto {
namespace {

struct Serializer {
    std::string_view name;
    std::string_view layout;
};

// In MetaType order, which is protocol 774's numbering.
constexpr std::array<Serializer, 39> kSerializers{{
    {"byte", "byte"},
    {"varint", "varint"},
    {"varlong", "varlong"},
    {"float", "float"},
    {"string", "string"},
    {"component", "component"},
    {"optional_component", "opt(component)"},
    {"item_stack", "slot"},
    {"boolean", "bool"},
    {"rotations", "{x:float y:float z:float}"},
    {"block_pos", "position"},
    {"optional_block_pos", "opt(position)"},
    {"direction", "enum(down,up,north,south,west,east)"},
    {"optional_living_entity", "opt(uuid)"},
    {"block_state", "varint"},
    {"optional_block_state", "varint"},
    {"particle", "@particle"},
    {"particles", "list(@particle)"},
    {"villager_data",
     "{type:id(minecraft:villager_type) profession:id(minecraft:villager_profession) "
     "level:varint}"},
    {"optional_unsigned_int", "optvarint"},
    {"pose",
     "enum(standing,fall_flying,sleeping,swimming,spin_attack,crouching,long_jumping,dying,"
     "croaking,using_tongue,sitting,roaring,sniffing,emerging,digging,sliding,shooting,"
     "inhaling)"},
    {"cat_variant", "id(minecraft:cat_variant)"},
    {"cow_variant", "id(minecraft:cow_variant)"},
    {"wolf_variant", "id(minecraft:wolf_variant)"},
    {"wolf_sound_variant", "id(minecraft:wolf_sound_variant)"},
    {"frog_variant", "id(minecraft:frog_variant)"},
    {"pig_variant", "id(minecraft:pig_variant)"},
    {"chicken_variant", "id(minecraft:chicken_variant)"},
    {"zombie_nautilus_variant", "id(minecraft:zombie_nautilus_variant)"},
    {"optional_global_pos", "opt({dimension:identifier pos:position})"},
    {"painting_variant", "idor(minecraft:painting_variant, @painting_variant)"},
    {"sniffer_state", "enum(idling,feeling_happy,scenting,sniffing,searching,digging,rising)"},
    {"armadillo_state", "enum(idle,rolling,scared,unrolling)"},
    {"copper_golem_state",
     "enum(idle,getting_item,getting_no_item,dropping_item,dropping_no_item)"},
    {"weathering_copper_state", "enum(unaffected,exposed,weathered,oxidized)"},
    {"vector3", "{x:float y:float z:float}"},
    {"quaternion", "{x:float y:float z:float w:float}"},
    {"resolvable_profile", "@resolvable_profile"},
    {"humanoid_arm", "enum(left,right)"},
}};

constexpr uint8_t kEnd = 0xFF;

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

std::string_view meta_type_name(MetaType type) {
    const auto index = static_cast<size_t>(type);
    return index < kSerializers.size() ? kSerializers[index].name : std::string_view{};
}

Metadata read_metadata(ByteBuffer& in, int32_t protocol) {
    if (protocol != 774) {
        throw ProtocolError(std::format("no entity data serializers for protocol {}", protocol));
    }
    Metadata out;
    for (;;) {
        const uint8_t index = in.read_u8();
        if (index == kEnd) return out;
        const int32_t type = in.read_varint();
        if (type < 0 || static_cast<size_t>(type) >= kSerializers.size()) {
            throw ProtocolError(std::format("entity data index {} has serializer {}", index, type));
        }
        Value value = read_layout(in, kSerializers[static_cast<size_t>(type)].layout, protocol);
        out.set(index, static_cast<MetaType>(type), std::move(value));
    }
}

std::span<const MetaField> metadata_fields(std::string_view entity_type) {
    const auto& table = generated::metadata_fields();
    const auto found = table.find(entity_type);
    if (found == table.end()) return {};
    return found->second;
}

const MetaField* metadata_field(std::string_view entity_type, uint8_t index) {
    const auto fields = metadata_fields(entity_type);
    return index < fields.size() ? &fields[index] : nullptr;
}

int metadata_index(std::string_view entity_type, std::string_view name) {
    for (const auto& field : metadata_fields(entity_type)) {
        if (field.name == name) return field.index;
    }
    return -1;
}

} // namespace mcpp::proto
