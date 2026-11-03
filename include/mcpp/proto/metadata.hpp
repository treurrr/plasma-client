#pragma once
#include "mcpp/proto/components.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace mcpp::proto {

// The entity data serializers, numbered as protocol 774 numbers them.
enum class MetaType : int32_t {
    Byte, VarInt, VarLong, Float, String, Component, OptionalComponent, ItemStack, Boolean,
    Rotations, BlockPos, OptionalBlockPos, Direction, OptionalLivingEntity, BlockState,
    OptionalBlockState, Particle, Particles, VillagerData, OptionalUnsignedInt, Pose, CatVariant,
    CowVariant, WolfVariant, WolfSoundVariant, FrogVariant, PigVariant, ChickenVariant,
    ZombieNautilusVariant, OptionalGlobalPos, PaintingVariant, SnifferState, ArmadilloState,
    CopperGolemState, WeatheringCopperState, Vector3, Quaternion, ResolvableProfile, HumanoidArm,
};

enum class Pose : int32_t {
    Standing, FallFlying, Sleeping, Swimming, SpinAttack, Crouching, LongJumping, Dying,
    Croaking, UsingTongue, Sitting, Roaring, Sniffing, Emerging, Digging, Sliding, Shooting,
    Inhaling,
};

// Everything set_entity_data said about one entity, the latest value per
// index. Indices mean something different per entity type; the ones every
// entity shares have getters of their own.
class Metadata {
public:
    struct Entry {
        uint8_t index = 0;
        MetaType type = MetaType::Byte;
        Value value;
    };

    bool empty() const { return entries_.empty(); }
    size_t size() const { return entries_.size(); }
    std::span<const Entry> entries() const { return entries_; }   // sorted by index

    const Entry* entry(uint8_t index) const;
    const Value* get(uint8_t index) const;
    bool has(uint8_t index) const { return entry(index) != nullptr; }
    // Replaces the value at that index, or adds it.
    void set(uint8_t index, MetaType type, Value value);
    // An update carries only the indices that changed.
    void merge(const Metadata& update);

    // The value at an index read as a type, or the fallback when the server
    // never sent it or sent something else.
    int64_t int_at(uint8_t index, int64_t fallback) const;
    double float_at(uint8_t index, double fallback) const;
    bool bool_at(uint8_t index, bool fallback) const;
    const nbt::Value* component_at(uint8_t index) const;   // absent optional: nullptr
    const ItemStack* stack_at(uint8_t index) const;
    std::optional<BlockPos> pos_at(uint8_t index) const;
    const std::string* string_at(uint8_t index) const;   // strings and uuids

    // Every entity: 0..7.
    static constexpr uint8_t kFlags = 0;
    static constexpr uint8_t kAir = 1;
    static constexpr uint8_t kCustomName = 2;
    static constexpr uint8_t kCustomNameVisible = 3;
    static constexpr uint8_t kSilent = 4;
    static constexpr uint8_t kNoGravity = 5;
    static constexpr uint8_t kPose = 6;
    static constexpr uint8_t kFrozenTicks = 7;
    // Every living entity: 8..14.
    static constexpr uint8_t kLivingFlags = 8;
    static constexpr uint8_t kHealth = 9;
    static constexpr uint8_t kEffectParticles = 10;
    static constexpr uint8_t kEffectAmbient = 11;
    static constexpr uint8_t kArrows = 12;
    static constexpr uint8_t kStingers = 13;
    static constexpr uint8_t kSleepingPos = 14;

    enum Flag : uint8_t {
        OnFire = 0x01, Crouching = 0x02, Sprinting = 0x08, Swimming = 0x10, Invisible = 0x20,
        Glowing = 0x40, FallFlying = 0x80,
    };

    uint8_t flags() const { return static_cast<uint8_t>(int_at(kFlags, 0)); }
    bool flag(Flag bit) const { return (flags() & bit) != 0; }
    Pose pose() const { return static_cast<Pose>(int_at(kPose, 0)); }
    const nbt::Value* custom_name() const { return component_at(kCustomName); }
    bool custom_name_visible() const { return bool_at(kCustomNameVisible, false); }

private:
    std::vector<Entry> entries_;
};

} // namespace mcpp::proto
