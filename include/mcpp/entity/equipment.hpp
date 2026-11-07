#pragma once
#include "mcpp/entity/assets.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mcpp::entity {

// `EquipmentClientInfo.LayerType`, in the enum's own order.
enum class LayerType : uint8_t {
    Humanoid,
    HumanoidLeggings,
    Wings,
    WolfBody,
    HorseBody,
    LlamaBody,
    PigSaddle,
    StriderSaddle,
    CamelSaddle,
    CamelHuskSaddle,
    HorseSaddle,
    DonkeySaddle,
    MuleSaddle,
    ZombieHorseSaddle,
    SkeletonHorseSaddle,
    HappyGhastBody,
    NautilusSaddle,
    NautilusBody,
};
constexpr size_t kLayerTypes = 18;

// The name the game serialises a layer type under ("humanoid_leggings").
std::string_view layer_type_name(LayerType type);

// `EquipmentClientInfo.Layer`.
struct EquipmentLayer {
    std::string texture;              // under textures/entity/, no extension
    bool dyeable = false;
    uint32_t colour_when_undyed = 0;  // ARGB; 0 when the layer names none
    bool use_player_texture = false;
};

// `EquipmentAssetManager` and the `minecraft:equippable` component, baked.
class EquipmentAssets {
public:
    bool open(const std::filesystem::path& path);
    void parse(std::string_view text);   // throws with the line number
    // What an asset draws for one layer type; empty when it draws nothing.
    std::span<const EquipmentLayer> layers(std::string_view asset, LayerType type) const;
    // Which asset an item equips as, and the `EquipmentSlot` it goes in
    // (the game's own ordinal); nullptr for an item that equips as nothing.
    struct Equippable { std::string_view asset; uint8_t slot = 0; };
    const Equippable* equippable(int32_t item) const;
    static std::filesystem::path default_path(int32_t protocol);

    size_t asset_count() const { return assets_.size(); }
    size_t layer_count() const { return layers_.size(); }
    size_t equippable_count() const { return equippable_.size(); }

private:
    struct Range {
        uint32_t at = 0;
        uint32_t count = 0;
    };
    struct Asset {
        Range by_type[kLayerTypes];
    };

    std::string_view intern(std::string_view asset);

    std::map<std::string, Asset, std::less<>> assets_;
    std::vector<EquipmentLayer> layers_;
    std::unordered_map<int32_t, Equippable> equippable_;
};

// `EquipmentLayerRenderer.getColorForLayer`: the ARGB one layer is drawn in,
// 0 when it is not drawn at all. `dyed` is the item's `minecraft:dyed_color`,
// or 0 when it has none.
uint32_t equipment_layer_colour(const EquipmentLayer& layer, uint32_t dyed);

// The armour trim sprites, which exist in no asset of the jar's own: the
// `paletted_permutations` source of assets/minecraft/atlases/armor_trims.json
// builds each one, and tools/bake_equipment.py built them once instead.
class EquipmentTrims {
public:
    bool open(const std::filesystem::path& path);
    // By the id `ArmorTrim.layerAssetId` makes.
    const Texture* sprite(std::string_view id) const;
    size_t size() const { return sprites_.size(); }
    static std::filesystem::path default_path(int32_t protocol);
    // `LayerType.trimAssetPrefix`, `ArmorTrim.layerAssetId`: which sprite a
    // `minecraft:trim` asks for, from the pattern's `asset_id` and the
    // material asset group's suffix for this equipment asset.
    static std::string sprite_id(LayerType type, std::string_view pattern,
                                 std::string_view material);

private:
    std::map<std::string, Texture, std::less<>> sprites_;
};

} // namespace mcpp::entity
